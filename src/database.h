// database.h
#pragma once
#include <glib.h>
#include <sqlite3.h>
#include "spot.h" 

typedef struct {
    sqlite3 *spot_db;
} SpotDb;

SpotDb* spot_db_new(void);
void    spot_db_free(SpotDb *db);

// Singleton access
SpotDb* spot_db_get_instance(void);
void    spot_db_cleanup_instance(void);

gboolean spot_db_add_qso_from_spot(SpotDb *db, ArtemisSpot *spot,
                                   sqlite3_int64 *out_qso_id, GError **error);

// Row representation for a QSO result
typedef struct {
    sqlite3_int64 id;
    gchar *park_ref;
    gchar *callsign;
    gchar *mode;
    gint   frequency_hz;
    gchar *created_utc;       // ISO 8601 Z (owned string)
    gchar *spotter;
    gchar *spotter_comment;
    gchar *activator_comment;
} QsoRow;

// Ownership helpers
void qso_row_free(QsoRow *row);
void qso_row_array_free(GPtrArray *rows); // frees each row & array

// Query helpers
// 1) Latest QSO per park (one row per park). Ordered by newest first.
//    Returns a GPtrArray* of QsoRow*. Caller owns and must free with qso_row_array_free().
GPtrArray* spot_db_latest_qso_per_park(SpotDb *db, GError **error);

// 2a) Latest N QSOs (across all parks), newest first
GPtrArray* spot_db_latest_qsos(SpotDb *db, int limit, GError **error);

// 2b) Latest QSO for a specific park (NULL if none)
QsoRow* spot_db_latest_qso_for_park(SpotDb *db, const char *park_ref, GError **error);

// 3) Have I had a QSO with this park on the given UTC day?
//    Pass any time inside the desired day (UTC); helper computes [day_start, next_day_start).
gboolean spot_db_had_qso_with_park_on_utc_day(SpotDb *db,
                                               const char *park_ref,
                                               GDateTime *utc_when_in_day,
                                               GError **error);
