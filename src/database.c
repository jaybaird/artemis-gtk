// database.c
#include "database.h"
#include "glib.h"
#include <sqlite3.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC(SpotDb, spot_db_free)

static gboolean spot_db_init_schema(sqlite3 *db);
static gboolean sqlite_exec_or_fail(sqlite3 *db, const char *sql);

// Singleton instance
static SpotDb *g_spot_db_instance = NULL;
static GMutex g_spot_db_mutex;

SpotDb* spot_db_new(void)
{
    SpotDb* db = g_new0(SpotDb, 1);

    const gchar *data_dir = g_get_user_data_dir();
    g_autofree gchar *app_dir = g_build_filename(data_dir, "artemis", NULL);
    g_mkdir_with_parents(app_dir, 0700);

    g_autofree gchar *db_path = g_build_filename(app_dir, "spots.db", NULL);

    int rc = sqlite3_open(db_path, &db->spot_db);
    if (rc != SQLITE_OK) {
        g_critical("Cannot open DB at %s: %s", db_path, sqlite3_errmsg(db->spot_db));
        if (db->spot_db) sqlite3_close(db->spot_db);
        g_free(db);
        return NULL;
    }

    if (!sqlite_exec_or_fail(db->spot_db, "PRAGMA journal_mode=WAL;") ||
        !sqlite_exec_or_fail(db->spot_db, "PRAGMA synchronous=NORMAL;") ||
        !sqlite_exec_or_fail(db->spot_db, "PRAGMA foreign_keys=ON;")) {
        sqlite3_close(db->spot_db);
        g_free(db);
        return NULL;
    }
    sqlite3_busy_timeout(db->spot_db, 3000);

    if (!spot_db_init_schema(db->spot_db)) {
        sqlite3_close(db->spot_db);
        g_free(db);
        return NULL;
    }

    g_message("DB opened: %s", db_path);
    return db;
}

void spot_db_free(SpotDb* db)
{
    if (!db) return;
    if (db->spot_db) { sqlite3_close(db->spot_db); db->spot_db = NULL; }
    g_free(db);
}

/* Ensure tables/triggers exist */
static gboolean spot_db_init_schema(sqlite3 *db)
{
    const char *schema[] = {
        "CREATE TABLE IF NOT EXISTS parks ("
        "  reference TEXT PRIMARY KEY,"
        "  park_name TEXT,"
        "  dx_entity TEXT,"
        "  location  TEXT,"
        "  hasc      TEXT,"
        "  first_qso_date DATETIME,"
        "  qso_count INTEGER NOT NULL DEFAULT 0"
        ");",

        "CREATE TABLE IF NOT EXISTS qsos ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  park_ref TEXT NOT NULL,"
        "  callsign TEXT NOT NULL,"
        "  mode TEXT,"
        "  frequency_hz INTEGER,"
        "  created_utc DATETIME NOT NULL,"
        "  spotter TEXT,"
        "  spotter_comment TEXT,"
        "  activator_comment TEXT,"
        "  FOREIGN KEY(park_ref) REFERENCES parks(reference) ON DELETE CASCADE"
        ");",

        "CREATE INDEX IF NOT EXISTS idx_qsos_park_ref ON qsos(park_ref);",
        "CREATE INDEX IF NOT EXISTS idx_qsos_created  ON qsos(created_utc);",

        "CREATE TRIGGER IF NOT EXISTS trg_qsos_ai "
        "AFTER INSERT ON qsos "
        "FOR EACH ROW BEGIN "
        "  UPDATE parks "
        "    SET qso_count = qso_count + 1, "
        "        first_qso_date = CASE "
        "            WHEN first_qso_date IS NULL THEN NEW.created_utc "
        "            WHEN NEW.created_utc < first_qso_date THEN NEW.created_utc "
        "            ELSE first_qso_date "
        "        END "
        "  WHERE reference = NEW.park_ref; "
        "END;",

        "CREATE TRIGGER IF NOT EXISTS trg_qsos_ad "
        "AFTER DELETE ON qsos "
        "FOR EACH ROW BEGIN "
        "  UPDATE parks "
        "    SET qso_count = CASE WHEN qso_count > 0 THEN qso_count - 1 ELSE 0 END, "
        "        first_qso_date = (SELECT MIN(created_utc) FROM qsos WHERE park_ref = OLD.park_ref) "
        "  WHERE reference = OLD.park_ref; "
        "END;"
    };

    for (gsize i = 0; i < G_N_ELEMENTS(schema); ++i) {
        if (!sqlite_exec_or_fail(db, schema[i])) {
            return FALSE;
        }
    }
    return TRUE;
}

