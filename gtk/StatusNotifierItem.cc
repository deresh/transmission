// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "StatusNotifierItem.h"

#include <utility>

#include <gio/gio.h>
#include <glib.h>

namespace
{

char const* const WatcherBusName = "org.kde.StatusNotifierWatcher";
char const* const WatcherObjectPath = "/StatusNotifierWatcher";
char const* const WatcherInterface = "org.kde.StatusNotifierWatcher";

char const* const ItemObjectPath = "/StatusNotifierItem";
char const* const ItemInterface = "org.kde.StatusNotifierItem";
char const* const ItemId = "com.transmissionbt.TransmissionAdw";
char const* const ItemTitle = "Transmission";
// Installed by the GTK target. Watchers look icons up in the system theme,
// not in the application's resource bundle.
char const* const ItemIconName = "transmission-gtk";

char const* const MenuObjectPath = "/MenuBar";
char const* const MenuInterface = "com.canonical.dbusmenu";

// Menu item ids, as the dbusmenu spec numbers them. The root is 0.
gint32 const MenuIdRoot = 0;
gint32 const MenuIdShow = 1;
gint32 const MenuIdSeparator = 2;
gint32 const MenuIdQuit = 3;

// Only the properties and methods that watchers read or call. ItemIsMenu is
// false: Activate toggles the window, and the watcher shows the dbusmenu at
// Menu on right click. ContextMenu is accepted and ignored.
char const* const ItemIntrospectionXml = R"XML(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <method name="Activate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="SecondaryActivate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="ContextMenu">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
  </interface>
</node>
)XML";

// The dbusmenu interface, as used by AppIndicator and KDE. Only the
// subset that the fixed menu needs is implemented.
char const* const MenuIntrospectionXml = R"XML(
<node>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout">
      <arg name="parentId" type="i" direction="in"/>
      <arg name="recursionDepth" type="i" direction="in"/>
      <arg name="propertyNames" type="as" direction="in"/>
      <arg name="revision" type="u" direction="out"/>
      <arg name="layout" type="(ia{sv}av)" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg name="ids" type="ai" direction="in"/>
      <arg name="propertyNames" type="as" direction="in"/>
      <arg name="properties" type="a(ia{sv})" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg name="id" type="i" direction="in"/>
      <arg name="name" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="Event">
      <arg name="id" type="i" direction="in"/>
      <arg name="eventId" type="s" direction="in"/>
      <arg name="data" type="v" direction="in"/>
      <arg name="timestamp" type="u" direction="in"/>
    </method>
    <method name="EventGroup">
      <arg name="events" type="a(isvu)" direction="in"/>
      <arg name="idErrors" type="ai" direction="out"/>
    </method>
    <method name="AboutToShow">
      <arg name="id" type="i" direction="in"/>
      <arg name="needUpdate" type="b" direction="out"/>
    </method>
    <method name="AboutToShowGroup">
      <arg name="ids" type="ai" direction="in"/>
      <arg name="updatesNeeded" type="ai" direction="out"/>
      <arg name="idErrors" type="ai" direction="out"/>
    </method>
    <signal name="LayoutUpdated">
      <arg name="revision" type="u"/>
      <arg name="parent" type="i"/>
    </signal>
  </interface>
</node>
)XML";

// Returns the a{sv} properties of a menu item (a new reference).
// Unknown ids get an empty dictionary.
GVariant* menu_item_properties(gint32 id)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

    switch (id)
    {
    case MenuIdShow:
        g_variant_builder_add(&builder, "{sv}", "label", g_variant_new_string("Show Transmission"));
        g_variant_builder_add(&builder, "{sv}", "enabled", g_variant_new_boolean(TRUE));
        g_variant_builder_add(&builder, "{sv}", "type", g_variant_new_string("standard"));
        break;

    case MenuIdSeparator:
        g_variant_builder_add(&builder, "{sv}", "type", g_variant_new_string("separator"));
        break;

    case MenuIdQuit:
        g_variant_builder_add(&builder, "{sv}", "label", g_variant_new_string("Quit"));
        g_variant_builder_add(&builder, "{sv}", "enabled", g_variant_new_boolean(TRUE));
        g_variant_builder_add(&builder, "{sv}", "type", g_variant_new_string("standard"));
        break;

    default:
        break;
    }

    return g_variant_builder_end(&builder);
}

