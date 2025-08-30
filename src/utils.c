#include "utils.h"

#include <json-glib/json-glib.h>

gchar* humanize_ago(GDateTime *t) {
	if (!t) return g_strdup("unknown");

	GDateTime *now = g_date_time_new_now(g_date_time_get_timezone(t));
	gint64 span_us = g_date_time_difference(now, t); // positive if t <= now
	g_date_time_unref(now);

	if (span_us < 0)  // t is in the future, but we said we don’t care
			return g_strdup("in the future");

	const gint64 sec = span_us / G_TIME_SPAN_SECOND;
	const gint64 min = span_us / G_TIME_SPAN_MINUTE;

	if (sec < 5)
		return g_strdup("just now");
	if (sec < 60)
		return g_strdup_printf("%ld seconds ago", (long)sec);
	if (min == 1)
		return g_strdup("a minute ago");
	if (min < 60)
		return g_strdup_printf("%ld minutes ago", (long)min);

	return g_strdup("more than an hour ago");
}

const char *format_title(const char *callsign, const char *park_ref)
{
	if (callsign && park_ref)
		return g_strdup_printf("%s @ %s", callsign, park_ref);
	
	if (callsign) return g_strdup(callsign);
	if (park_ref) return g_strdup(park_ref);
	return g_strdup("POTA Spot");
}   

const char *park_uri_from_ref(const char *park_ref)
{
  if (!park_ref || !*park_ref) return g_strdup("https://pota.app");
    return g_strdup_printf("https://pota.app/#/park/%s", park_ref);
}

const char *band_from_hz(int hz) {
  double mhz = (double)hz / 1e3;
  if (mhz >= 1.8   && mhz < 2.0)   return BANDS[1];
  if (mhz >= 3.5   && mhz < 4.1)   return BANDS[2];
  if (mhz >= 5.25  && mhz < 5.45)  return BANDS[3];
  if (mhz >= 7.0   && mhz < 7.3)   return BANDS[4];
  if (mhz >= 10.1  && mhz < 10.15) return BANDS[5];
  if (mhz >= 14.0  && mhz < 14.35) return BANDS[6];
  if (mhz >= 18.068&& mhz < 18.168)return BANDS[7];
  if (mhz >= 21.0  && mhz < 21.45) return BANDS[8];
  if (mhz >= 24.89 && mhz < 24.99) return BANDS[9];
  if (mhz >= 28.0  && mhz < 29.7)  return BANDS[10];
  if (mhz >= 50.0  && mhz < 54.0)  return BANDS[11];
  if (mhz >= 144.0 && mhz < 148.0) return BANDS[12];
  return "Other";
}

int gen_aprs_is_passcode(const char *callsign)
{
	if (!callsign || !*callsign) return -1;

	guint16 hash = 0x73E2; 
	const unsigned char *p = (const unsigned char *)callsign;

	while (*p && *p != '-') {
			unsigned char c1 = (unsigned char)g_ascii_toupper(*p++);
			hash ^= (guint16)(c1 << 8);

			if (*p && *p != '-') {
					unsigned char c2 = (unsigned char)g_ascii_toupper(*p++);
					hash ^= (guint16)c2;
			}
	}

	return (int)(hash & 0x7FFF);
}