// helper: format a borrowed GDateTime* as ISO8601 Z (allocates; caller frees)
static gchar *iso8601_from_borrowed_utc(GDateTime *dt) {
  if (!dt) return NULL;
  // Ensure UTC, but since your getter says "borrowed", don't unref the input
  g_autoptr(GDateTime) utc = g_date_time_to_utc(dt);
  return g_date_time_format(utc, "%Y-%m-%dT%H:%M:%SZ");
}

gboolean spot_db_add_qso_from_spot(SpotDb *db, ArtemisSpot *spot,
                                   sqlite3_int64 *out_qso_id, GError **error) {
  g_return_val_if_fail(db && db->spot_db && spot, FALSE);

  // Borrowed pointers from ArtemisSpot (do not free)
  const char *callsign          = artemis_spot_get_callsign(spot);
  const char *park_ref          = artemis_spot_get_park_ref(spot);
  const char *park_name         = artemis_spot_get_park_name(spot);
  const char *location_desc     = artemis_spot_get_location_desc(spot);
  const char *mode              = artemis_spot_get_mode(spot);
  int         frequency_hz      = artemis_spot_get_frequency_hz(spot);
  GDateTime  *spot_time_borrow  = artemis_spot_get_spot_time(spot);     // borrowed
  const char *spotter           = artemis_spot_get_spotter(spot);
  const char *spotter_comment   = artemis_spot_get_spotter_comment(spot);
  const char *activator_comment = artemis_spot_get_activator_comment(spot);

  g_autofree gchar *created_iso = iso8601_from_borrowed_utc(spot_time_borrow);

  if (!park_ref || !callsign || !created_iso) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                "Missing required fields (park_ref/callsign/spot_time)");
    return FALSE;
  }

  int rc = sqlite3_exec(db->spot_db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
  if (rc != SQLITE_OK) {
    g_set_error(error, G_IO_ERROR, rc, "BEGIN failed: %s", sqlite3_errmsg(db->spot_db));
    return FALSE;
  }

  // Ensure park row exists (also upsert name/location if you want to keep them)
  {
    const char *sql =
      "INSERT INTO parks(reference, park_name, location) VALUES(?, ?, ?) "
      "ON CONFLICT(reference) DO UPDATE SET "
      "  park_name = COALESCE(excluded.park_name, parks.park_name), "
      "  location  = COALESCE(excluded.location,  parks.location);";

    sqlite3_stmt *st = NULL;
    rc = sqlite3_prepare_v2(db->spot_db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) goto sqlite_fail;

    sqlite3_bind_text(st, 1, park_ref,      -1, SQLITE_TRANSIENT);
    if (park_name)     sqlite3_bind_text(st, 2, park_name,     -1, SQLITE_TRANSIENT);
    else               sqlite3_bind_null(st, 2);
    if (location_desc) sqlite3_bind_text(st, 3, location_desc, -1, SQLITE_TRANSIENT);
    else               sqlite3_bind_null(st, 3);

    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) goto sqlite_fail;
  }

  // Insert QSO row
  {
    // NOTE: schema from earlier: qsos(park_ref, callsign, mode, frequency_hz, created_utc, spotter, spotter_comment, activator_comment)
    // If you want to store 'band', add a TEXT column 'band' and bind it too.
    const char *sql =
      "INSERT INTO qsos("
      "  park_ref, callsign, mode, frequency_hz, created_utc, "
      "  spotter, spotter_comment, activator_comment"
      ") VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *st = NULL;
    rc = sqlite3_prepare_v2(db->spot_db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) goto sqlite_fail;

    sqlite3_bind_text(st, 1, park_ref,  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, callsign,  -1, SQLITE_TRANSIENT);
    if (mode)         sqlite3_bind_text(st, 3, mode,        -1, SQLITE_TRANSIENT);
    else              sqlite3_bind_null(st, 3);
    if (frequency_hz) sqlite3_bind_int (st, 4, frequency_hz);
    else              sqlite3_bind_null(st, 4);
    sqlite3_bind_text(st, 5, created_iso, -1, SQLITE_TRANSIENT);
    if (spotter)           sqlite3_bind_text(st, 6, spotter,         -1, SQLITE_TRANSIENT);
    else                   sqlite3_bind_null(st, 6);
    if (spotter_comment)   sqlite3_bind_text(st, 7, spotter_comment, -1, SQLITE_TRANSIENT);
    else                   sqlite3_bind_null(st, 7);
    if (activator_comment) sqlite3_bind_text(st, 8, activator_comment, -1, SQLITE_TRANSIENT);
    else                   sqlite3_bind_null(st, 8);

    rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) {
      sqlite3_finalize(st);
      goto sqlite_fail;
    }

    if (out_qso_id) *out_qso_id = sqlite3_last_insert_rowid(db->spot_db);
    sqlite3_finalize(st);
  }

  rc = sqlite3_exec(db->spot_db, "COMMIT;", NULL, NULL, NULL);
  if (rc != SQLITE_OK) {
    g_set_error(error, G_IO_ERROR, rc, "COMMIT failed: %s", sqlite3_errmsg(db->spot_db));
    return FALSE;
  }

  return TRUE;

