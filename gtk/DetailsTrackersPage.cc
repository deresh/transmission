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

/****
*****
*****  TRACKER
*****
****/

namespace
{

auto constexpr ErrMarkupBegin = "<span color='red'>"sv;
auto constexpr ErrMarkupEnd = "</span>"sv;
auto constexpr TimeoutMarkupBegin = "<span color='#246'>"sv;
auto constexpr TimeoutMarkupEnd = "</span>"sv;
auto constexpr SuccessMarkupBegin = "<span color='#080'>"sv;
auto constexpr SuccessMarkupEnd = "</span>"sv;

std::array<std::string_view, 3> const text_dir_mark = { ""sv, "\u200E"sv, "\u200F"sv };

void appendAnnounceInfo(tr_tracker_view const& tracker, time_t const now, Gtk::TextDirection direction, std::ostream& gstr)
{
    auto const dir_mark = text_dir_mark.at(static_cast<int>(direction));

    if (tracker.hasAnnounced && tracker.announceState != TR_TRACKER_INACTIVE)
    {
        gstr << '\n';
        gstr << dir_mark;
        auto const time_span_ago = tr_format_time_relative(now, tracker.lastAnnounceTime);

        if (tracker.lastAnnounceSucceeded)
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the peer text
                fmt::runtime(ngettext(
                    "Got a list of {markup_begin}{peer_count} peer{markup_end} {time_span_ago}",
                    "Got a list of {markup_begin}{peer_count} peers{markup_end} {time_span_ago}",
                    tracker.lastAnnouncePeerCount)),
                fmt::arg("markup_begin", SuccessMarkupBegin),
                fmt::arg("peer_count", tracker.lastAnnouncePeerCount),
                fmt::arg("markup_end", SuccessMarkupEnd),
                fmt::arg("time_span_ago", time_span_ago));
        }
        else if (tracker.lastAnnounceTimedOut)
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the time_span
                fmt::runtime(_("Peer list request {markup_begin}timed out {time_span_ago}{markup_end}; will retry")),
                fmt::arg("markup_begin", TimeoutMarkupBegin),
                fmt::arg("time_span_ago", time_span_ago),
                fmt::arg("markup_end", TimeoutMarkupEnd));
        }
        else
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the error
                fmt::runtime(_("Got an error '{markup_begin}{error}{markup_end}' {time_span_ago}")),
                fmt::arg("markup_begin", ErrMarkupBegin),
                fmt::arg("error", Glib::Markup::escape_text(std::data(tracker.lastAnnounceResult))),
                fmt::arg("markup_end", ErrMarkupEnd),
                fmt::arg("time_span_ago", time_span_ago));
        }
    }

    switch (tracker.announceState)
    {
    case TR_TRACKER_INACTIVE:
        gstr << '\n';
        gstr << dir_mark;
        gstr << _("No updates scheduled");
        break;

    case TR_TRACKER_WAITING:
        gstr << '\n';
        gstr << dir_mark;
        gstr << fmt::format(
            fmt::runtime(_("Asking for more peers {time_span_from_now}")),
            fmt::arg("time_span_from_now", tr_format_time_relative(now, tracker.nextAnnounceTime)));
        break;

    case TR_TRACKER_QUEUED:
        gstr << '\n';
        gstr << dir_mark;
        gstr << _("Queued to ask for more peers");
        break;

    case TR_TRACKER_ACTIVE:
        gstr << '\n';
        gstr << dir_mark;
        gstr << fmt::format(
            // {markup_begin} and {markup_end} should surround time_span_ago
            fmt::runtime(_("Asked for more peers {markup_begin}{time_span_ago}{markup_end}")),
            fmt::arg("markup_begin", "<small>"),
            fmt::arg("time_span_ago", tr_format_time_relative(now, tracker.lastAnnounceStartTime)),
            fmt::arg("markup_end", "</small>"));
        break;

    default:
        g_assert_not_reached();
    }
}

