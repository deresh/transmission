// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "Dialogs.h"

#include "AdwCompat.h"
#include "GtkCompat.h"
#include "Session.h"
#include "Utils.h"

#include <glibmm/i18n.h>
#include <glibmm/ustring.h>

#include <fmt/format.h>

#include <memory>
#include <string_view>
#include <vector>

/***
****
***/

void gtr_confirm_remove(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids,
    bool delete_files)
{
    auto const count = torrent_ids.size();
    if (count == 0)
    {
        return;
    }

    size_t connected = 0;
    size_t incomplete = 0;
    // TODO(c++20) remove `torrents` local when tr_torrentStat() takes a span
    auto const torrents = core->find_torrents(torrent_ids);
    for (auto const& stat : tr_torrentStat(std::data(torrents), std::size(torrents)))
    {
        if (stat.left_until_done != 0)
        {
            ++incomplete;
        }

        if (stat.peers_connected != 0)
        {
            ++connected;
        }
    }

    auto const primary_text = fmt::format(
        fmt::runtime(
            !delete_files ? ngettext("Remove torrent?", "Remove {count:L} torrents?", count) :
                            ngettext(
                                "Delete this torrent's downloaded files?",
                                "Delete these {count:L} torrents' downloaded files?",
                                count)),
        fmt::arg("count", count));

    Glib::ustring secondary_text;
    if (incomplete == 0 && connected == 0)
    {
        secondary_text = ngettext(
            "Once removed, continuing the transfer will require the torrent file or magnet link.",
            "Once removed, continuing the transfers will require the torrent files or magnet links.",
            count);
    }
    else if (count == incomplete)
    {
        secondary_text = ngettext(
            "This torrent has not finished downloading.",
            "These torrents have not finished downloading.",
            count);
    }
    else if (count == connected)
    {
        secondary_text = ngettext("This torrent is connected to peers.", "These torrents are connected to peers.", count);
    }
    else
    {
        if (connected != 0)
        {
            secondary_text += ngettext(
                "One of these torrents is connected to peers.",
                "Some of these torrents are connected to peers.",
                connected);
        }

        if (connected != 0 && incomplete != 0)
        {
            secondary_text += "\n";
        }

        if (incomplete != 0)
        {
            secondary_text += ngettext(
                "One of these torrents has not finished downloading.",
                "Some of these torrents have not finished downloading.",
                incomplete);
        }
    }

    auto* const dialog = adw_compat_alert_dialog_new(primary_text.c_str(), secondary_text.c_str());
    adw_compat_alert_add_response(dialog, "cancel", _("Cancel"));
    adw_compat_alert_add_response(dialog, "accept", delete_files ? _("Delete") : _("Remove"), ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
    adw_compat_alert_on_response(
        dialog,
        [core, torrent_ids, delete_files](char const* response)
        {
            if (std::string_view{ response } == "accept")
            {
                for (auto const id : torrent_ids)
                {
                    core->remove_torrent(id, delete_files);
                }
            }
        });
    adw_compat_dialog_present(dialog, GTK_WIDGET(parent.gobj()));
}