sqlite_fail:
  {
    const char *msg = sqlite3_errmsg(db->spot_db);
    sqlite3_exec(db->spot_db, "ROLLBACK;", NULL, NULL, NULL);
    g_set_error(error, G_IO_ERROR, rc, "SQLite error: %s", msg);
    return FALSE;
  }
}

gboolean spot_db_add_park(SpotDb *db, const char *reference, const char *park_name,
                          const char *dx_entity, const char *location, const char *hasc,
                          gint qso_count, GError **error) {
  g_return_val_if_fail(db && db->spot_db && reference && *reference, FALSE);

  const char *sql = 
    "INSERT OR REPLACE INTO parks(reference, park_name, dx_entity, location, hasc, qso_count) "
    "VALUES(?, ?, ?, ?, ?, ?);";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2(db->spot_db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK) {
    g_set_error(error, G_IO_ERROR, rc, "Failed to prepare park insert: %s", sqlite3_errmsg(db->spot_db));
    return FALSE;
  }

  sqlite3_bind_text(st, 1, reference, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, park_name ? park_name : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, dx_entity ? dx_entity : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, location ? location : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, hasc ? hasc : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 6, qso_count >= 0 ? qso_count : 0);

  rc = sqlite3_step(st);
  sqlite3_finalize(st);
  
  if (rc != SQLITE_DONE) {
    g_set_error(error, G_IO_ERROR, rc, "Failed to insert park: %s", sqlite3_errmsg(db->spot_db));
    return FALSE;
  }

  return TRUE;
}

gboolean
spot_db_is_park_hunted(SpotDb *db, const char *park_reference) {
  g_return_val_if_fail(db && db->spot_db && park_reference && *park_reference, FALSE);

  const char *sql = "SELECT qso_count FROM parks WHERE reference = ? AND qso_count > 0;";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2(db->spot_db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK) {
    g_warning("Failed to prepare park hunt check: %s", sqlite3_errmsg(db->spot_db));
    return FALSE;
  }

  sqlite3_bind_text(st, 1, park_reference, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step(st);
  gboolean hunted = (rc == SQLITE_ROW);

  sqlite3_finalize(st);
  return hunted;
}

/* Helpers */
static gboolean sqlite_exec_or_fail(sqlite3 *db, const char *sql)
{
    char *err=NULL; int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        g_critical("SQL error running '%s': %s", sql, err?err:"(unknown)");
        if (err) sqlite3_free(err);
        return FALSE;
    }
    return TRUE;
}

// ... includes and existing code ...

static QsoRow* qso_row_from_stmt(sqlite3_stmt *st);
static gchar* iso8601_day_start(GDateTime *utc_any);
static gchar* iso8601_next_day_start(GDateTime *utc_any);

/* ----------------- Ownership helpers ----------------- */
void
qso_row_free(QsoRow *row) {
    if (!row) return;
    g_free(row->park_ref);
    g_free(row->callsign);
    g_free(row->mode);
    g_free(row->created_utc);
    g_free(row->spotter);
    g_free(row->spotter_comment);
    g_free(row->activator_comment);
    g_free(row);
}

void
qso_row_array_free(GPtrArray *rows) {
    if (!rows) return;
    for (guint i = 0; i < rows->len; ++i)
        qso_row_free((QsoRow*)rows->pdata[i]);
    g_ptr_array_free(rows, TRUE);
}

