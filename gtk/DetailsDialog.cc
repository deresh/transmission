// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "Actions.h"
#include "AdwCompat.h"
#include "DetailsDialogImpl.h"
#include "FileList.h"
#include "GtkCompat.h"
#include "HigWorkarea.h" // GUI_PAD, GUI_PAD_BIG, GUI_PAD_SMALL
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

#include <libtransmission/file-utils.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/utils.h>
#include <libtransmission/values.h>
#include <libtransmission/web-utils.h>

#include <gdkmm/pixbuf.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <glibmm/quark.h>
#include <glibmm/ustring.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/cellrendererprogress.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/combobox.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/notebook.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/textbuffer.h>
#include <gtkmm/textview.h>
#include <gtkmm/tooltip.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treemodelfilter.h>
#include <gtkmm/treemodelsort.h>
#include <gtkmm/treerowreference.h>
#include <gtkmm/treeview.h>

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib> // abort()
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

using namespace std::literals;

using namespace tr::Values;

guint DetailsDialog::Impl::last_page_ = 0;

std::vector<tr_torrent*> DetailsDialog::Impl::getTorrents() const
{
    return core_->find_torrents(ids_);
}

/****
*****  DIALOG
****/

void DetailsDialog::Impl::refresh()
{
    auto const torrents = getTorrents();

    refreshInfo(torrents);
    refreshPeers(torrents);
    refreshTracker(torrents);
    refreshFiles(torrents);
    refreshOptions(torrents);

    if (torrents.empty())
    {
        adw_dialog_close(ADW_DIALOG(dialog_.gobj()));
    }
}

DetailsDialog::Impl::~Impl()
{
    periodic_refresh_tag_.disconnect();
    close_button_tag_.disconnect();
}

std::unique_ptr<DetailsDialog> DetailsDialog::create(Glib::RefPtr<Session> const& core)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("DetailsDialog.ui"));
    return std::unique_ptr<DetailsDialog>(gtr_get_widget_derived<DetailsDialog>(builder, "DetailsDialog", core));
}

DetailsDialog::DetailsDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : Gtk::Widget(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core))
{
}

DetailsDialog::~DetailsDialog() = default;

