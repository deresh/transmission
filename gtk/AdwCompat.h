// libadwaita glue for the GTK client.
//
// libadwaita has no C++ binding. Its widgets are GtkWidget subclasses and
// are used through the C API. Keep all libadwaita-specific helpers in this file.

#pragma once

#include <adwaita.h>

#include <gtk/gtk.h>

#if !ADW_CHECK_VERSION(1, 5, 0)
#error "libadwaita 1.5 or newer is required"
#endif

// Adds a widget to the end of an AdwHeaderBar.
inline void adw_compat_header_pack_end(GtkWidget* header_bar, GtkWidget* child)
{
    adw_header_bar_pack_end(ADW_HEADER_BAR(header_bar), child);
}

// AdwApplicationWindow takes its content through this call; gtk_window_set_child()
// is rejected by libadwaita for these windows.
inline void adw_compat_set_window_content(GtkWindow* window, GtkWidget* content)
{
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), content);
}