void appendScrapeInfo(tr_tracker_view const& tracker, time_t const now, Gtk::TextDirection direction, std::ostream& gstr)
{
    auto const dir_mark = text_dir_mark.at(static_cast<int>(direction));

    if (tracker.hasScraped)
    {
        gstr << '\n';
        gstr << dir_mark;
        auto const time_span_ago = tr_format_time_relative(now, tracker.lastScrapeTime);

        if (tracker.lastScrapeSucceeded)
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the seeder/leecher text
                fmt::runtime(_(
                    "Tracker had {markup_begin}{seeder_count} {seeder_or_seeders} and {leecher_count} {leecher_or_leechers}{markup_end} {time_span_ago}")),
                fmt::arg("seeder_count", tracker.seederCount),
                fmt::arg("seeder_or_seeders", ngettext("seeder", "seeders", tracker.seederCount)),
                fmt::arg("leecher_count", tracker.leecherCount),
                fmt::arg("leecher_or_leechers", ngettext("leecher", "leechers", tracker.leecherCount)),
                fmt::arg("time_span_ago", time_span_ago),
                fmt::arg("markup_begin", SuccessMarkupBegin),
                fmt::arg("markup_end", SuccessMarkupEnd));
        }
        else
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the error text
                fmt::runtime(_("Got a scrape error '{markup_begin}{error}{markup_end}' {time_span_ago}")),
                fmt::arg("error", Glib::Markup::escape_text(std::data(tracker.lastScrapeResult))),
                fmt::arg("time_span_ago", time_span_ago),
                fmt::arg("markup_begin", ErrMarkupBegin),
                fmt::arg("markup_end", ErrMarkupEnd));
        }
    }

    switch (tracker.scrapeState)
    {
    case TR_TRACKER_INACTIVE:
        break;

    case TR_TRACKER_WAITING:
        gstr << '\n';
        gstr << dir_mark;
        gstr << fmt::format(
            fmt::runtime(_("Asking for peer counts in {time_span_from_now}")),
            fmt::arg("time_span_from_now", tr_format_time_relative(now, tracker.nextScrapeTime)));
        break;

    case TR_TRACKER_QUEUED:
        gstr << '\n';
        gstr << dir_mark;
        gstr << _("Queued to ask for peer counts");
        break;

    case TR_TRACKER_ACTIVE:
        gstr << '\n';
        gstr << dir_mark;
        gstr << fmt::format(
            fmt::runtime(_("Asked for peer counts {markup_begin}{time_span_ago}{markup_end}")),
            fmt::arg("markup_begin", "<small>"),
            fmt::arg("time_span_ago", tr_format_time_relative(now, tracker.lastScrapeStartTime)),
            fmt::arg("markup_end", "</small>"));
        break;

    default:
        g_assert_not_reached();
    }
}

void buildTrackerSummary(
    std::ostream& gstr,
    std::string const& key,
    tr_tracker_view const& tracker,
    bool showScrape,
    Gtk::TextDirection direction)
{
    // hostname
    gstr << text_dir_mark.at(static_cast<int>(direction));
    gstr << (tracker.isBackup ? "<i>" : "<b>");
    gstr << Glib::Markup::escape_text(
        !key.empty() ? fmt::format("{:s} - {:s}", tracker.host_and_port, key) : tracker.host_and_port);
    gstr << (tracker.isBackup ? "</i>" : "</b>");

    if (!tracker.isBackup)
    {
        time_t const now = time(nullptr);

        appendAnnounceInfo(tracker, now, direction, gstr);

        if (showScrape)
        {
            appendScrapeInfo(tracker, now, direction, gstr);
        }
    }
}

class TrackerModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    TrackerModelColumns() noexcept
    {
        add(torrent_id);
        add(text);
        add(is_backup);
        add(tracker_id);
        add(favicon);
        add(was_updated);
        add(key);
    }

    Gtk::TreeModelColumn<tr_torrent_id_t> torrent_id;
    Gtk::TreeModelColumn<Glib::ustring> text;
    Gtk::TreeModelColumn<bool> is_backup;
    Gtk::TreeModelColumn<int> tracker_id;
    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> favicon;
    Gtk::TreeModelColumn<bool> was_updated;
    Gtk::TreeModelColumn<std::string> key;
};