bool is_menu_item(gint32 id)
{
    return id == MenuIdRoot || id == MenuIdShow || id == MenuIdSeparator || id == MenuIdQuit;
}

// Returns an empty "av" (a new reference), the children of a leaf.
GVariant* no_children()
{
    return g_variant_new_array(G_VARIANT_TYPE_VARIANT, nullptr, 0);
}

// Returns the (ia{sv}av) node for one item. The children are consumed.
GVariant* menu_node(gint32 id, GVariant* children)
{
    return g_variant_new("(i@a{sv}@av)", id, menu_item_properties(id), children);
}

// Returns the root node with its three children, as a new reference.
// Each child is wrapped in a variant, as the dbusmenu spec requires.
GVariant* menu_root_with_children()
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("av"));

    for (gint32 const id : { MenuIdShow, MenuIdSeparator, MenuIdQuit })
    {
        g_variant_builder_add(&builder, "v", menu_node(id, no_children()));
    }

    return menu_node(MenuIdRoot, g_variant_builder_end(&builder));
}

GVariant* empty_ai()
{
    return g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0);
}

} // namespace

class StatusNotifierItem::Impl
{
public:
    Impl(ActivateFunc activate, QuitFunc quit, ExportedFunc exported);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

    [[nodiscard]] bool is_exported() const;

private:
    static void on_watcher_appeared(GDBusConnection* connection, gchar const* name, gchar const* owner, gpointer user_data);
    static void on_watcher_vanished(GDBusConnection* connection, gchar const* name, gpointer user_data);
    static void on_registered(GObject* source, GAsyncResult* result, gpointer user_data);

    void set_exported(bool exported);

    static void handle_method_call(
        GDBusConnection* connection,
        gchar const* sender,
        gchar const* object_path,
        gchar const* interface_name,
        gchar const* method_name,
        GVariant* parameters,
        GDBusMethodInvocation* invocation,
        gpointer user_data);
    static GVariant* handle_get_property(
        GDBusConnection* connection,
        gchar const* sender,
        gchar const* object_path,
        gchar const* interface_name,
        gchar const* property_name,
        GError** error,
        gpointer user_data);

    void register_with_watcher(GDBusConnection* connection);

    static GDBusInterfaceVTable const vtable;
    static GDBusInterfaceVTable const menu_vtable;

    static void handle_menu_method_call(
        GDBusConnection* connection,
        gchar const* sender,
        gchar const* object_path,
        gchar const* interface_name,
        gchar const* method_name,
        GVariant* parameters,
        GDBusMethodInvocation* invocation,
        gpointer user_data);
    static GVariant* handle_menu_get_property(
        GDBusConnection* connection,
        gchar const* sender,
        gchar const* object_path,
        gchar const* interface_name,
        gchar const* property_name,
        GError** error,
        gpointer user_data);

    // Runs the action of a dbusmenu event. Only "clicked" acts.
    void handle_menu_event(gint32 id, char const* event_id);

    ActivateFunc activate_;
    QuitFunc quit_;
    ExportedFunc exported_func_;
    bool exported_ = false;
    GCancellable* cancellable_ = nullptr;
    GDBusNodeInfo* node_info_ = nullptr;
    GDBusInterfaceInfo* interface_info_ = nullptr;
    GDBusNodeInfo* menu_node_info_ = nullptr;
    GDBusInterfaceInfo* menu_interface_info_ = nullptr;
    guint watch_id_ = 0;
    guint object_id_ = 0;
    guint menu_id_ = 0;
    GDBusConnection* connection_ = nullptr;
};

GDBusInterfaceVTable const StatusNotifierItem::Impl::vtable = {
    .method_call = &handle_method_call,
    .get_property = &handle_get_property,
    .set_property = nullptr,
    .padding = {},
};

GDBusInterfaceVTable const StatusNotifierItem::Impl::menu_vtable = {
    .method_call = &handle_menu_method_call,
    .get_property = &handle_menu_get_property,
    .set_property = nullptr,
    .padding = {},
};

