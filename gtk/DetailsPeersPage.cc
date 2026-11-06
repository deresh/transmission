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
*****  PEERS TAB
*****
****/

namespace
{

class WebseedModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    WebseedModelColumns() noexcept
    {
        add(key);
        add(was_updated);
        add(url);
        add(download_rate_speed);
        add(download_rate_string);
    }

    Gtk::TreeModelColumn<std::string> key;
    Gtk::TreeModelColumn<bool> was_updated;
    Gtk::TreeModelColumn<Glib::ustring> url;
    Gtk::TreeModelColumn<Speed> download_rate_speed;
    Gtk::TreeModelColumn<Glib::ustring> download_rate_string;
};

WebseedModelColumns const webseed_cols;

class PeerModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    PeerModelColumns() noexcept
    {
        add(key);
        add(was_updated);
        add(address);
        add(address_collated);
        add(download_rate_speed);
        add(download_rate_string);
        add(upload_rate_speed);
        add(upload_rate_string);
        add(client);
        add(progress);
        add(upload_request_count_number);
        add(upload_request_count_string);
        add(download_request_count_number);
        add(download_request_count_string);
        add(blocks_downloaded_count_number);
        add(blocks_downloaded_count_string);
        add(blocks_uploaded_count_number);
        add(blocks_uploaded_count_string);
        add(reqs_cancelled_by_client_count_number);
        add(reqs_cancelled_by_client_count_string);
        add(reqs_cancelled_by_peer_count_number);
        add(reqs_cancelled_by_peer_count_string);
        add(encryption_stock_id);
        add(flags);
        add(torrent_name);
    }

    Gtk::TreeModelColumn<std::string> key;
    Gtk::TreeModelColumn<bool> was_updated;
    Gtk::TreeModelColumn<Glib::ustring> address;
    Gtk::TreeModelColumn<Glib::ustring> address_collated;
    Gtk::TreeModelColumn<Speed> download_rate_speed;
    Gtk::TreeModelColumn<Glib::ustring> download_rate_string;
    Gtk::TreeModelColumn<Speed> upload_rate_speed;
    Gtk::TreeModelColumn<Glib::ustring> upload_rate_string;
    Gtk::TreeModelColumn<Glib::ustring> client;
    Gtk::TreeModelColumn<int> progress;
    Gtk::TreeModelColumn<decltype(tr_peer_stat::active_reqs_to_client)> upload_request_count_number;
    Gtk::TreeModelColumn<Glib::ustring> upload_request_count_string;
    Gtk::TreeModelColumn<decltype(tr_peer_stat::active_reqs_to_peer)> download_request_count_number;
    Gtk::TreeModelColumn<Glib::ustring> download_request_count_string;
    Gtk::TreeModelColumn<decltype(tr_peer_stat::blocks_to_client)> blocks_downloaded_count_number;
    Gtk::TreeModelColumn<Glib::ustring> blocks_downloaded_count_string;
    Gtk::TreeModelColumn<decltype(tr_peer_stat::blocks_to_peer)> blocks_uploaded_count_number;
    Gtk::TreeModelColumn<Glib::ustring> blocks_uploaded_count_string;
    Gtk::TreeModelColumn<decltype(tr_peer_stat::cancels_to_peer)> reqs_cancelled_by_client_count_number;
    Gtk::TreeModelColumn<Glib::ustring> reqs_cancelled_by_client_count_string;
    Gtk::TreeModelColumn<decltype(tr_peer_stat::cancels_to_client)> reqs_cancelled_by_peer_count_number;
    Gtk::TreeModelColumn<Glib::ustring> reqs_cancelled_by_peer_count_string;
    Gtk::TreeModelColumn<Glib::ustring> encryption_stock_id;
    Gtk::TreeModelColumn<Glib::ustring> flags;
    Gtk::TreeModelColumn<Glib::ustring> torrent_name;
};

PeerModelColumns const peer_cols;