TrackerModelColumns const tracker_cols;

} // namespace

bool DetailsDialog::Impl::trackerVisibleFunc(Gtk::TreeModel::const_iterator const& iter)
{
    /* show all */
    if (all_check_->get_active())
    {
        return true;
    }

    /* don't show the backups... */
    return !iter->get_value(tracker_cols.is_backup);
}

tr_torrent_id_t DetailsDialog::Impl::tracker_list_get_current_torrent_id() const
{
    // if there's only one torrent in the dialog, always use it
    if (ids_.size() == 1)
    {
        return ids_.front();
    }

    // otherwise, use the selected tracker's torrent
    auto const sel = tracker_view_->get_selection();
    if (auto const iter = sel->get_selected(); iter)
    {
        return iter->get_value(tracker_cols.torrent_id);
    }

    return -1;
}

tr_torrent* DetailsDialog::Impl::tracker_list_get_current_torrent() const
{
    return core_->find_torrent(tracker_list_get_current_torrent_id());
}

namespace
{

void favicon_ready_cb(Glib::RefPtr<Gdk::Pixbuf> const* pixbuf, Gtk::TreeRowReference& reference)
{
    if (pixbuf != nullptr && *pixbuf != nullptr)
    {
        auto const path = reference.get_path();
        auto const model = reference.get_model();

        if (auto const iter = model->get_iter(path); iter)
        {
            (*iter)[tracker_cols.favicon] = *pixbuf;
        }
    }
}

} // namespace

void DetailsDialog::Impl::refreshTracker(std::vector<tr_torrent*> const& torrents)
{
    std::ostringstream gstr;
    auto& hash = tracker_hash_;
    auto const& store = tracker_store_;
    bool const showScrape = scrape_check_->get_active();

    /* step 1: get all the trackers */
    auto trackers = std::multimap<tr_torrent const*, tr_tracker_view>{};
    for (auto const* tor : torrents)
    {
        for (size_t i = 0, n = tr_torrentTrackerCount(tor); i < n; ++i)
        {
            trackers.emplace(tor, tr_torrentTracker(tor, i));
        }
    }

    /* step 2: mark all the trackers in the list as not-updated */
    for (auto& row : store->children())
    {
        row[tracker_cols.was_updated] = false;
    }

    /* step 3: add / update trackers */
    for (auto const& [tor, tracker] : trackers)
    {
        auto const torrent_id = tr_torrentId(tor);

        // build the key to find the row
        gstr.str({});
        gstr << torrent_id << '\t' << tracker.tier << '\t' << tracker.announce;
        if (!hash.contains(gstr.str()))
        {
            // if we didn't have that row, add it
            auto const iter = store->append();
            (*iter)[tracker_cols.torrent_id] = torrent_id;
            (*iter)[tracker_cols.tracker_id] = static_cast<int>(tracker.id);
            (*iter)[tracker_cols.key] = gstr.str();

            auto const p = store->get_path(iter);
            hash.try_emplace(gstr.str(), Gtk::TreeRowReference(store, p));
            core_->favicon_cache().load(
                tracker.announce,
                [ref = Gtk::TreeRowReference(store, p)](auto const* pixbuf_refptr) mutable
                { favicon_ready_cb(pixbuf_refptr, ref); });
        }
    }

    /* step 4: update the rows */
    auto const summary_name = std::size(torrents) == 1 ? tr_torrentName(torrents.front()) : ""s;
    for (auto const& [tor, tracker] : trackers)
    {
        auto const torrent_id = tr_torrentId(tor);

        // build the key to find the row
        gstr.str({});
        gstr << torrent_id << '\t' << tracker.tier << '\t' << tracker.announce;
        auto const iter = store->get_iter(hash.at(gstr.str()).get_path());

        // update the row
        gstr.str({});
        buildTrackerSummary(gstr, summary_name, tracker, showScrape, dialog_.get_direction());
        (*iter)[tracker_cols.text] = gstr.str();
        (*iter)[tracker_cols.is_backup] = tracker.isBackup;
        (*iter)[tracker_cols.tracker_id] = static_cast<int>(tracker.id);
        (*iter)[tracker_cols.was_updated] = true;
    }

    /* step 5: remove trackers that have disappeared */
    if (auto iter = store->children().begin(); iter)
    {
        while (iter)
        {
            if (iter->get_value(tracker_cols.was_updated))
            {
                ++iter;
            }
            else
            {
                auto const key = iter->get_value(tracker_cols.key);
                hash.erase(key);
                iter = store->erase(iter);
            }
        }
    }

    edit_trackers_button_->set_sensitive(tracker_list_get_current_torrent_id() > 0);
}

