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

// Only the properties and methods that watchers read or call. There is no
// dbusmenu: ItemIsMenu is false, so ContextMenu has nothing to show.
char const* const ItemIntrospectionXml = R"XML(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
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

} // namespace

class StatusNotifierItem::Impl
{
public:
    Impl(ActivateFunc activate, ExportedFunc exported);
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

    ActivateFunc activate_;
    ExportedFunc exported_func_;
    bool exported_ = false;
    GCancellable* cancellable_ = nullptr;
    GDBusNodeInfo* node_info_ = nullptr;
    GDBusInterfaceInfo* interface_info_ = nullptr;
    guint watch_id_ = 0;
    guint object_id_ = 0;
    GDBusConnection* connection_ = nullptr;
};

GDBusInterfaceVTable const StatusNotifierItem::Impl::vtable = {
    .method_call = &handle_method_call,
    .get_property = &handle_get_property,
    .set_property = nullptr,
    .padding = {},
};

StatusNotifierItem::Impl::Impl(ActivateFunc activate, ExportedFunc exported)
    : activate_(std::move(activate))
    , exported_func_(std::move(exported))
    , cancellable_(g_cancellable_new())
{
    node_info_ = g_dbus_node_info_new_for_xml(ItemIntrospectionXml, nullptr);
    if (node_info_ == nullptr)
    {
        g_warning("Couldn't parse the StatusNotifierItem introspection data");
        return;
    }

    interface_info_ = g_dbus_node_info_lookup_interface(node_info_, ItemInterface);

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
        if (object_id_ != 0)
        {
            g_dbus_connection_unregister_object(connection_, object_id_);
        }

        g_object_unref(connection_);
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

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property_name);
    return nullptr;
}

StatusNotifierItem::StatusNotifierItem(ActivateFunc activate, ExportedFunc exported)
    : impl_(std::make_unique<Impl>(std::move(activate), std::move(exported)))
{
}

StatusNotifierItem::~StatusNotifierItem() = default;

bool StatusNotifierItem::is_exported() const
{
    return impl_->is_exported();
}
