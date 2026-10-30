// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cairommconfig.h>
#include <glibmmconfig.h>
#include <gtkmmconfig.h>
#include <pangommconfig.h>

#include <cstddef>

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

#ifndef GTKMM_CHECK_VERSION
#define GTKMM_CHECK_VERSION(major, minor, micro) \
    (GTKMM_MAJOR_VERSION > (major) || (GTKMM_MAJOR_VERSION == (major) && GTKMM_MINOR_VERSION > (minor)) || \
     (GTKMM_MAJOR_VERSION == (major) && GTKMM_MINOR_VERSION == (minor) && GTKMM_MICRO_VERSION >= (micro)))
#endif

#ifndef GLIBMM_CHECK_VERSION
#define GLIBMM_CHECK_VERSION(major, minor, micro) \
    (GLIBMM_MAJOR_VERSION > (major) || (GLIBMM_MAJOR_VERSION == (major) && GLIBMM_MINOR_VERSION > (minor)) || \
     (GLIBMM_MAJOR_VERSION == (major) && GLIBMM_MINOR_VERSION == (minor) && GLIBMM_MICRO_VERSION >= (micro)))
#endif

#ifndef CAIROMM_CHECK_VERSION
#define CAIROMM_CHECK_VERSION(major, minor, micro) \
    (CAIROMM_MAJOR_VERSION > (major) || (CAIROMM_MAJOR_VERSION == (major) && CAIROMM_MINOR_VERSION > (minor)) || \
     (CAIROMM_MAJOR_VERSION == (major) && CAIROMM_MINOR_VERSION == (minor) && CAIROMM_MICRO_VERSION >= (micro)))
#endif

#ifndef PANGOMM_CHECK_VERSION
#define PANGOMM_CHECK_VERSION(major, minor, micro) \
    (PANGOMM_MAJOR_VERSION > (major) || (PANGOMM_MAJOR_VERSION == (major) && PANGOMM_MINOR_VERSION > (minor)) || \
     (PANGOMM_MAJOR_VERSION == (major) && PANGOMM_MINOR_VERSION == (minor) && PANGOMM_MICRO_VERSION >= (micro)))
#endif

#if CAIROMM_CHECK_VERSION(1, 16, 0)
#define IF_CAIROMM1_16(ThenValue, ElseValue) ThenValue
#else
#define IF_CAIROMM1_16(ThenValue, ElseValue) ElseValue
#endif

#if PANGOMM_CHECK_VERSION(2, 48, 0)
#define IF_PANGOMM2_48(ThenValue, ElseValue) ThenValue
#else
#define IF_PANGOMM2_48(ThenValue, ElseValue) ElseValue
#endif

#define TR_GTK_ALIGN(Code) Gtk::Align::Code
#define TR_GTK_BUTTONS_TYPE(Code) Gtk::ButtonsType::Code
#define TR_GTK_CELL_RENDERER_STATE(Code) Gtk::CellRendererState::Code
#define TR_GTK_FILE_CHOOSER_ACTION(Code) Gtk::FileChooser::Action::Code
#define TR_GTK_MESSAGE_TYPE(Code) Gtk::MessageType::Code
#define TR_GTK_ORIENTATION(Code) Gtk::Orientation::Code
#define TR_GTK_POLICY_TYPE(Code) Gtk::PolicyType::Code
#define TR_GTK_RESPONSE_TYPE(Code) Gtk::ResponseType::Code
#define TR_GTK_SELECTION_MODE(Code) Gtk::SelectionMode::Code
#define TR_GTK_SORT_TYPE(Code) Gtk::SortType::Code
#define TR_GTK_STATE_FLAGS(Code) Gtk::StateFlags::Code
#define TR_GTK_TREE_MODEL_FLAGS(Code) Gtk::TreeModel::Flags::Code
#define TR_GTK_TREE_VIEW_COLUMN_SIZING(Code) Gtk::TreeViewColumn::Sizing::Code

#define TR_GTK_TREE_MODEL_CHILD_ITER(Obj) (Obj).get_iter()
#define TR_GTK_WIDGET_GET_ROOT(Obj) (Obj).get_root()

#define TR_GDK_COLORSPACE(Code) Gdk::Colorspace::Code
#define TR_GDK_EVENT_TYPE(Code) Gdk::Event::Type::Code
#define TR_GDK_DRAG_ACTION(Code) Gdk::DragAction::Code
#define TR_GDK_MODIFIED_TYPE(Code) Gdk::ModifierType::Code

#define TR_GLIB_FILE_TEST(Code) Glib::FileTest::Code
#define TR_GLIB_NODE_TREE_TRAVERSE_FLAGS(Cls, Code) Cls::TraverseFlags::Code
#define TR_GLIB_PARAM_FLAGS(Code) Glib::ParamFlags::Code
#define TR_GLIB_SPAWN_FLAGS(Code) Glib::SpawnFlags::Code
#define TR_GLIB_USER_DIRECTORY(Code) Glib::UserDirectory::Code

#define TR_GLIB_EXCEPTION_WHAT(Obj) (Obj).what()

#define TR_GIO_APP_INFO_CREATE_FLAGS(Code) Gio::AppInfo::CreateFlags::Code
#define TR_GIO_APPLICATION_FLAGS(Code) Gio::Application::Flags::Code
#define TR_GIO_DBUS_BUS_TYPE(Code) Gio::DBus::BusType::Code
#define TR_GIO_DBUS_PROXY_FLAGS(Code) Gio::DBus::ProxyFlags::Code
#define TR_GIO_FILE_MONITOR_EVENT(Code) Gio::FileMonitor::Event::Code

#define TR_CAIRO_SURFACE_FORMAT(Code) IF_CAIROMM1_16(Cairo::Surface::Format::Code, Cairo::FORMAT_##Code)
#define TR_CAIRO_CONTEXT_OPERATOR(Code) IF_CAIROMM1_16(Cairo::Context::Operator::Code, Cairo::OPERATOR_##Code)

#define TR_PANGO_ALIGNMENT(Code) IF_PANGOMM2_48(Pango::Alignment::Code, Pango::ALIGN_##Code)
#define TR_PANGO_ELLIPSIZE_MODE(Code) IF_PANGOMM2_48(Pango::EllipsizeMode::Code, Pango::ELLIPSIZE_##Code)
#define TR_PANGO_WEIGHT(Code) IF_PANGOMM2_48(Pango::Weight::Code, Pango::WEIGHT_##Code)

// NOLINTEND(cppcoreguidelines-macro-usage)

namespace Glib
{

} // namespace Glib