/* Build row from current sqlite3_stmt */
static QsoRow* qso_row_from_stmt(sqlite3_stmt *st)
{
    QsoRow *r = g_new0(QsoRow, 1);
    r->id               = sqlite3_column_int64(st, 0);
    r->park_ref         = g_strdup((const char*)sqlite3_column_text(st, 1));
    r->callsign         = g_strdup((const char*)sqlite3_column_text(st, 2));
    r->mode             = sqlite3_column_type(st, 3)==SQLITE_NULL ? NULL : g_strdup((const char*)sqlite3_column_text(st, 3));
    r->frequency_hz     = sqlite3_column_type(st, 4)==SQLITE_NULL ? 0 : sqlite3_column_int(st, 4);
    r->created_utc      = g_strdup((const char*)sqlite3_column_text(st, 5));
    r->spotter          = sqlite3_column_type(st, 6)==SQLITE_NULL ? NULL : g_strdup((const char*)sqlite3_column_text(st, 6));
    r->spotter_comment  = sqlite3_column_type(st, 7)==SQLITE_NULL ? NULL : g_strdup((const char*)sqlite3_column_text(st, 7));
    r->activator_comment= sqlite3_column_type(st, 8)==SQLITE_NULL ? NULL : g_strdup((const char*)sqlite3_column_text(st, 8));
    return r;
}

/* ----------------- 1) Latest QSO per park ----------------- */
GPtrArray* spot_db_latest_qso_per_park(SpotDb *db, GError **error)
{
    g_return_val_if_fail(db && db->spot_db, NULL);

    const char *sql =
        "SELECT q.id, q.park_ref, q.callsign, q.mode, q.frequency_hz, "
        "       q.created_utc, q.spotter, q.spotter_comment, q.activator_comment "
        "FROM qsos q "
        "JOIN (SELECT park_ref, MAX(created_utc) AS maxc FROM qsos GROUP BY park_ref) t "
        "  ON q.park_ref = t.park_ref AND q.created_utc = t.maxc "
        "ORDER BY q.created_utc DESC;";

    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db->spot_db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, rc, "prepare latest_qso_per_park: %s", sqlite3_errmsg(db->spot_db));
        return NULL;
    }

    GPtrArray *rows = g_ptr_array_new_with_free_func((GDestroyNotify)qso_row_free);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        g_ptr_array_add(rows, qso_row_from_stmt(st));
    }

    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, rc, "step latest_qso_per_park: %s", sqlite3_errmsg(db->spot_db));
        sqlite3_finalize(st);
        qso_row_array_free(rows);
        return NULL;
    }

    sqlite3_finalize(st);
    return rows;
}

/* ----------------- 2a) Latest N QSOs overall ----------------- */
GPtrArray* spot_db_latest_qsos(SpotDb *db, int limit, GError **error)
{
    g_return_val_if_fail(db && db->spot_db, NULL);
    if (limit <= 0) limit = 50;

    const char *sql =
        "SELECT id, park_ref, callsign, mode, frequency_hz, created_utc, "
        "       spotter, spotter_comment, activator_comment "
        "FROM qsos "
        "ORDER BY created_utc DESC "
        "LIMIT ?;";

    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db->spot_db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, rc, "prepare latest_qsos: %s", sqlite3_errmsg(db->spot_db));
        return NULL;
    }
    sqlite3_bind_int(st, 1, limit);

    GPtrArray *rows = g_ptr_array_new_with_free_func((GDestroyNotify)qso_row_free);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        g_ptr_array_add(rows, qso_row_from_stmt(st));
    }

    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, rc, "step latest_qsos: %s", sqlite3_errmsg(db->spot_db));
        sqlite3_finalize(st);
        qso_row_array_free(rows);
        return NULL;
    }

    sqlite3_finalize(st);
    return rows;
}

