// logbook_qrz.h - QRZ logbook provider
#pragma once

#include "logbook.h"
#include <libsoup/soup.h>

G_BEGIN_DECLS

#define LOGBOOK_TYPE_QRZ (logbook_qrz_get_type())
G_DECLARE_FINAL_TYPE(LogbookQrz, logbook_qrz, LOGBOOK, QRZ, LogbookProvider)

LogbookQrz* logbook_qrz_new(void);

G_END_DECLS