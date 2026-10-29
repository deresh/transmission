// libadwaita glue for the GTK client.
//
// libadwaita has no C++ binding. Its widgets are GtkWidget subclasses and
// are used through the C API. Keep all libadwaita-specific helpers in this file.

#pragma once

#include <adwaita.h>

#if !ADW_CHECK_VERSION(1, 5, 0)
#error "libadwaita 1.5 or newer is required"
#endif
