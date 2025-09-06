#pragma once

#include <adwaita.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS
#define ARTEMIS_TYPE_STATUS_PAGE (status_page_get_type())

G_DECLARE_FINAL_TYPE(StatusPage, status_page, ARTEMIS, STATUS_PAGE, GtkBox)

StatusPage *status_page_new(void);

void
status_page_set_icon_name(StatusPage *self, const char *icon_name);
void
status_page_set_title(StatusPage *self, const char *title);
void
status_page_set_description(StatusPage *self, const char *description);

G_END_DECLS