void initPeerRow(
    Gtk::TreeModel::iterator const& iter,
    std::string_view const key,
    std::string_view const torrent_name,
    tr_peer_stat const& peer)
{
    auto peer_addr4 = in_addr();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto const* const peer_addr4_octets = reinterpret_cast<uint8_t const*>(&peer_addr4.s_addr);
    auto const collated_name = inet_pton(AF_INET, peer.addr.c_str(), &peer_addr4) != 1 ?
        peer.addr :
        fmt::format(
            "{:03}",
            fmt::join(
                peer_addr4_octets,
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                peer_addr4_octets + sizeof(peer_addr4.s_addr), // TODO(C++20): Use std::span
                "."));

    (*iter)[peer_cols.address] = peer.addr;
    (*iter)[peer_cols.address_collated] = collated_name;
    (*iter)[peer_cols.client] = peer.user_agent;
    (*iter)[peer_cols.encryption_stock_id] = peer.is_encrypted ? "lock" : "";
    (*iter)[peer_cols.key] = std::string(key);
    (*iter)[peer_cols.torrent_name] = std::string(torrent_name);
}

void refreshPeerRow(Gtk::TreeModel::iterator const& iter, tr_peer_stat const& peer)
{
    auto const down_speed = peer.rate_to_client;
    auto const up_speed = peer.rate_to_peer;

    auto blocks_to_client = std::string{};
    auto blocks_to_peer = std::string{};
    auto cancelled_by_client = std::string{};
    auto cancelled_by_peer = std::string{};
    auto down_count = std::string{};
    auto down_speed_string = std::string{};
    auto up_count = std::string{};
    auto up_speed_string = std::string{};

    if (peer.rate_to_peer.base_quantity() > 0U)
    {
        up_speed_string = up_speed.to_string();
    }

    if (peer.rate_to_client.base_quantity() > 0U)
    {
        down_speed_string = down_speed.to_string();
    }

    if (peer.active_reqs_to_peer > 0)
    {
        down_count = std::to_string(peer.active_reqs_to_peer);
    }

    if (peer.active_reqs_to_client > 0)
    {
        up_count = std::to_string(peer.active_reqs_to_client);
    }

    if (peer.blocks_to_peer > 0)
    {
        blocks_to_peer = std::to_string(peer.blocks_to_peer);
    }

    if (peer.blocks_to_client > 0)
    {
        blocks_to_client = std::to_string(peer.blocks_to_client);
    }

    if (peer.cancels_to_peer > 0)
    {
        cancelled_by_client = std::to_string(peer.cancels_to_peer);
    }

    if (peer.cancels_to_client > 0)
    {
        cancelled_by_peer = std::to_string(peer.cancels_to_client);
    }

    (*iter)[peer_cols.progress] = static_cast<int>(100.0 * peer.progress);
    (*iter)[peer_cols.upload_request_count_number] = peer.active_reqs_to_client;
    (*iter)[peer_cols.upload_request_count_string] = up_count;
    (*iter)[peer_cols.download_request_count_number] = peer.active_reqs_to_peer;
    (*iter)[peer_cols.download_request_count_string] = down_count;
    (*iter)[peer_cols.download_rate_speed] = down_speed;
    (*iter)[peer_cols.download_rate_string] = down_speed_string;
    (*iter)[peer_cols.upload_rate_speed] = up_speed;
    (*iter)[peer_cols.upload_rate_string] = up_speed_string;
    (*iter)[peer_cols.flags] = peer.flag_str;
    (*iter)[peer_cols.was_updated] = true;
    (*iter)[peer_cols.blocks_downloaded_count_number] = peer.blocks_to_client;
    (*iter)[peer_cols.blocks_downloaded_count_string] = blocks_to_client;
    (*iter)[peer_cols.blocks_uploaded_count_number] = peer.blocks_to_peer;
    (*iter)[peer_cols.blocks_uploaded_count_string] = blocks_to_peer;
    (*iter)[peer_cols.reqs_cancelled_by_client_count_number] = peer.cancels_to_peer;
    (*iter)[peer_cols.reqs_cancelled_by_client_count_string] = cancelled_by_client;
    (*iter)[peer_cols.reqs_cancelled_by_peer_count_number] = peer.cancels_to_client;
    (*iter)[peer_cols.reqs_cancelled_by_peer_count_string] = cancelled_by_peer;
}

} // namespace