/* ----------------- 2b) Latest QSO for a specific park ----------------- */
QsoRow* spot_db_latest_qso_for_park(SpotDb *db, const char *park_ref, GError **error)
{
    g_return_val_if_fail(db && db->spot_db && park_ref, NULL);

    const char *sql =
        "SELECT id, park_ref, callsign, mode, frequency_hz, created_utc, "
        "       spotter, spotter_comment, activator_comment "
        "FROM qsos "
        "WHERE park_ref = ? "
        "ORDER BY created_utc DESC "
        "LIMIT 1;";

    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db->spot_db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, rc, "prepare latest_qso_for_park: %s", sqlite3_errmsg(db->spot_db));
        return NULL;
    }
    sqlite3_bind_text(st, 1, park_ref, -1, SQLITE_TRANSIENT);

    QsoRow *row = NULL;
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        row = qso_row_from_stmt(st);
        rc = sqlite3_step(st); // should be DONE now
    }

    if (rc != SQLITE_DONE) {
        g_clear_pointer(&row, qso_row_free);
        g_set_error(error, G_IO_ERROR, rc, "step latest_qso_for_park: %s", sqlite3_errmsg(db->spot_db));
        sqlite3_finalize(st);
        return NULL;
    }

    sqlite3_finalize(st);
    return row; // may be NULL if no rows
}

/* ----------------- 3) Did I have a QSO with park on this UTC day? ----------------- */
gboolean spot_db_had_qso_with_park_on_utc_day(SpotDb *db,
                                              const char *park_ref,
                                              GDateTime *utc_when_in_day,
                                              GError **error)
{
    g_return_val_if_fail(db && db->spot_db && park_ref && utc_when_in_day, FALSE);

    // Ensure it’s UTC
    g_autoptr(GDateTime) utc = g_date_time_to_utc(utc_when_in_day);
    g_autofree gchar *start_iso = iso8601_day_start(utc);
    g_autofree gchar *next_iso  = iso8601_next_day_start(utc);

    const char *sql =
        "SELECT EXISTS ("
        "  SELECT 1 FROM qsos "
        "  WHERE park_ref = ? AND created_utc >= ? AND created_utc < ?"
        ");";

    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db->spot_db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, rc, "prepare had_qso_on_day: %s", sqlite3_errmsg(db->spot_db));
        return FALSE;
    }

    sqlite3_bind_text(st, 1, park_ref, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, start_iso, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, next_iso,  -1, SQLITE_TRANSIENT);

    gboolean exists = FALSE;
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        exists = sqlite3_column_int(st, 0) ? TRUE : FALSE;
        rc = sqlite3_step(st);
    }

    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, rc, "step had_qso_on_day: %s", sqlite3_errmsg(db->spot_db));
        sqlite3_finalize(st);
        return FALSE;
    }

    sqlite3_finalize(st);
    return exists;
}

/* ----------------- tiny datetime helpers ----------------- */
/* Returns "YYYY-MM-DDT00:00:00Z" for the UTC day containing utc_any */
static gchar* iso8601_day_start(GDateTime *utc_any)
{
    gint y = g_date_time_get_year(utc_any);
    gint m = g_date_time_get_month(utc_any);
    gint d = g_date_time_get_day_of_month(utc_any);
    g_autoptr(GDateTime) start = g_date_time_new_utc(y, m, d, 0, 0, 0);
    return g_date_time_format(start, "%Y-%m-%dT%H:%M:%SZ");
}

/* Returns next day's start "YYYY-MM-DDT00:00:00Z" (exclusive upper bound) */
static gchar* iso8601_next_day_start(GDateTime *utc_any)
{
    gint y = g_date_time_get_year(utc_any);
    gint m = g_date_time_get_month(utc_any);
    gint d = g_date_time_get_day_of_month(utc_any);
    g_autoptr(GDateTime) start = g_date_time_new_utc(y, m, d, 0, 0, 0);
    g_autoptr(GDateTime) next  = g_date_time_add_days(start, 1);
    return g_date_time_format(next, "%Y-%m-%dT%H:%M:%SZ");
}

/* ----------------- Singleton implementation ----------------- */
SpotDb* spot_db_get_instance(void)
{
    g_mutex_lock(&g_spot_db_mutex);
    
    if (!g_spot_db_instance) {
        g_spot_db_instance = spot_db_new();
        if (!g_spot_db_instance) {
            g_critical("Failed to initialize database - database operations will not work");
        }
    }
    
    g_mutex_unlock(&g_spot_db_mutex);
    return g_spot_db_instance;
}

void spot_db_cleanup_instance(void)
{
    g_mutex_lock(&g_spot_db_mutex);
    
    if (g_spot_db_instance) {
        spot_db_free(g_spot_db_instance);
        g_spot_db_instance = NULL;
    }
    
    g_mutex_unlock(&g_spot_db_mutex);
}
