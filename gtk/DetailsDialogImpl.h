// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "DetailsDialog.h"
#include "FileList.h"
#include "Session.h"
#include <sigc++/connection.h>

#include <glibmm/quark.h>
#include <glibmm/refptr.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/combobox.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/textbuffer.h>
#include <gtkmm/tooltip.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treemodelfilter.h>
#include <gtkmm/treerowreference.h>
#include <gtkmm/treeview.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class DetailsDialog::Impl
{
public:
    Impl(DetailsDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

    void set_torrents(std::vector<tr_torrent_id_t> const& torrent_ids);
    void refresh();

private:
    void info_page_init(Glib::RefPtr<Gtk::Builder> const& builder);
    void peer_page_init(Glib::RefPtr<Gtk::Builder> const& builder);
    void tracker_page_init(Glib::RefPtr<Gtk::Builder> const& builder);
    void options_page_init(Glib::RefPtr<Gtk::Builder> const& builder);

    void on_details_window_size_allocated();

    bool onPeerViewQueryTooltip(int x, int y, bool keyboard_tip, Glib::RefPtr<Gtk::Tooltip> const& tooltip);
    void onMorePeerInfoToggled();

    bool trackerVisibleFunc(Gtk::TreeModel::const_iterator const& iter);
    void on_tracker_list_selection_changed();

    void on_tracker_list_add_button_clicked();
    void on_edit_trackers();
    void on_tracker_list_remove_button_clicked();
    void onScrapeToggled();
    void onBackupToggled();

    template<typename T>
    void torrent_set_field(tr_quark const key, T value)
    {
        auto params = tr_variant::Map{ 2U };
        params.try_emplace(key, std::forward<T>(value));
        params.try_emplace(TR_KEY_ids, Session::to_variant(ids_));
        core_->exec(TR_KEY_torrent_set, std::move(params));
    }

    void refreshInfo(std::vector<tr_torrent*> const& torrents);
    void refreshPeers(std::vector<tr_torrent*> const& torrents);
    void refreshTracker(std::vector<tr_torrent*> const& torrents);
    void refreshFiles(std::vector<tr_torrent*> const& torrents);
    void refreshOptions(std::vector<tr_torrent*> const& torrents);

    void refreshPeerList(std::vector<tr_torrent*> const& torrents);
    void refreshWebseedList(std::vector<tr_torrent*> const& torrents);

    tr_torrent_id_t tracker_list_get_current_torrent_id() const;
    tr_torrent* tracker_list_get_current_torrent() const;

    std::vector<tr_torrent*> getTorrents() const;

private:
    DetailsDialog& dialog_;
    Glib::RefPtr<Session> const core_;

    Gtk::CheckButton* honor_limits_check_ = nullptr;
    Gtk::CheckButton* up_limited_check_ = nullptr;
    Gtk::SpinButton* up_limit_sping_ = nullptr;
    Gtk::CheckButton* down_limited_check_ = nullptr;
    Gtk::SpinButton* down_limit_spin_ = nullptr;
    Gtk::ComboBox* bandwidth_combo_ = nullptr;

    Gtk::ComboBox* ratio_combo_ = nullptr;
    Gtk::SpinButton* ratio_spin_ = nullptr;
    Gtk::ComboBox* idle_combo_ = nullptr;
    Gtk::SpinButton* idle_spin_ = nullptr;
    Gtk::SpinButton* max_peers_spin_ = nullptr;

    sigc::connection honor_limits_check_tag_;
    sigc::connection up_limited_check_tag_;
    sigc::connection down_limited_check_tag_;
    sigc::connection down_limit_spin_tag_;
    sigc::connection up_limit_spin_tag_;
    sigc::connection bandwidth_combo_tag_;
    sigc::connection ratio_combo_tag_;
    sigc::connection ratio_spin_tag_;
    sigc::connection idle_combo_tag_;
    sigc::connection idle_spin_tag_;
    sigc::connection max_peers_spin_tag_;

    Gtk::Label* added_lb_ = nullptr;
    Gtk::Label* size_lb_ = nullptr;
    Gtk::Label* state_lb_ = nullptr;
    Gtk::Label* have_lb_ = nullptr;
    Gtk::Label* dl_lb_ = nullptr;
    Gtk::Label* ul_lb_ = nullptr;
    Gtk::Label* error_lb_ = nullptr;
    Gtk::Label* date_started_lb_ = nullptr;
    Gtk::Label* eta_lb_ = nullptr;
    Gtk::Label* last_activity_lb_ = nullptr;

    Gtk::Label* hash_lb_ = nullptr;
    Gtk::Label* privacy_lb_ = nullptr;
    Gtk::Label* origin_lb_ = nullptr;
    Gtk::Label* destination_lb_ = nullptr;
    Glib::RefPtr<Gtk::TextBuffer> comment_buffer_;

    std::unordered_map<std::string, Gtk::TreeRowReference> peer_hash_;
    std::unordered_map<std::string, Gtk::TreeRowReference> webseed_hash_;
    Glib::RefPtr<Gtk::ListStore> peer_store_;
    Glib::RefPtr<Gtk::ListStore> webseed_store_;
    Gtk::ScrolledWindow* webseed_view_ = nullptr;
    Gtk::TreeView* peer_view_ = nullptr;
    Gtk::CheckButton* more_peer_details_check_ = nullptr;

    Glib::RefPtr<Gtk::ListStore> tracker_store_;
    std::unordered_map<std::string, Gtk::TreeRowReference> tracker_hash_;
    Glib::RefPtr<Gtk::TreeModelFilter> trackers_filtered_;
    Gtk::Button* add_tracker_button_ = nullptr;
    Gtk::Button* edit_trackers_button_ = nullptr;
    Gtk::Button* remove_tracker_button_ = nullptr;
    Gtk::TreeView* tracker_view_ = nullptr;
    Gtk::CheckButton* scrape_check_ = nullptr;
    Gtk::CheckButton* all_check_ = nullptr;

    FileList* file_list_ = nullptr;
    Gtk::Label* file_label_ = nullptr;

    std::vector<tr_torrent_id_t> ids_;
    sigc::connection periodic_refresh_tag_;

    Glib::Quark const TORRENT_ID_KEY = Glib::Quark("tr-torrent-id-key");
    Glib::Quark const TEXT_BUFFER_KEY = Glib::Quark("tr-text-buffer-key");
    Glib::Quark const URL_ENTRY_KEY = Glib::Quark("tr-url-entry-key");

    static guint last_page_;
};
