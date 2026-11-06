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
*****  INFO TAB
*****
****/

namespace
{

Glib::ustring activityString(int activity, bool finished)
{
    switch (activity)
    {
    case TR_STATUS_CHECK_WAIT:
        return _("Queued for verification");

    case TR_STATUS_CHECK:
        return _("Verifying local data");

    case TR_STATUS_DOWNLOAD_WAIT:
        return _("Queued for download");

    case TR_STATUS_DOWNLOAD:
        return C_("Verb", "Downloading");

    case TR_STATUS_SEED_WAIT:
        return _("Queued for seeding");

    case TR_STATUS_SEED:
        return C_("Verb", "Seeding");

    case TR_STATUS_STOPPED:
        return finished ? _("Finished") : _("Paused");

    default:
        g_assert_not_reached();
    }

    return {};
}

/* Only call gtk_text_buffer_set_text () if the new text differs from the old.
 * This way if the user has text selected, refreshing won't deselect it */
void gtr_text_buffer_set_text(Glib::RefPtr<Gtk::TextBuffer> const& b, Glib::ustring const& str)
{
    if (b->get_text() != str)
    {
        b->set_text(str);
    }
}

[[nodiscard]] std::string get_date_string(time_t t)
{
    return t == 0 ? _("N/A") : fmt::format("{:%x}", *std::localtime(&t));
}

[[nodiscard]] std::string get_date_time_string(time_t t)
{
    return t == 0 ? _("N/A") : fmt::format("{:%c}", *std::localtime(&t));
}

} // namespace

