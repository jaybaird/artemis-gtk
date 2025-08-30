#include <glib.h>

static const char *const BANDS[] = {
  "All", "160m","80m","60m","40m","30m","20m","17m","15m","12m","10m","6m","2m","70cm"
};
static const char *const MODES[] = { "SSB","CW","FT8","FM","AM","RTTY","JT65" };

gchar* humanize_ago(GDateTime *t);
const char *format_title(const char *callsign, const char *park_ref);
const char *park_uri_from_ref(const char *park_ref);

const char *band_from_hz(int hz);