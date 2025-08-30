#include "config.h"
#include "artemis.h"

int main(int argc, char** argv)
{
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    return g_application_run(G_APPLICATION(g_object_new(ARTEMIS_TYPE_APP,
        "application-id", APPLICATION_ID,
        "flags", G_APPLICATION_DEFAULT_FLAGS,
        NULL)),
        argc, argv);
}