void DetailsDialog::Impl::refreshInfo(std::vector<tr_torrent*> const& torrents)
{
    auto const now = time(nullptr);
    Glib::ustring str;
    Glib::ustring const mixed = _("Mixed");
    Glib::ustring const no_torrent = _("No Torrents Selected");
    Glib::ustring stateString;
    uint64_t sizeWhenDone = 0;

    auto const stats = tr_torrentStat(std::data(torrents), std::size(torrents));

    std::vector<tr_torrent_view> infos;
    infos.reserve(torrents.size());
    for (auto* const torrent : torrents)
    {
        infos.push_back(tr_torrentView(torrent));
    }

    /* privacy_lb */
    if (infos.empty())
    {
        str = no_torrent;
    }
    else
    {
        bool const baseline = infos.front().is_private;
        bool const is_uniform = std::ranges::all_of(
            infos,
            [baseline](auto const& info) { return info.is_private == baseline; });

        if (is_uniform)
        {
            str = baseline ? _("Private to this tracker -- DHT and PEX disabled") : _("Public torrent");
        }
        else
        {
            str = mixed;
        }
    }

    privacy_lb_->set_text(str);

    /* added_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const baseline = stats.front().added_date;
        bool const is_uniform = std::ranges::all_of(
            stats,
            [baseline](auto const& stat) { return stat.added_date == baseline; });

        if (is_uniform)
        {
            str = get_date_time_string(baseline);
        }
        else
        {
            str = mixed;
        }
    }

    added_lb_->set_text(str);

    /* origin_lb */
    if (infos.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const creator = tr_strv_strip(infos.front().creator != nullptr ? infos.front().creator : "");
        auto const date = infos.front().date_created;
        auto const datestr = get_date_string(date);
        bool const mixed_creator = std::ranges::any_of(
            infos,
            [&creator](auto const& info) { return creator != (info.creator != nullptr ? info.creator : ""); });
        bool const mixed_date = std::ranges::any_of(infos, [date](auto const& info) { return date != info.date_created; });

        bool const empty_creator = std::empty(creator);
        bool const empty_date = date == 0;

        if (mixed_creator || mixed_date)
        {
            str = mixed;
        }
        else if (!empty_creator && !empty_date)
        {
            str = fmt::format(
                fmt::runtime(_("Created by {creator} on {date}")),
                fmt::arg("creator", creator),
                fmt::arg("date", datestr));
        }
        else if (!empty_creator)
        {
            str = fmt::format(fmt::runtime(_("Created by {creator}")), fmt::arg("creator", creator));
        }
        else if (!empty_date)
        {
            str = fmt::format(fmt::runtime(_("Created on {date}")), fmt::arg("date", datestr));
        }
        else
        {
            str = _("N/A");
        }
    }

    origin_lb_->set_text(str);

    /* comment_buffer */
    if (infos.empty())
    {
        str.clear();
    }
    else
    {
        auto const baseline = Glib::ustring(infos.front().comment != nullptr ? infos.front().comment : "");
        bool const is_uniform = std::ranges::all_of(
            infos,
            [&baseline](auto const& info) { return baseline == (info.comment != nullptr ? info.comment : ""); });

        str = is_uniform ? baseline : mixed;
    }

    gtr_text_buffer_set_text(comment_buffer_, str);

    /* destination_lb */
    if (torrents.empty())
    {
        str = no_torrent;
    }
    else
    {
        std::string_view const baseline = tr_torrentGetDownloadDir(torrents.front());
        bool const is_uniform = std::ranges::all_of(
            torrents,
            [&baseline](auto const* torrent) { return baseline == tr_torrentGetDownloadDir(torrent); });

        str = is_uniform ? Glib::ustring{ baseline.data(), baseline.size() } : mixed;
    }

    destination_lb_->set_text(str);

    /* state_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const baseline = stats.front().activity;
        bool const is_uniform = std::ranges::all_of(stats, [baseline](auto const& st) { return baseline == st.activity; });
        bool const all_finished = std::ranges::all_of(stats, [](auto const& st) { return st.finished; });

        str = is_uniform ? activityString(baseline, all_finished) : mixed;
    }

    stateString = str;
    state_lb_->set_text(str);

    /* date started */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        time_t const baseline = stats.front().start_date;
        bool const is_uniform = std::ranges::all_of(stats, [baseline](auto const& st) { return baseline == st.start_date; });

        if (!is_uniform)
        {
            str = mixed;
        }
        else if (baseline <= 0 || stats[0].activity == TR_STATUS_STOPPED)
        {
            str = stateString;
        }
        else
        {
            str = tr_format_time(now - baseline);
        }
    }

    date_started_lb_->set_text(str);

    /* eta */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const baseline = stats.front().eta;
        auto const is_uniform = std::ranges::all_of(stats, [baseline](auto const& st) { return baseline == st.eta; });

        if (!is_uniform)
        {
            str = mixed;
        }
        else if (baseline < 0)
        {
            str = _("Unknown");
        }
        else
        {
            str = tr_format_time_left(baseline);
        }
    }

    eta_lb_->set_text(str);

    /* size_lb */
    {
        auto const piece_count = std::accumulate(
            std::begin(infos),
            std::end(infos),
            uint64_t{},
            [](auto sum, auto const& info) { return sum + info.n_pieces; });

        if (piece_count == 0)
        {
            str.clear();
        }
        else
        {
            auto const total_size = std::accumulate(
                std::begin(infos),
                std::end(infos),
                uint64_t{},
                [](auto sum, auto const& info) { return sum + info.total_size; });

            auto const file_count = std::accumulate(
                std::begin(torrents),
                std::end(torrents),
                std::size_t{},
                [](auto sum, auto const* tor) { return sum + tr_torrentFileCount(tor); });

            str = fmt::format(
                fmt::runtime(
                    ngettext("{total_size} in {file_count:L} file", "{total_size} in {file_count:L} files", file_count)),
                fmt::arg("total_size", tr_strlsize(total_size)),
                fmt::arg("file_count", file_count));

            auto const piece_size = std::empty(infos) ? uint32_t{} : infos.front().piece_size;
            auto const piece_size_is_uniform = std::ranges::all_of(
                infos,
                [piece_size](auto const& info) { return info.piece_size == piece_size; });

            if (piece_size_is_uniform)
            {
                str += ' ';
                str += fmt::format(
                    fmt::runtime(ngettext(
                        "({piece_count} BitTorrent piece @ {piece_size})",
                        "({piece_count} BitTorrent pieces @ {piece_size})",
                        piece_count)),
                    fmt::arg("piece_count", piece_count),
                    fmt::arg("piece_size", Memory{ piece_size, Memory::Units::Bytes }.to_string()));
            }
        }

        size_lb_->set_text(str);
    }

    /* have_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        uint64_t leftUntilDone = 0;
        uint64_t haveUnchecked = 0;
        uint64_t haveValid = 0;
        uint64_t available = 0;

        for (auto const& st : stats)
        {
            haveUnchecked += st.have_unchecked;
            haveValid += st.have_valid;
            sizeWhenDone += st.size_when_done;
            leftUntilDone += st.left_until_done;
            available += st.size_when_done - st.left_until_done + st.have_unchecked + st.desired_available;
        }

        {
            auto const d = sizeWhenDone != 0 ? 100.0 * static_cast<double>(available) / static_cast<double>(sizeWhenDone) : 0;
            auto const ratio = 100.0 *
                (sizeWhenDone != 0 ? static_cast<double>(haveValid + haveUnchecked) / static_cast<double>(sizeWhenDone) : 1.);

            auto const avail = tr_strpercent(d);
            auto const buf2 = tr_strpercent(ratio);
            auto const total = tr_strlsize(haveUnchecked + haveValid);
            auto const unver = tr_strlsize(haveUnchecked);

            if (haveUnchecked == 0 && leftUntilDone == 0)
            {
                str = fmt::format(
                    fmt::runtime(_("{current_size} ({percent_done}%)")),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2));
            }
            else if (haveUnchecked == 0)
            {
                str = fmt::format(
                    // xgettext:no-c-format
                    fmt::runtime(_("{current_size} ({percent_done}% of {percent_available}% available)")),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2),
                    fmt::arg("percent_available", avail));
            }
            else
            {
                str = fmt::format(
                    // xgettext:no-c-format
                    fmt::runtime(
                        _("{current_size} ({percent_done}% of {percent_available}% available; {unverified_size} unverified)")),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2),
                    fmt::arg("percent_available", avail),
                    fmt::arg("unverified_size", unver));
            }
        }
    }

    have_lb_->set_text(str);

    // dl_lb
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const downloaded_str = tr_strlsize(
            std::accumulate(
                std::begin(stats),
                std::end(stats),
                uint64_t{ 0 },
                [](auto sum, auto const& st) { return sum + st.downloaded_ever; }));

        auto const failed = std::accumulate(
            std::begin(stats),
            std::end(stats),
            uint64_t{ 0 },
            [](auto sum, auto const& st) { return sum + st.corrupt_ever; });

        if (failed != 0)
        {
            str = fmt::format(
                fmt::runtime(_("{downloaded_size} (+{discarded_size} discarded after failed checksum)")),
                fmt::arg("downloaded_size", downloaded_str),
                fmt::arg("discarded_size", tr_strlsize(failed)));
        }
        else
        {
            str = downloaded_str;
        }
    }

    dl_lb_->set_text(str);

    /* ul_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const uploaded = std::accumulate(
            std::begin(stats),
            std::end(stats),
            uint64_t{},
            [](auto sum, auto const& st) { return sum + st.uploaded_ever; });
        auto const denominator = std::accumulate(
            std::begin(stats),
            std::end(stats),
            uint64_t{},
            [](auto sum, auto const& st) { return sum + st.size_when_done; });
        str = fmt::format(
            fmt::runtime(_("{uploaded_size} (Ratio: {ratio})")),
            fmt::arg("uploaded_size", tr_strlsize(uploaded)),
            fmt::arg("ratio", tr_strlratio(tr_getRatio(uploaded, denominator))));
    }

    ul_lb_->set_text(str);

    /* hash_lb */
    if (infos.empty())
    {
        str = no_torrent;
    }
    else if (infos.size() == 1)
    {
        str = infos.front().hash_string;
    }
    else
    {
        str = mixed;
    }

    hash_lb_->set_text(str);

    /* error */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const& baseline = stats.front().error_string;
        bool const is_uniform = std::ranges::all_of(stats, [&baseline](auto const& st) { return baseline == st.error_string; });

        str = is_uniform ? Glib::ustring{ baseline } : mixed;
    }

    if (str.empty())
    {
        str = _("No errors");
    }

    error_lb_->set_text(str);

    /* activity date */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const iter = std::ranges::max_element(
            stats,
            [](auto const& lhs, auto const& rhs) { return lhs.activity_date < rhs.activity_date; });
        time_t const latest = iter->activity_date;

        if (latest <= 0)
        {
            str = _("Never");
        }
        else if ((now - latest) < 5U)
        {
            str = _("Active now");
        }
        else
        {
            str = tr_format_time_relative(now, latest);
        }
    }

    last_activity_lb_->set_text(str);
}

void DetailsDialog::Impl::info_page_init(Glib::RefPtr<Gtk::Builder> const& builder)
{
    comment_buffer_ = Gtk::TextBuffer::create();
    auto* tw = gtr_get_widget<Gtk::TextView>(builder, "comment_value_view");
    tw->set_buffer(comment_buffer_);
}