DetailsDialog::Impl::Impl(DetailsDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core)
    : dialog_(dialog)
    , core_(core)
    , builder_(builder)
    , honor_limits_check_(gtr_get_widget<Gtk::CheckButton>(builder, "honor_limits_check"))
    , up_limited_check_(gtr_get_widget<Gtk::CheckButton>(builder, "upload_limit_check"))
    , up_limit_sping_(gtr_get_widget<Gtk::SpinButton>(builder, "upload_limit_spin"))
    , down_limited_check_(gtr_get_widget<Gtk::CheckButton>(builder, "download_limit_check"))
    , down_limit_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "download_limit_spin"))
    , bandwidth_combo_(gtr_get_widget<Gtk::ComboBox>(builder, "priority_combo"))
    , ratio_combo_(gtr_get_widget<Gtk::ComboBox>(builder, "ratio_limit_combo"))
    , ratio_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "ratio_limit_spin"))
    , idle_combo_(gtr_get_widget<Gtk::ComboBox>(builder, "idle_limit_combo"))
    , idle_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "idle_limit_spin"))
    , max_peers_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "max_peers_spin"))
    , added_lb_(gtr_get_widget<Gtk::Label>(builder, "added_value_label"))
    , size_lb_(gtr_get_widget<Gtk::Label>(builder, "torrent_size_value_label"))
    , state_lb_(gtr_get_widget<Gtk::Label>(builder, "state_value_label"))
    , have_lb_(gtr_get_widget<Gtk::Label>(builder, "have_value_label"))
    , dl_lb_(gtr_get_widget<Gtk::Label>(builder, "downloaded_value_label"))
    , ul_lb_(gtr_get_widget<Gtk::Label>(builder, "uploaded_value_label"))
    , error_lb_(gtr_get_widget<Gtk::Label>(builder, "error_value_label"))
    , date_started_lb_(gtr_get_widget<Gtk::Label>(builder, "running_time_value_label"))
    , eta_lb_(gtr_get_widget<Gtk::Label>(builder, "remaining_time_value_label"))
    , last_activity_lb_(gtr_get_widget<Gtk::Label>(builder, "last_activity_value_label"))
    , hash_lb_(gtr_get_widget<Gtk::Label>(builder, "hash_value_label"))
    , privacy_lb_(gtr_get_widget<Gtk::Label>(builder, "privacy_value_label"))
    , origin_lb_(gtr_get_widget<Gtk::Label>(builder, "origin_value_label"))
    , destination_lb_(gtr_get_widget<Gtk::Label>(builder, "location_value_label"))
    , webseed_view_(gtr_get_widget<Gtk::ScrolledWindow>(builder, "webseeds_view_scroll"))
    , peer_view_(gtr_get_widget<Gtk::TreeView>(builder, "peers_view"))
    , more_peer_details_check_(gtr_get_widget<Gtk::CheckButton>(builder, "more_peer_details_check"))
    , add_tracker_button_(gtr_get_widget<Gtk::Button>(builder, "add_tracker_button"))
    , edit_trackers_button_(gtr_get_widget<Gtk::Button>(builder, "edit_tracker_button"))
    , remove_tracker_button_(gtr_get_widget<Gtk::Button>(builder, "remove_tracker_button"))
    , tracker_view_(gtr_get_widget<Gtk::TreeView>(builder, "trackers_view"))
    , scrape_check_(gtr_get_widget<Gtk::CheckButton>(builder, "more_tracker_details_check"))
    , all_check_(gtr_get_widget<Gtk::CheckButton>(builder, "backup_trackers_check"))
    , file_list_(gtr_get_widget_derived<FileList>(builder, "files_view_scroll", "files_view", core, 0))
    , file_label_(gtr_get_widget<Gtk::Label>(builder, "files_label"))
{
    /* return saved window size */
    auto const width = gtr_pref_int_get<int>(TR_KEY_details_window_width);
    auto const height = gtr_pref_int_get<int>(TR_KEY_details_window_height);
    adw_dialog_set_content_width(ADW_DIALOG(dialog_.gobj()), width);
    adw_dialog_set_content_height(ADW_DIALOG(dialog_.gobj()), height);

    close_button_tag_ = gtr_get_widget<Gtk::Button>(builder, "close_button")
                            ->signal_clicked()
                            .connect([this]() { adw_dialog_close(ADW_DIALOG(dialog_.gobj())); });

    info_page_init(builder);
    peer_page_init(builder);
    tracker_page_init(builder);
    options_page_init(builder);

    periodic_refresh_tag_ = Glib::signal_timeout().connect_seconds(
        [this]() { return refresh(), true; },
        SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);

    auto* const n = gtr_get_widget<Gtk::Notebook>(builder, "dialog_pages");
    n->set_current_page(static_cast<int>(last_page_));
    n->signal_switch_page().connect([](Gtk::Widget* /*page*/, guint page_number) { last_page_ = page_number; });
}

void DetailsDialog::set_torrents(std::vector<tr_torrent_id_t> const& ids)
{
    impl_->set_torrents(ids);
}

void DetailsDialog::refresh()
{
    impl_->refresh();
}

void DetailsDialog::Impl::set_torrents(std::vector<tr_torrent_id_t> const& ids)
{
    Glib::ustring title;
    auto const len = ids.size();

    ids_ = ids;

    if (len == 1)
    {
        int const id = ids.front();
        auto const* tor = core_->find_torrent(id);
        title = fmt::format(fmt::runtime(_("{torrent_name} Properties")), fmt::arg("torrent_name", tr_torrentName(tor)));
    }
    else
    {
        title = fmt::format(
            fmt::runtime(ngettext("Properties - {torrent_count:L} Torrent", "Properties - {torrent_count:L} Torrents", len)),
            fmt::arg("torrent_count", len));
    }

    adw_dialog_set_title(ADW_DIALOG(dialog_.gobj()), title.c_str());

    refresh();
}
