// libadwaita glue for the GTK client.
//
// libadwaita has no C++ binding. Its widgets are GtkWidget subclasses and
// are used through the C API. Keep all libadwaita-specific helpers in this file.

#pragma once

#include <adwaita.h>

#include <functional>
#include <string>
#include <type_traits>
#include <vector>

#include <gtk/gtk.h>

#if !ADW_CHECK_VERSION(1, 5, 0)
#error "libadwaita 1.5 or newer is required"
#endif

// Adds a widget to the end of an AdwHeaderBar.
inline void adw_compat_header_pack_end(GtkWidget* header_bar, GtkWidget* child)
{
    adw_header_bar_pack_end(ADW_HEADER_BAR(header_bar), child);
}

// Wraps the window content so toasts can be shown over it.
inline GtkWidget* adw_compat_toast_overlay_new(GtkWidget* child)
{
    GtkWidget* const overlay = adw_toast_overlay_new();
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(overlay), child);
    return overlay;
}

inline void adw_compat_toast_show(GtkWidget* overlay, char const* title)
{
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(overlay), adw_toast_new(title));
}

// AdwApplicationWindow takes its content through this call; gtk_window_set_child()
// is rejected by libadwaita for these windows.
inline void adw_compat_set_window_content(GtkWindow* window, GtkWidget* content)
{
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), content);
}

// Connects a handler that takes no arguments to a notify::* signal.
template<typename F>
void adw_compat_connect_notify(gpointer instance, char const* detailed_signal, F&& callback)
{
    using Callback = std::decay_t<F>;
    auto* const data = new Callback(std::forward<F>(callback));
    g_signal_connect_data(
        instance,
        detailed_signal,
        G_CALLBACK(+[](gpointer, GParamSpec*, gpointer user_data) { (*static_cast<Callback*>(user_data))(); }),
        data,
        [](gpointer user_data, GClosure*) { delete static_cast<Callback*>(user_data); },
        GConnectFlags{});
}

// Replaces the model of an AdwComboRow with the given strings.
inline void adw_compat_combo_row_set_strings(GtkWidget* row, std::vector<std::string> const& strings)
{
    auto* const list = gtk_string_list_new(nullptr);
    for (auto const& text : strings)
    {
        gtk_string_list_append(list, text.c_str());
    }
    adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(list));
    g_object_unref(list);
}

// AdwDialog is a GtkWidget, not a GtkWindow, so it is presented over a parent window.
inline void adw_compat_dialog_present(GtkWidget* dialog, GtkWidget* parent)
{
    adw_dialog_present(ADW_DIALOG(dialog), parent);
}

// Creates an AdwAlertDialog. A null or empty body is left out.
inline GtkWidget* adw_compat_alert_dialog_new(char const* heading, char const* body)
{
    auto const* const text = (body != nullptr && body[0] != '\0') ? body : nullptr;
    return GTK_WIDGET(adw_alert_dialog_new(heading, text));
}

// Adds a response button to an AdwAlertDialog.
inline void adw_compat_alert_add_response(
    GtkWidget* dialog,
    char const* id,
    char const* label,
    AdwResponseAppearance appearance = ADW_RESPONSE_DEFAULT)
{
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), id, label);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), id, appearance);
}

// Runs callback(response id) when the user picks a response. The callback is owned by the signal connection.
template<typename F>
void adw_compat_alert_on_response(GtkWidget* dialog, F&& callback)
{
    using Callback = std::decay_t<F>;
    auto* const data = new Callback(std::forward<F>(callback));
    g_signal_connect_data(
        dialog,
        "response",
        G_CALLBACK(+[](AdwAlertDialog*, char const* response, gpointer user_data)
                   { (*static_cast<Callback*>(user_data))(response); }),
        data,
        [](gpointer user_data, GClosure*) { delete static_cast<Callback*>(user_data); },
        GConnectFlags{});
}

// Runs callback once the dialog has closed. The callback is owned by the signal connection.
template<typename F>
void adw_compat_dialog_on_closed(GtkWidget* dialog, F&& callback)
{
    using Callback = std::decay_t<F>;
    auto* const data = new Callback(std::forward<F>(callback));
    g_signal_connect_data(
        dialog,
        "closed",
        G_CALLBACK(+[](AdwDialog*, gpointer user_data) { (*static_cast<Callback*>(user_data))(); }),
        data,
        [](gpointer user_data, GClosure*) { delete static_cast<Callback*>(user_data); },
        GConnectFlags{});
}