void DetailsDialog::Impl::refreshPeerList(std::vector<tr_torrent*> const& torrents)
{
    auto& hash = peer_hash_;
    auto const& store = peer_store_;

    /* step 1: get all the peers */
    std::vector<std::vector<tr_peer_stat>> peers;

    peers.reserve(torrents.size());
    for (auto const* const torrent : torrents)
    {
        peers.push_back(tr_torrentPeers(torrent));
    }

    /* step 2: mark all the peers in the list as not-updated */
    for (auto& row : store->children())
    {
        row[peer_cols.was_updated] = false;
    }

    auto make_key = [](tr_torrent const* tor, tr_peer_stat const& ps)
    {
        return fmt::format("{:d}.{:s}", tr_torrentId(tor), ps.addr);
    };

    /* step 3: add any new peers */
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        auto const* tor = torrents.at(i);
        auto const& torrent_peers = peers.at(i);

        for (auto const& peer : torrent_peers)
        {
            auto const key = make_key(tor, peer);

            if (!hash.contains(key))
            {
                auto const iter = store->append();
                initPeerRow(iter, key, tr_torrentName(tor), peer);
                hash.try_emplace(key, Gtk::TreeRowReference(store, store->get_path(iter)));
            }
        }
    }

    /* step 4: update the peers */
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        auto const* tor = torrents.at(i);
        auto const& torrent_peers = peers.at(i);

        for (auto const& peer : torrent_peers)
        {
            auto const key = make_key(tor, peer);
            refreshPeerRow(store->get_iter(hash.at(key).get_path()), peer);
        }
    }

    /* step 5: remove peers that have disappeared */
    if (auto iter = store->children().begin(); iter)
    {
        while (iter)
        {
            if (iter->get_value(peer_cols.was_updated))
            {
                ++iter;
            }
            else
            {
                auto const key = iter->get_value(peer_cols.key);
                hash.erase(key);
                iter = store->erase(iter);
            }
        }
    }
}

void DetailsDialog::Impl::refreshWebseedList(std::vector<tr_torrent*> const& torrents)
{
    auto has_any_webseeds = false;
    auto& hash = webseed_hash_;
    auto const& store = webseed_store_;

    auto make_key = [](tr_torrent const* tor, char const* url)
    {
        return fmt::format("{:d}.{:s}", tr_torrentId(tor), url);
    };

    /* step 1: mark all webseeds as not-updated */
    for (auto& row : store->children())
    {
        row[webseed_cols.was_updated] = false;
    }

    /* step 2: add any new webseeds */
    for (auto const* const tor : torrents)
    {
        for (size_t j = 0, n = tr_torrentWebseedCount(tor); j < n; ++j)
        {
            has_any_webseeds = true;

            auto const* const url = tr_torrentWebseed(tor, j).url;
            auto const key = make_key(tor, url);

            if (!hash.contains(key))
            {
                auto const iter = store->append();
                (*iter)[webseed_cols.url] = url;
                (*iter)[webseed_cols.key] = key;
                hash.try_emplace(key, Gtk::TreeRowReference(store, store->get_path(iter)));
            }
        }
    }

    /* step 3: update the webseeds */
    for (auto const* const tor : torrents)
    {
        for (size_t j = 0, n = tr_torrentWebseedCount(tor); j < n; ++j)
        {
            auto const webseed = tr_torrentWebseed(tor, j);
            auto const key = make_key(tor, webseed.url);
            auto const iter = store->get_iter(hash.at(key).get_path());

            auto const speed = Speed{ webseed.download_bytes_per_second, Speed::Units::Byps };
            auto const speed_string = webseed.is_downloading ? speed.to_string() : std::string{};

            (*iter)[webseed_cols.download_rate_speed] = speed;
            (*iter)[webseed_cols.download_rate_string] = speed_string;
            (*iter)[webseed_cols.was_updated] = true;
        }
    }

    /* step 4: remove webseeds that have disappeared */
    if (auto iter = store->children().begin(); iter)
    {
        while (iter)
        {
            if (iter->get_value(webseed_cols.was_updated))
            {
                ++iter;
            }
            else
            {
                auto const key = iter->get_value(webseed_cols.key);
                hash.erase(key);
                iter = store->erase(iter);
            }
        }
    }

    /* most of the time there are no webseeds...
       don't waste space showing an empty list */
    webseed_view_->set_visible(has_any_webseeds);
}