StatusNotifierItem::Impl::Impl(ActivateFunc activate, QuitFunc quit, ExportedFunc exported)
    : activate_(std::move(activate))
    , quit_(std::move(quit))
    , exported_func_(std::move(exported))
    , cancellable_(g_cancellable_new())
{
    node_info_ = g_dbus_node_info_new_for_xml(ItemIntrospectionXml, nullptr);
    menu_node_info_ = g_dbus_node_info_new_for_xml(MenuIntrospectionXml, nullptr);
    if (node_info_ == nullptr || menu_node_info_ == nullptr)
    {
        g_warning("Couldn't parse the StatusNotifierItem introspection data");
        return;
    }

    interface_info_ = g_dbus_node_info_lookup_interface(node_info_, ItemInterface);
    menu_interface_info_ = g_dbus_node_info_lookup_interface(menu_node_info_, MenuInterface);

    // The item is exported only once a watcher appears on the session bus.
    watch_id_ = g_bus_watch_name(
        G_BUS_TYPE_SESSION,
        WatcherBusName,
        G_BUS_NAME_WATCHER_FLAGS_NONE,
        &on_watcher_appeared,
        &on_watcher_vanished,
        this,
        nullptr);
}

StatusNotifierItem::Impl::~Impl()
{
    if (watch_id_ != 0)
    {
        g_bus_unwatch_name(watch_id_);
    }

    // Cancelling makes a pending registration reply return without touching
    // this object. The reply callback still runs, so it must not dereference user_data.
    g_cancellable_cancel(cancellable_);
    g_object_unref(cancellable_);

    if (connection_ != nullptr)
    {
        if (menu_id_ != 0)
        {
            g_dbus_connection_unregister_object(connection_, menu_id_);
        }

        if (object_id_ != 0)
        {
            g_dbus_connection_unregister_object(connection_, object_id_);
        }

        g_object_unref(connection_);
    }

    if (menu_node_info_ != nullptr)
    {
        g_dbus_node_info_unref(menu_node_info_);
    }

    if (node_info_ != nullptr)
    {
        g_dbus_node_info_unref(node_info_);
    }
}

bool StatusNotifierItem::Impl::is_exported() const
{
    return exported_;
}

void StatusNotifierItem::Impl::set_exported(bool exported)
{
    exported_ = exported;

    if (exported_func_)
    {
        exported_func_(exported);
    }
}

void StatusNotifierItem::Impl::on_watcher_appeared(
    GDBusConnection* connection,
    gchar const* /*name*/,
    gchar const* /*owner*/,
    gpointer user_data)
{
    auto* const self = static_cast<Impl*>(user_data);

    if (self->connection_ == nullptr)
    {
        GError* error = nullptr;
        self->object_id_ = g_dbus_connection_register_object(
            connection,
            ItemObjectPath,
            self->interface_info_,
            &vtable,
            self,
            nullptr,
            &error);
        if (self->object_id_ == 0)
        {
            g_warning("Couldn't export the StatusNotifierItem: %s", error->message);
            g_error_free(error);
            self->set_exported(false);
            return;
        }

        self->menu_id_ = g_dbus_connection_register_object(
            connection,
            MenuObjectPath,
            self->menu_interface_info_,
            &menu_vtable,
            self,
            nullptr,
            &error);
        if (self->menu_id_ == 0)
        {
            g_warning("Couldn't export the tray menu: %s", error->message);
            g_error_free(error);
            g_dbus_connection_unregister_object(connection, self->object_id_);
            self->object_id_ = 0;
            self->set_exported(false);
            return;
        }

        self->connection_ = static_cast<GDBusConnection*>(g_object_ref(connection));
    }

    self->register_with_watcher(connection);
}

void StatusNotifierItem::Impl::on_watcher_vanished(GDBusConnection* /*connection*/, gchar const* /*name*/, gpointer user_data)
{
    // Also runs once at startup when no watcher owns the name.
    g_debug("%s is not on the session bus, so no tray icon is shown", WatcherBusName);

    static_cast<Impl*>(user_data)->set_exported(false);
}

