#pragma once

#include <glib.h>
#include <sqlite3.h>
#include "spot.h" 

typedef struct {
    sqlite3 *spot_db;
} SpotDb;

SpotDb*
spot_db_new(void);

void
spot_db_free(SpotDb *db);

SpotDb*
spot_db_get_instance(void);

void
spot_db_cleanup_instance(void);

gboolean spot_db_add_qso_from_spot(SpotDb *db, 
                                   ArtemisSpot *spot,
                                   sqlite3_int64 *out_qso_id, 
                                   GError **error);

gboolean 
spot_db_add_park(SpotDb *db, 
                const char *reference, 
                const char *park_name,                 
                const char *dx_entity, 
                const char *location, 
                const char *hasc,                 
                gint qso_count, 
                GError **error);

gboolean
spot_db_is_park_hunted(SpotDb *db, const char *park_reference);

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

void
qso_row_free(QsoRow *row);

void
qso_row_array_free(GPtrArray *rows);

GPtrArray*
spot_db_latest_qso_per_park(SpotDb *db, GError **error);

GPtrArray*
spot_db_latest_qsos(SpotDb *db, int limit, GError **error);

QsoRow*
spot_db_latest_qso_for_park(SpotDb *db, const char *park_ref, GError **error);

gboolean 
spot_db_had_qso_with_park_on_utc_day(SpotDb *db,
                                     const char *park_ref,
                                     GDateTime *utc_when_in_day,
                                     GError **error);