void DetailsDialog::Impl::refreshPeers(std::vector<tr_torrent*> const& torrents)
{
    refreshPeerList(torrents);
    refreshWebseedList(torrents);
}

bool DetailsDialog::Impl::onPeerViewQueryTooltip(int x, int y, bool keyboard_tip, Glib::RefPtr<Gtk::Tooltip> const& tooltip)
{
    Gtk::TreeModel::iterator iter;
    bool show_tip = false;

    if (peer_view_->get_tooltip_context_iter(x, y, keyboard_tip, iter))
    {
        auto const name = iter->get_value(peer_cols.torrent_name);
        auto const addr = iter->get_value(peer_cols.address);
        auto const flagstr = iter->get_value(peer_cols.flags);

        std::ostringstream gstr;
        gstr << "<b>" << Glib::Markup::escape_text(name) << "</b>\n" << addr << "\n \n";

        for (auto const ch : flagstr)
        {
            char const* s = nullptr;

            switch (ch)
            {
            case 'O':
                s = _("Optimistic unchoke");
                break;

            case 'D':
                s = _("Downloading from this peer");
                break;

            case 'd':
                s = _("We would download from this peer if they would let us");
                break;

            case 'U':
                s = _("Uploading to peer");
                break;

            case 'u':
                s = _("We would upload to this peer if they asked");
                break;

            case 'K':
                s = _("Peer has unchoked us, but we're not interested");
                break;

            case '?':
                s = _("We unchoked this peer, but they're not interested");
                break;

            case 'E':
                s = _("Encrypted connection");
                break;

            case 'X':
                s = _("Peer was found through Peer Exchange (PEX)");
                break;

            case 'H':
                s = _("Peer was found through DHT");
                break;

            case 'I':
                s = _("Peer is an incoming connection");
                break;

            case 'T':
                s = _("Peer is connected over µTP");
                break;

            case 'h':
                s = _("Peer supports holepunch (BEP 55)");
                break;

            default:
                g_assert_not_reached();
            }

            if (s != nullptr)
            {
                gstr << ch << ": " << s << '\n';
            }
        }

        auto str = gstr.str();
        if (!str.empty()) /* remove the last linefeed */
        {
            str.resize(str.size() - 1);
        }

        tooltip->set_markup(str);

        show_tip = true;
    }

    return show_tip;
}

