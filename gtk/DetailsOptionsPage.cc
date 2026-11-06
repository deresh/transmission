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

/****
*****
*****  OPTIONS TAB
*****
****/

namespace
{

void set_togglebutton_if_different(Gtk::CheckButton* toggle, sigc::connection& tag, bool value)
{
    bool const currentValue = toggle->get_active();

    if (currentValue != value)
    {
        tag.block();
        toggle->set_active(value);
        tag.unblock();
    }
}

void set_int_spin_if_different(Gtk::SpinButton* spin, sigc::connection& tag, int value)
{
    int const currentValue = spin->get_value_as_int();

    if (currentValue != value)
    {
        tag.block();
        spin->set_value(value);
        tag.unblock();
    }
}

void set_double_spin_if_different(Gtk::SpinButton* spin, sigc::connection& tag, double value)
{
    double const currentValue = spin->get_value();

    if ((int)(currentValue * 100) != (int)(value * 100))
    {
        tag.block();
        spin->set_value(value);
        tag.unblock();
    }
}

void unset_combo(Gtk::ComboBox* combobox, sigc::connection& tag)
{
    tag.block();
    combobox->set_active(-1);
    tag.unblock();
}

} // namespace

void DetailsDialog::Impl::refreshOptions(std::vector<tr_torrent*> const& torrents)
{
    /***
    ****  Options Page
    ***/

    /* honor_limits_check */
    if (!torrents.empty())
    {
        bool const baseline = tr_torrentUsesSessionLimits(torrents.front());
        bool const is_uniform = std::ranges::all_of(
            torrents,
            [baseline](auto const* torrent) { return baseline == tr_torrentUsesSessionLimits(torrent); });

        if (is_uniform)
        {
            set_togglebutton_if_different(honor_limits_check_, honor_limits_check_tag_, baseline);
        }
    }

    /* down_limited_check */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentUsesSpeedLimit(torrents.front(), tr_direction::Down);
        auto const is_uniform = std::ranges::all_of(
            torrents,
            [baseline](auto const* torrent) { return baseline == tr_torrentUsesSpeedLimit(torrent, tr_direction::Down); });

        if (is_uniform)
        {
            set_togglebutton_if_different(down_limited_check_, down_limited_check_tag_, baseline);
        }
    }

    /* down_limit_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetSpeedLimit_KBps(torrents.front(), tr_direction::Down);
        auto const is_uniform = std::ranges::all_of(
            torrents,
            [baseline](auto const* torrent) { return baseline == tr_torrentGetSpeedLimit_KBps(torrent, tr_direction::Down); });

        if (is_uniform)
        {
            set_int_spin_if_different(down_limit_spin_, down_limit_spin_tag_, static_cast<int>(baseline));
        }
    }

    /* up_limited_check */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentUsesSpeedLimit(torrents.front(), tr_direction::Up);
        auto const is_uniform = std::ranges::all_of(
            torrents,
            [baseline](auto const* torrent) { return baseline == tr_torrentUsesSpeedLimit(torrent, tr_direction::Up); });

        if (is_uniform)
        {
            set_togglebutton_if_different(up_limited_check_, up_limited_check_tag_, baseline);
        }
    }

    /* up_limit_sping */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetSpeedLimit_KBps(torrents.front(), tr_direction::Up);
        auto const is_uniform = std::ranges::all_of(
            torrents,
            [baseline](auto const* torrent) { return baseline == tr_torrentGetSpeedLimit_KBps(torrent, tr_direction::Up); });

        if (is_uniform)
        {
            set_int_spin_if_different(up_limit_sping_, up_limit_spin_tag_, static_cast<int>(baseline));
        }
    }

    /* bandwidth_combo */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetPriority(torrents.front());
        bool const is_uniform = std::ranges::all_of(
            torrents,
            [baseline](auto const* torrent) { return baseline == tr_torrentGetPriority(torrent); });

        if (is_uniform)
        {
            bandwidth_combo_tag_.block();
            gtr_combo_box_set_active_enum(*bandwidth_combo_, baseline);
            bandwidth_combo_tag_.unblock();
        }
        else
        {
            unset_combo(bandwidth_combo_, bandwidth_combo_tag_);
        }
    }

    /* ratio_combo */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetRatioMode(torrents.front());
        bool const is_uniform = std::ranges::all_of(
            torrents,
            [baseline](auto const* torrent) { return baseline == tr_torrentGetRatioMode(torrent); });

        if (is_uniform)
        {
            ratio_combo_tag_.block();
            gtr_combo_box_set_active_enum(*ratio_combo_, baseline);
            gtr_widget_set_visible(*ratio_spin_, baseline == TR_RATIOLIMIT_SINGLE);
            ratio_combo_tag_.unblock();
        }
    }

    /* ratio_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetRatioLimit(torrents.front());
        set_double_spin_if_different(ratio_spin_, ratio_spin_tag_, baseline);
    }

    /* idle_combo */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetIdleMode(torrents.front());
        bool const is_uniform = std::ranges::all_of(
            torrents,
            [baseline](auto const* torrent) { return baseline == tr_torrentGetIdleMode(torrent); });

        if (is_uniform)
        {
            idle_combo_tag_.block();
            gtr_combo_box_set_active_enum(*idle_combo_, baseline);
            gtr_widget_set_visible(*idle_spin_, baseline == TR_IDLELIMIT_SINGLE);
            idle_combo_tag_.unblock();
        }
    }

    /* idle_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetIdleLimit(torrents.front());
        set_int_spin_if_different(idle_spin_, idle_spin_tag_, baseline);
    }

    /* max_peers_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetPeerLimit(torrents.front());
        set_int_spin_if_different(max_peers_spin_, max_peers_spin_tag_, baseline);
    }
}

void DetailsDialog::Impl::options_page_init(Glib::RefPtr<Gtk::Builder> const& /*builder*/)
{
    auto const speed_units_kbyps_str = Speed::units().display_name(Speed::Units::KByps);

    honor_limits_check_tag_ = honor_limits_check_->signal_toggled().connect(
        [this]() { torrent_set_field(TR_KEY_honors_session_limits, honor_limits_check_->get_active()); });

    down_limited_check_->set_label(
        fmt::format(fmt::runtime(down_limited_check_->get_label().raw()), fmt::arg("speed_units", speed_units_kbyps_str)));
    down_limited_check_tag_ = down_limited_check_->signal_toggled().connect(
        [this]() { torrent_set_field(TR_KEY_download_limited, down_limited_check_->get_active()); });

    down_limit_spin_->set_adjustment(Gtk::Adjustment::create(0, 0, std::numeric_limits<int>::max(), 5));
    down_limit_spin_tag_ = down_limit_spin_->signal_value_changed().connect(
        [this]() { torrent_set_field(TR_KEY_download_limit, down_limit_spin_->get_value_as_int()); });

    up_limited_check_->set_label(
        fmt::format(fmt::runtime(up_limited_check_->get_label().raw()), fmt::arg("speed_units", speed_units_kbyps_str)));
    up_limited_check_tag_ = up_limited_check_->signal_toggled().connect(
        [this]() { torrent_set_field(TR_KEY_upload_limited, up_limited_check_->get_active()); });

    up_limit_sping_->set_adjustment(Gtk::Adjustment::create(0, 0, std::numeric_limits<int>::max(), 5));
    up_limit_spin_tag_ = up_limit_sping_->signal_value_changed().connect(
        [this]() { torrent_set_field(TR_KEY_upload_limit, up_limit_sping_->get_value_as_int()); });

    gtr_priority_combo_init(*bandwidth_combo_);
    bandwidth_combo_tag_ = bandwidth_combo_->signal_changed().connect(
        [this]() { torrent_set_field(TR_KEY_bandwidth_priority, gtr_combo_box_get_active_enum(*bandwidth_combo_)); });

    gtr_combo_box_set_enum(
        *ratio_combo_,
        {
            { _("Use global settings"), TR_RATIOLIMIT_GLOBAL },
            { _("Seed regardless of ratio"), TR_RATIOLIMIT_UNLIMITED },
            { _("Stop seeding at ratio:"), TR_RATIOLIMIT_SINGLE },
        });
    ratio_combo_tag_ = ratio_combo_->signal_changed().connect(
        [this]()
        {
            torrent_set_field(TR_KEY_seed_ratio_mode, gtr_combo_box_get_active_enum(*ratio_combo_));
            refresh();
        });
    ratio_spin_->set_adjustment(Gtk::Adjustment::create(0, 0, 1000, .05));
    ratio_spin_->set_width_chars(7);
    ratio_spin_tag_ = ratio_spin_->signal_value_changed().connect(
        [this]() { torrent_set_field(TR_KEY_seed_ratio_limit, ratio_spin_->get_value()); });

    gtr_combo_box_set_enum(
        *idle_combo_,
        {
            { _("Use global settings"), TR_IDLELIMIT_GLOBAL },
            { _("Seed regardless of activity"), TR_IDLELIMIT_UNLIMITED },
            { _("Stop seeding if idle for N minutes:"), TR_IDLELIMIT_SINGLE },
        });
    idle_combo_tag_ = idle_combo_->signal_changed().connect(
        [this]()
        {
            torrent_set_field(TR_KEY_seed_idle_mode, gtr_combo_box_get_active_enum(*idle_combo_));
            refresh();
        });
    idle_spin_->set_adjustment(Gtk::Adjustment::create(1, 1, 40320, 5));
    idle_spin_tag_ = idle_spin_->signal_value_changed().connect(
        [this]() { torrent_set_field(TR_KEY_seed_idle_limit, idle_spin_->get_value_as_int()); });

    max_peers_spin_->set_adjustment(Gtk::Adjustment::create(1, 1, 3000, 5));
    max_peers_spin_tag_ = max_peers_spin_->signal_value_changed().connect(
        [this]() { torrent_set_field(TR_KEY_peer_limit, max_peers_spin_->get_value_as_int()); });
}