void DetailsDialog::Impl::onScrapeToggled()
{
    tr_quark const key = TR_KEY_show_tracker_scrapes;
    bool const value = scrape_check_->get_active();
    core_->set_pref(key, value);
    refresh();
}

void DetailsDialog::Impl::onBackupToggled()
{
    tr_quark const key = TR_KEY_show_backup_trackers;
    bool const value = all_check_->get_active();
    core_->set_pref(key, value);
    refresh();
}

namespace
{

class EditTrackersDialog : public Gtk::Widget
{
public:
    EditTrackersDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr_torrent const* torrent);
    EditTrackersDialog(EditTrackersDialog&&) = delete;
    EditTrackersDialog(EditTrackersDialog const&) = delete;
    EditTrackersDialog& operator=(EditTrackersDialog&&) = delete;
    EditTrackersDialog& operator=(EditTrackersDialog const&) = delete;
    ~EditTrackersDialog() override = default;

    static std::unique_ptr<EditTrackersDialog> create(
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr_torrent const* tor);

private:
    void onAccept();

private:
    Glib::RefPtr<Gtk::Builder> const ui_builder_;
    DetailsDialog& parent_;
    Glib::RefPtr<Session> const core_;
    tr_torrent_id_t const torrent_id_;
    Gtk::TextView* const urls_view_;
};

EditTrackersDialog::EditTrackersDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr_torrent const* torrent)
    : Gtk::Widget(cast_item)
    , ui_builder_(builder)
    , parent_(parent)
    , core_(core)
    , torrent_id_(tr_torrentId(torrent))
    , urls_view_(gtr_get_widget<Gtk::TextView>(builder, "urls_view"))
{
    auto const title = fmt::format(
        fmt::runtime(_("{torrent_name} - Edit Trackers")),
        fmt::arg("torrent_name", tr_torrentName(torrent)));
    adw_dialog_set_title(ADW_DIALOG(gobj()), title.c_str());
    urls_view_->get_buffer()->set_text(tr_torrentGetTrackerList(torrent));

    gtr_get_widget<Gtk::Button>(builder, "save_button")->signal_clicked().connect([this]() { onAccept(); });
    gtr_get_widget<Gtk::Button>(builder, "cancel_button")
        ->signal_clicked()
        .connect([this]() { adw_dialog_close(ADW_DIALOG(gobj())); });
}

std::unique_ptr<EditTrackersDialog> EditTrackersDialog::create(
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr_torrent const* torrent)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("EditTrackersDialog.ui"));
    return std::unique_ptr<EditTrackersDialog>(
        gtr_get_widget_derived<EditTrackersDialog>(builder, "EditTrackersDialog", parent, core, torrent));
}

void EditTrackersDialog::onAccept()
{
    bool do_destroy = true;
    auto const text_buffer = urls_view_->get_buffer();
    if (auto* const tor = core_->find_torrent(torrent_id_); tor != nullptr)
    {
        if (tr_torrentSetTrackerList(tor, text_buffer->get_text(false).raw()))
        {
            parent_.refresh();
        }
        else
        {
            gtr_show_error(*this, _("List contains invalid URLs"), _("Please correct the errors and try again."));
            do_destroy = false;
        }
    }

    if (do_destroy)
    {
        adw_dialog_close(ADW_DIALOG(gobj()));
    }
}

} // namespace