namespace
{

void setPeerViewColumns(Gtk::TreeView* peer_view)
{
    std::vector<Gtk::TreeModelColumnBase const*> view_columns;
    Gtk::TreeViewColumn* c = nullptr;
    bool const more = gtr_pref_flag_get(TR_KEY_show_extra_peer_details);

    view_columns.push_back(&peer_cols.encryption_stock_id);
    view_columns.push_back(&peer_cols.upload_rate_string);

    if (more)
    {
        view_columns.push_back(&peer_cols.upload_request_count_string);
    }

    view_columns.push_back(&peer_cols.download_rate_string);

    if (more)
    {
        view_columns.push_back(&peer_cols.download_request_count_string);
        view_columns.push_back(&peer_cols.blocks_downloaded_count_string);
        view_columns.push_back(&peer_cols.blocks_uploaded_count_string);
        view_columns.push_back(&peer_cols.reqs_cancelled_by_client_count_string);
        view_columns.push_back(&peer_cols.reqs_cancelled_by_peer_count_string);
    }

    view_columns.push_back(&peer_cols.progress);
    view_columns.push_back(&peer_cols.flags);
    view_columns.push_back(&peer_cols.address);
    view_columns.push_back(&peer_cols.client);

    /* remove any existing columns */
    peer_view->remove_all_columns();

    for (auto const* const col : view_columns)
    {
        auto const* sort_col = col;

        if (*col == peer_cols.address)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Address"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.address_collated;
        }
        else if (*col == peer_cols.progress)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererProgress>();
            // % is percent done
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("%"), *r);
            c->add_attribute(r->property_text(), *col);
        }
        else if (*col == peer_cols.encryption_stock_id)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
            r->property_xalign() = 0.0F;
            r->property_yalign() = 0.5F;
            c = Gtk::make_managed<Gtk::TreeViewColumn>(Glib::ustring(), *r);
            c->add_attribute(r->property_icon_name(), *col);
            c->set_sizing(Gtk::TreeViewColumn::Sizing::FIXED);
            c->set_fixed_width(20);
        }
        else if (*col == peer_cols.download_request_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Dn Reqs"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.download_request_count_number;
        }
        else if (*col == peer_cols.upload_request_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Up Reqs"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.upload_request_count_number;
        }
        else if (*col == peer_cols.blocks_downloaded_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Dn Blocks"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.blocks_downloaded_count_number;
        }
        else if (*col == peer_cols.blocks_uploaded_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Up Blocks"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.blocks_uploaded_count_number;
        }
        else if (*col == peer_cols.reqs_cancelled_by_client_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("We Cancelled"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.reqs_cancelled_by_client_count_number;
        }
        else if (*col == peer_cols.reqs_cancelled_by_peer_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("They Cancelled"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.reqs_cancelled_by_peer_count_number;
        }
        else if (*col == peer_cols.download_rate_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            r->property_xalign() = 1.0F;
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Down"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.download_rate_speed;
        }
        else if (*col == peer_cols.upload_rate_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            r->property_xalign() = 1.0F;
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Up"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.upload_rate_speed;
        }
        else if (*col == peer_cols.client)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Client"), *r);
            c->add_attribute(r->property_text(), *col);
        }
        else if (*col == peer_cols.flags)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Flags"), *r);
            c->add_attribute(r->property_text(), *col);
        }
        else
        {
            std::abort();
        }

        c->set_resizable(false);
        c->set_sort_column(*sort_col);
        peer_view->append_column(*c);
    }

    /* the 'expander' column has a 10-pixel margin on the left
       that doesn't look quite correct in any of these columns...
       so create a non-visible column and assign it as the
       'expander column. */
    c = Gtk::make_managed<Gtk::TreeViewColumn>();
    c->set_visible(false);
    peer_view->append_column(*c);
    peer_view->set_expander_column(*c);
}

} // namespace

void DetailsDialog::Impl::onMorePeerInfoToggled()
{
    tr_quark const key = TR_KEY_show_extra_peer_details;
    bool const value = more_peer_details_check_->get_active();
    core_->set_pref(key, value);
    setPeerViewColumns(peer_view_);
}

void DetailsDialog::Impl::peer_page_init(Glib::RefPtr<Gtk::Builder> const& builder)
{
    /* webseeds */

    webseed_store_ = Gtk::ListStore::create(webseed_cols);
    auto* v = gtr_get_widget<Gtk::TreeView>(builder, "webseeds_view");
    v->set_model(webseed_store_);
    setup_item_view_button_event_handling(
        *v,
        {},
        [v](double view_x, double view_y) { return on_item_view_button_released(*v, view_x, view_y); });

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        r->property_ellipsize() = Pango::EllipsizeMode::END;
        auto* c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Web Seeds"), *r);
        c->add_attribute(r->property_text(), webseed_cols.url);
        c->set_expand(true);
        c->set_sort_column(webseed_cols.url);
        v->append_column(*c);
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        auto* c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Down"), *r);
        c->add_attribute(r->property_text(), webseed_cols.download_rate_string);
        c->set_sort_column(webseed_cols.download_rate_speed);
        v->append_column(*c);
    }

    /* peers */

    peer_store_ = Gtk::ListStore::create(peer_cols);
    auto m = Gtk::TreeModelSort::create(peer_store_);
    m->set_sort_column(peer_cols.progress, Gtk::SortType::DESCENDING);

    peer_view_->set_model(m);
    peer_view_->set_has_tooltip(true);
    peer_view_->signal_query_tooltip().connect(sigc::mem_fun(*this, &Impl::onPeerViewQueryTooltip), false);
    setup_item_view_button_event_handling(
        *peer_view_,
        {},
        [this](double view_x, double view_y) { return on_item_view_button_released(*peer_view_, view_x, view_y); });

    setPeerViewColumns(peer_view_);

    more_peer_details_check_->set_active(gtr_pref_flag_get(TR_KEY_show_extra_peer_details));
    more_peer_details_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onMorePeerInfoToggled));
}