// Registers by the connection's unique name. The watcher then calls
// ItemObjectPath on that unique name. The result comes back asynchronously
// through on_registered().
void StatusNotifierItem::Impl::register_with_watcher(GDBusConnection* connection)
{
    g_dbus_connection_call(
        connection,
        WatcherBusName,
        WatcherObjectPath,
        WatcherInterface,
        "RegisterStatusNotifierItem",
        g_variant_new("(s)", g_dbus_connection_get_unique_name(connection)),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        cancellable_,
        &on_registered,
        this);
}

void StatusNotifierItem::Impl::on_registered(GObject* source, GAsyncResult* result, gpointer user_data)
{
    GError* error = nullptr;
    GVariant* const reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);

    if (error != nullptr && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) != FALSE)
    {
        // The item was destroyed. user_data is no longer valid.
        g_error_free(error);
        return;
    }

    if (reply != nullptr)
    {
        g_variant_unref(reply);
    }

    auto* const self = static_cast<Impl*>(user_data);

    if (error != nullptr)
    {
        g_debug("Couldn't register the StatusNotifierItem with the watcher: %s", error->message);
        g_error_free(error);
    }

    self->set_exported(reply != nullptr);
}

void StatusNotifierItem::Impl::handle_method_call(
    GDBusConnection* /*connection*/,
    gchar const* /*sender*/,
    gchar const* /*object_path*/,
    gchar const* /*interface_name*/,
    gchar const* method_name,
    GVariant* /*parameters*/,
    GDBusMethodInvocation* invocation,
    gpointer user_data)
{
    auto* const self = static_cast<Impl*>(user_data);

    if (g_strcmp0(method_name, "Activate") == 0 && self->activate_)
    {
        self->activate_();
    }

    // SecondaryActivate and ContextMenu are accepted and do nothing.
    g_dbus_method_invocation_return_value(invocation, nullptr);
}

GVariant* StatusNotifierItem::Impl::handle_get_property(
    GDBusConnection* /*connection*/,
    gchar const* /*sender*/,
    gchar const* /*object_path*/,
    gchar const* /*interface_name*/,
    gchar const* property_name,
    GError** error,
    gpointer /*user_data*/)
{
    if (g_strcmp0(property_name, "Category") == 0)
    {
        return g_variant_new_string("ApplicationStatus");
    }
    if (g_strcmp0(property_name, "Id") == 0)
    {
        return g_variant_new_string(ItemId);
    }
    if (g_strcmp0(property_name, "Title") == 0)
    {
        return g_variant_new_string(ItemTitle);
    }
    if (g_strcmp0(property_name, "Status") == 0)
    {
        return g_variant_new_string("Active");
    }
    if (g_strcmp0(property_name, "IconName") == 0)
    {
        return g_variant_new_string(ItemIconName);
    }
    if (g_strcmp0(property_name, "ItemIsMenu") == 0)
    {
        return g_variant_new_boolean(FALSE);
    }
    if (g_strcmp0(property_name, "Menu") == 0)
    {
        return g_variant_new_object_path(MenuObjectPath);
    }

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property_name);
    return nullptr;
}

void StatusNotifierItem::Impl::handle_menu_event(gint32 id, char const* event_id)
{
    if (g_strcmp0(event_id, "clicked") != 0)
    {
        return;
    }

    // Must stay the last use of this object: a callback may destroy it.
    if (id == MenuIdShow && activate_)
    {
        activate_();
    }
    else if (id == MenuIdQuit && quit_)
    {
        quit_();
    }
}