void DetailsDialog::Impl::on_edit_trackers()
{
    if (auto const* const tor = tracker_list_get_current_torrent(); tor != nullptr)
    {
        auto d = std::shared_ptr<EditTrackersDialog>(EditTrackersDialog::create(dialog_, core_, tor));
        adw_compat_dialog_on_closed(GTK_WIDGET(d->gobj()), [d]() mutable { d.reset(); });
        adw_compat_dialog_present(GTK_WIDGET(d->gobj()), GTK_WIDGET(dialog_.gobj()));
    }
}

void DetailsDialog::Impl::on_tracker_list_selection_changed()
{
    int const n = tracker_view_->get_selection()->count_selected_rows();
    auto const* const tor = tracker_list_get_current_torrent();

    remove_tracker_button_->set_sensitive(n > 0);
    add_tracker_button_->set_sensitive(tor != nullptr);
    edit_trackers_button_->set_sensitive(tor != nullptr);
}

namespace
{

class AddTrackerDialog : public Gtk::Widget
{
public:
    AddTrackerDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr_torrent const* torrent);
    AddTrackerDialog(AddTrackerDialog&&) = delete;
    AddTrackerDialog(AddTrackerDialog const&) = delete;
    AddTrackerDialog& operator=(AddTrackerDialog&&) = delete;
    AddTrackerDialog& operator=(AddTrackerDialog const&) = delete;
    ~AddTrackerDialog() override = default;

    static std::unique_ptr<AddTrackerDialog> create(
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr_torrent const* tor);

private:
    void onAccept();

private:
    Glib::RefPtr<Gtk::Builder> const ui_builder_;
    DetailsDialog& parent_;
    Glib::RefPtr<Session> const core_;
    tr_torrent_id_t const torrent_id_;
    Gtk::Entry* const url_entry_;
};

AddTrackerDialog::AddTrackerDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr_torrent const* torrent)
    : Gtk::Widget(cast_item)
    , ui_builder_(builder)
    , parent_(parent)
    , core_(core)
    , torrent_id_(tr_torrentId(torrent))
    , url_entry_(gtr_get_widget<Gtk::Entry>(builder, "url_entry"))
{
    auto const title = fmt::format(
        fmt::runtime(_("{torrent_name} - Add Tracker")),
        fmt::arg("torrent_name", tr_torrentName(torrent)));
    adw_dialog_set_title(ADW_DIALOG(gobj()), title.c_str());
    adw_dialog_set_default_widget(ADW_DIALOG(gobj()), gtr_get_widget<Gtk::Widget>(builder, "open_button")->gobj());
    gtr_paste_clipboard_url_into_entry(*url_entry_);

    gtr_get_widget<Gtk::Button>(builder, "open_button")->signal_clicked().connect([this]() { onAccept(); });
    gtr_get_widget<Gtk::Button>(builder, "cancel_button")
        ->signal_clicked()
        .connect([this]() { adw_dialog_close(ADW_DIALOG(gobj())); });
}

std::unique_ptr<AddTrackerDialog> AddTrackerDialog::create(
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr_torrent const* torrent)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("AddTrackerDialog.ui"));
    return std::unique_ptr<AddTrackerDialog>(
        gtr_get_widget_derived<AddTrackerDialog>(builder, "AddTrackerDialog", parent, core, torrent));
}

void AddTrackerDialog::onAccept()
{
    bool destroy = true;
    auto const url = gtr_str_strip(url_entry_->get_text());
    if (!url.empty())
    {
        if (tr_urlIsValidTracker(url.c_str()))
        {
            // TODO(ckerr) migrate to `TR_KEY_tracker_list`
            auto params = tr_variant::Map{ 2U };
            params.try_emplace(TR_KEY_ids, Session::to_variant({ torrent_id_ }));
            params.try_emplace(TR_KEY_tracker_add, Session::to_variant({ url.raw() }));
            core_->exec(TR_KEY_torrent_set, std::move(params));
            parent_.refresh();
        }
        else
        {
            gtr_unrecognized_url_dialog(*this, url);
            destroy = false;
        }
    }

    if (destroy)
    {
        adw_dialog_close(ADW_DIALOG(gobj()));
    }
}

} // namespace

