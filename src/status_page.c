#include "status_page.h"
#include "gtk/gtk.h"
#include "gtk/gtkbinlayout.h"

struct _StatusPage {
  GtkWidget    parent_instance;

  GtkImage    *status_icon;
  GtkLabel    *status_title;
  GtkLabel    *status_body;
};

G_DEFINE_FINAL_TYPE(StatusPage, status_page, GTK_TYPE_BOX);

static void
status_page_dispose(GObject *gobject)
{
  StatusPage *self = ARTEMIS_STATUS_PAGE(gobject);
  gtk_widget_dispose_template(GTK_WIDGET(self), G_OBJECT_TYPE(gobject));
  G_OBJECT_CLASS(status_page_parent_class)->dispose(gobject);
}

static void
status_page_class_init(StatusPageClass *klass) {
  g_type_ensure(ADW_TYPE_CLAMP);

  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_template_from_resource(widget_class, "/com/k0vcz/artemis/data/ui/status_page.ui");

  gtk_widget_class_bind_template_child(widget_class, StatusPage, status_icon);
  gtk_widget_class_bind_template_child(widget_class, StatusPage, status_title);
  gtk_widget_class_bind_template_child(widget_class, StatusPage, status_body);

  G_OBJECT_CLASS(klass)->dispose = status_page_dispose;
}

static void
status_page_init(StatusPage *self) {
  gtk_widget_init_template(GTK_WIDGET(self));
}

StatusPage *
status_page_new(void) {
  return g_object_new(ARTEMIS_TYPE_STATUS_PAGE, NULL);
}

void
status_page_set_icon_name(StatusPage *self, const char *icon_name)
{
  gtk_image_set_from_icon_name(self->status_icon, icon_name);
}

void
status_page_set_title(StatusPage *self, const char *title)
{
  gtk_label_set_label(self->status_title, title);
}

void
status_page_set_description(StatusPage *self, const char *description)
{
  gtk_label_set_label(self->status_body, description);
}