void StatusNotifierItem::Impl::handle_menu_method_call(
    GDBusConnection* /*connection*/,
    gchar const* /*sender*/,
    gchar const* /*object_path*/,
    gchar const* /*interface_name*/,
    gchar const* method_name,
    GVariant* parameters,
    GDBusMethodInvocation* invocation,
    gpointer user_data)
{
    auto* const self = static_cast<Impl*>(user_data);

    if (g_strcmp0(method_name, "GetLayout") == 0)
    {
        gint32 parent_id = MenuIdRoot;
        g_variant_get_child(parameters, 0, "i", &parent_id);

        // The fixed layout: the root with its children. Any other item is a leaf.
        GVariant* layout = nullptr;
        if (parent_id == MenuIdRoot)
        {
            layout = menu_root_with_children();
        }
        else if (is_menu_item(parent_id))
        {
            layout = menu_node(parent_id, no_children());
        }
        else
        {
            layout = menu_node(MenuIdRoot, no_children());
        }

        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u@(ia{sv}av))", 1U, layout));
        return;
    }

    if (g_strcmp0(method_name, "GetGroupProperties") == 0)
    {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ia{sv})"));

        GVariant* ids = g_variant_get_child_value(parameters, 0);
        GVariantIter iter;
        g_variant_iter_init(&iter, ids);
        gint32 id = 0;
        while (g_variant_iter_next(&iter, "i", &id))
        {
            if (is_menu_item(id))
            {
                g_variant_builder_add(&builder, "(i@a{sv})", id, menu_item_properties(id));
            }
        }
        g_variant_unref(ids);

        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&builder)));
        return;
    }

    if (g_strcmp0(method_name, "GetProperty") == 0)
    {
        gint32 id = MenuIdRoot;
        gchar const* name = nullptr;
        g_variant_get(parameters, "(i&s)", &id, &name);

        GVariant* props = menu_item_properties(id);
        GVariant* value = g_variant_lookup_value(props, name, nullptr);
        g_variant_unref(props);
        if (value == nullptr)
        {
            value = g_variant_new_string("");
        }

        g_dbus_method_invocation_return_value(invocation, g_variant_new("(v)", value));
        return;
    }

    if (g_strcmp0(method_name, "Event") == 0)
    {
        gint32 id = MenuIdRoot;
        gchar* event_id = nullptr;
        GVariant* data = nullptr;
        guint32 timestamp = 0;
        g_variant_get(parameters, "(isvu)", &id, &event_id, &data, &timestamp);
        g_variant_unref(data);

        // Answer first, so the reply does not wait for the action.
        g_dbus_method_invocation_return_value(invocation, nullptr);
        self->handle_menu_event(id, event_id);
        g_free(event_id);
        return;
    }

    if (g_strcmp0(method_name, "EventGroup") == 0)
    {
        GVariant* events = g_variant_get_child_value(parameters, 0);
        GVariantIter iter;
        g_variant_iter_init(&iter, events);
        gint32 id = MenuIdRoot;
        gchar* event_id = nullptr;
        GVariant* data = nullptr;
        guint32 timestamp = 0;
        while (g_variant_iter_next(&iter, "(isvu)", &id, &event_id, &data, &timestamp))
        {
            g_variant_unref(data);
            self->handle_menu_event(id, event_id);
            g_free(event_id);
        }
        g_variant_unref(events);

        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai)", empty_ai()));
        return;
    }

    if (g_strcmp0(method_name, "AboutToShow") == 0)
    {
        // The menu never changes, so nothing needs a refresh.
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        return;
    }

    if (g_strcmp0(method_name, "AboutToShowGroup") == 0)
    {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai@ai)", empty_ai(), empty_ai()));
        return;
    }

    g_dbus_method_invocation_return_error(
        invocation,
        G_DBUS_ERROR,
        G_DBUS_ERROR_UNKNOWN_METHOD,
        "Unknown dbusmenu method %s",
        method_name);
}

GVariant* StatusNotifierItem::Impl::handle_menu_get_property(
    GDBusConnection* /*connection*/,
    gchar const* /*sender*/,
    gchar const* /*object_path*/,
    gchar const* /*interface_name*/,
    gchar const* property_name,
    GError** error,
    gpointer /*user_data*/)
{
    if (g_strcmp0(property_name, "Version") == 0)
    {
        return g_variant_new_uint32(3);
    }
    if (g_strcmp0(property_name, "TextDirection") == 0)
    {
        return g_variant_new_string("ltr");
    }
    if (g_strcmp0(property_name, "Status") == 0)
    {
        return g_variant_new_string("normal");
    }
    if (g_strcmp0(property_name, "IconThemePath") == 0)
    {
        return g_variant_new_strv(nullptr, 0);
    }

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property_name);
    return nullptr;
}

StatusNotifierItem::StatusNotifierItem(ActivateFunc activate, QuitFunc quit, ExportedFunc exported)
    : impl_(std::make_unique<Impl>(std::move(activate), std::move(quit), std::move(exported)))
{
}

StatusNotifierItem::~StatusNotifierItem() = default;

bool StatusNotifierItem::is_exported() const
{
    return impl_->is_exported();
}