void DetailsDialog::Impl::on_tracker_list_add_button_clicked()
{
    if (auto const* const tor = tracker_list_get_current_torrent(); tor != nullptr)
    {
        auto d = std::shared_ptr<AddTrackerDialog>(AddTrackerDialog::create(dialog_, core_, tor));
        adw_compat_dialog_on_closed(GTK_WIDGET(d->gobj()), [d]() mutable { d.reset(); });
        adw_compat_dialog_present(GTK_WIDGET(d->gobj()), GTK_WIDGET(dialog_.gobj()));
    }
}

void DetailsDialog::Impl::on_tracker_list_remove_button_clicked()
{
    auto* v = tracker_view_;
    auto sel = v->get_selection();

    if (auto const iter = sel->get_selected(); iter)
    {
        auto const torrent_id = iter->get_value(tracker_cols.torrent_id);
        auto const tracker_id = iter->get_value(tracker_cols.tracker_id);

        // TODO(ckerr): migrate to `TR_KEY_tracker_list`
        auto params = tr_variant::Map{ 2U };
        params.try_emplace(TR_KEY_ids, Session::to_variant({ torrent_id }));
        params.try_emplace(TR_KEY_tracker_remove, Session::to_variant({ tracker_id }));
        core_->exec(TR_KEY_torrent_set, std::move(params));
        refresh();
    }
}

void DetailsDialog::Impl::tracker_page_init(Glib::RefPtr<Gtk::Builder> const& /*builder*/)
{
    int const pad = (GUI_PAD + GUI_PAD_BIG) / 2;

    tracker_store_ = Gtk::ListStore::create(tracker_cols);

    trackers_filtered_ = Gtk::TreeModelFilter::create(tracker_store_);
    trackers_filtered_->set_visible_func(sigc::mem_fun(*this, &Impl::trackerVisibleFunc));

    tracker_view_->set_model(trackers_filtered_);
    setup_item_view_button_event_handling(
        *tracker_view_,
        [this](guint /*button*/, TrGdkModifierType /*state*/, double view_x, double view_y, bool context_menu_requested)
        { return on_item_view_button_pressed(*tracker_view_, view_x, view_y, context_menu_requested); },
        [this](double view_x, double view_y) { return on_item_view_button_released(*tracker_view_, view_x, view_y); });

    auto sel = tracker_view_->get_selection();
    sel->signal_changed().connect(sigc::mem_fun(*this, &Impl::on_tracker_list_selection_changed));

    auto* c = Gtk::make_managed<Gtk::TreeViewColumn>();
    c->set_title(_("Trackers"));
    tracker_view_->append_column(*c);

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        r->property_width() = 20 + (GUI_PAD_SMALL * 2);
        r->property_xpad() = GUI_PAD_SMALL;
        r->property_ypad() = pad;
        r->property_yalign() = 0.0F;
        c->pack_start(*r, false);
        c->add_attribute(r->property_pixbuf(), tracker_cols.favicon);
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        r->property_ellipsize() = Pango::EllipsizeMode::END;
        r->property_xpad() = GUI_PAD_SMALL;
        r->property_ypad() = pad;
        c->pack_start(*r, true);
        c->add_attribute(r->property_markup(), tracker_cols.text);
    }

    add_tracker_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_tracker_list_add_button_clicked));
    edit_trackers_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_edit_trackers));
    remove_tracker_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_tracker_list_remove_button_clicked));

    scrape_check_->set_active(gtr_pref_flag_get(TR_KEY_show_tracker_scrapes));
    scrape_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onScrapeToggled));

    all_check_->set_active(gtr_pref_flag_get(TR_KEY_show_backup_trackers));
    all_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onBackupToggled));
}
