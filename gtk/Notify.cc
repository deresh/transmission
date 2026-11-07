// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "Notify.h"

#include "Prefs.h"
#include "Session.h"
#include "Utils.h"

#include <giomm/application.h>
#include <giomm/notification.h>
#include <glibmm/i18n.h>
#include <glibmm/refptr.h>
#include <glibmm/spawn.h>
#include <glibmm/ustring.h>
#include <glibmm/variant.h>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <optional>
#include <string_view>

namespace
{

// The action is the detailed action name, e.g. "app.start-now", that the shell activates on click.
struct NotificationButton
{
    Glib::ustring label;
    Glib::ustring action;
};

// Sends through the running Gio::Application, so the shell shows it under the
// application ID. A notification with the same ID replaces the earlier one.
// The button, if any, activates its app action with tor_id as the target.
void send_torrent_notification(
    Glib::ustring const& id,
    Glib::ustring const& title,
    std::string const& torrent_name,
    std::string_view category,
    std::optional<NotificationButton> const& button,
    tr_torrent_id_t tor_id)
{
    auto const app = Gio::Application::get_default();
    if (!app)
    {
        return;
    }

    auto const notification = Gio::Notification::create(title);
    notification->set_body(torrent_name);

    if (!category.empty())
    {
        notification->set_category(std::string(category));
    }

    if (button)
    {
        notification->add_button_variant(
            button->label,
            button->action,
            Glib::Variant<gint32>::create(static_cast<gint32>(tor_id)));
    }

    app->send_notification(id, notification);
}

} // namespace

void gtr_notify_torrent_completed(Glib::RefPtr<Session> const& core, tr_torrent_id_t tor_id)
{
    if (gtr_pref_flag_get(TR_KEY_torrent_complete_sound_enabled))
    {
        auto const argv = gtr_pref_strv_get(TR_KEY_torrent_complete_sound_command);

        try
        {
            Glib::spawn_async({}, argv, Glib::SpawnFlags::SEARCH_PATH);
        }
        catch (Glib::SpawnError const& e)
        {
            gtr_warning(
                fmt::format(
                    fmt::runtime(_("Couldn't spawn async process \"'{command}'\": {error} ({error_code})")),
                    fmt::arg("command", fmt::join(argv, "' '")),
                    fmt::arg("error", e.what()),
                    fmt::arg("error_code", static_cast<int>(e.code()))));
        }
    }

    if (!gtr_pref_flag_get(TR_KEY_torrent_complete_notification_enabled))
    {
        return;
    }

    auto const* const tor = core->find_torrent(tor_id);
    if (tor == nullptr)
    {
        return;
    }

    auto const id = fmt::format("torrent-complete-{}", tr_torrentView(tor).hash_string);
    auto const button = tr_torrentFileCount(tor) == 1 ?
        NotificationButton{ .label = _("Open File"), .action = "app.open-file" } :
        NotificationButton{ .label = _("Open Folder"), .action = "app.open-folder" };
    send_torrent_notification(id, _("Torrent Complete"), tr_torrentName(tor), "transfer.complete", button, tor_id);
}

void gtr_notify_torrent_added(Glib::RefPtr<Session> const& core, tr_torrent_id_t tor_id)
{
    if (!gtr_pref_flag_get(TR_KEY_torrent_added_notification_enabled))
    {
        return;
    }

    auto const* const tor = core->find_torrent(tor_id);
    if (tor == nullptr)
    {
        return;
    }

    auto const id = fmt::format("torrent-added-{}", tr_torrentView(tor).hash_string);
    send_torrent_notification(
        id,
        _("Torrent Added"),
        tr_torrentName(tor),
        {},
        NotificationButton{ .label = _("Start Now"), .action = "app.start-now" },
        tor_id);
}
