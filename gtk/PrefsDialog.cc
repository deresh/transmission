// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "PrefsDialog.h"

#include "AdwCompat.h"
#include "FreeSpaceLabel.h"
#include "GtkCompat.h"
#include "PathButton.h"
#include "Prefs.h"
#include "Session.h"
#include "Utils.h"

#include <libtransmission/transmission.h>
#include <libtransmission/web-utils.h>

#include <glibmm/date.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/timer.h>
#include <glibmm/ustring.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/combobox.h>
#include <gtkmm/editable.h>
#include <gtkmm/entry.h>
#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/textview.h>
#include <gtkmm/treemodelcolumn.h>
#include <gtkmm/widget.h>

#include <fmt/format.h>

#include <array>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

using namespace tr::Values;

/**
***
**/

class PrefsDialog::Impl
{
public:
    Impl(PrefsDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl() = default;

private:
private:
    PrefsDialog& dialog_;
    Glib::RefPtr<Session> const core_;
};

/**
***
**/

namespace
{

class PageBase : public Gtk::Widget
{
public:
    PageBase(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    PageBase(PageBase&&) = delete;
    PageBase(PageBase const&) = delete;
    PageBase& operator=(PageBase&&) = delete;
    PageBase& operator=(PageBase const&) = delete;
    ~PageBase() override;

    Gtk::CheckButton* init_check_button(Glib::ustring const& name, tr_quark key);
    GtkWidget* init_switch_row(Glib::ustring const& name, tr_quark key);

    // Titles of rows may contain {placeholders}, filled in at runtime.
    template<typename... ArgTs>
    static void localize_row_title(GtkWidget* row, ArgTs&&... args)
    {
        auto const title = fmt::format(
            fmt::runtime(adw_preferences_row_get_title(ADW_PREFERENCES_ROW(row))),
            std::forward<ArgTs>(args)...);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title.c_str());
    }

    template<typename T>
    GtkWidget* init_spin_row(Glib::ustring const& name, tr_quark const key, T const low, T const high, T const step)
    {
        auto* const row = get_widget<Gtk::Widget>(name)->gobj();
        auto const adjustment = Gtk::Adjustment::create(
            static_cast<double>(gtr_pref_get<T>(key)),
            static_cast<double>(low),
            static_cast<double>(high),
            static_cast<double>(step));
        adw_spin_row_set_adjustment(ADW_SPIN_ROW(row), adjustment->gobj());
        adw_spin_row_set_digits(ADW_SPIN_ROW(row), std::is_floating_point_v<T> ? 2 : 0);
        // The row owns the adjustment; keep a raw pointer so the handler does not hold a reference cycle.
        auto* const raw = adjustment.get();
        raw->signal_value_changed().connect([raw, key]() { gtr_pref_set(key, static_cast<T>(raw->get_value())); });
        return row;
    }
    Gtk::Entry* init_entry(Glib::ustring const& name, tr_quark key);
    Gtk::TextView* init_text_view(Glib::ustring const& name, tr_quark key);
    PathButton* init_chooser_button(Glib::ustring const& name, tr_quark key);
    GtkWidget* init_encryption_row(Glib::ustring const& name, tr_quark key);
    GtkWidget* init_choice_row(
        Glib::ustring const& name,
        tr_quark key,
        std::vector<std::pair<std::string, int>> const& items,
        int current);
    GtkWidget* init_time_row(Glib::ustring const& name, tr_quark key);
    GtkWidget* init_week_row(Glib::ustring const& name, tr_quark key);

    template<typename T>
    Gtk::SpinButton* init_spin_button(Glib::ustring const& name, tr_quark const key, T const low, T const high, T const step)
    {
        auto* button = get_widget<Gtk::SpinButton>(name);
        button->set_adjustment(
            Gtk::Adjustment::create(
                static_cast<double>(gtr_pref_get<T>(key)),
                static_cast<double>(low),
                static_cast<double>(high),
                static_cast<double>(step)));
        button->set_digits(std::is_floating_point_v<T> ? 2 : 0);
        button->signal_value_changed().connect([this, button, key]() { spun_cb<T>(*button, key); });
        return button;
    }

    template<typename T>
    T* get_widget(Glib::ustring const& name) const
    {
        return gtr_get_widget<T>(builder_, name);
    }

    template<typename T, typename... ArgTs>
    T* get_widget_derived(Glib::ustring const& name, ArgTs&&... args) const
    {
        return gtr_get_widget_derived<T>(builder_, name, std::forward<ArgTs>(args)...);
    }

    template<typename T, typename... ArgTs>
    static void localize_label(T& widget, ArgTs&&... args)
    {
        widget.set_label(fmt::format(fmt::runtime(widget.get_label().raw()), std::forward<ArgTs>(args)...));
    }

private:
    template<typename T>
    bool spun_cb_idle(Gtk::SpinButton const& spin, tr_quark const key)
    {
        // How long the spin button must be idle before we push the value to core_.
        // Prevents flooding the core with intermediate values while the user spins.
        static constexpr auto SpinIdleThresholdSec = 0.33;

        auto const last_change_it = spin_timers_.find(key);
        g_return_val_if_fail(last_change_it != spin_timers_.end(), false);

        // if the user is still making changes, then do nothing yet
        if (last_change_it->second.first->elapsed() < SpinIdleThresholdSec)
        {
            return true;
        }

        // update the core
        core_->set_pref(key, static_cast<T>(spin.get_value()));

        // cleanup
        spin_timers_.erase(last_change_it);
        return false;
    }

    template<typename T>
    void spun_cb(Gtk::SpinButton& w, tr_quark const key)
    {
        // user may be spinning through many values, so let's hold off
        // for a moment to keep from flooding the core with changes
        auto last_change_it = spin_timers_.find(key);
        if (last_change_it == spin_timers_.end())
        {
            auto timeout_tag = Glib::signal_timeout().connect_seconds([this, &w, key]() { return spun_cb_idle<T>(w, key); }, 1);
            last_change_it = spin_timers_.emplace(key, std::pair(std::make_unique<Glib::Timer>(), timeout_tag)).first;
        }

        last_change_it->second.first->start();
    }

    void entry_changed_cb(Gtk::Entry& w, tr_quark key);

    void chosen_cb(PathButton& w, tr_quark key);

    void onIntComboChanged(Gtk::ComboBox& combo_box, tr_quark key);

    static auto get_weekday_string(Glib::Date::Weekday weekday);

private:
    Glib::RefPtr<Gtk::Builder> const builder_;
    Glib::RefPtr<Session> const core_;

    std::map<tr_quark, std::pair<std::unique_ptr<Glib::Timer>, sigc::connection>> spin_timers_;
};

PageBase::PageBase(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core)
    : Gtk::Widget(cast_item)
    , builder_(builder)
    , core_(core)
{
}

PageBase::~PageBase()
{
    for (auto& [key, info] : spin_timers_)
    {
        info.second.disconnect();
    }
}

GtkWidget* PageBase::init_switch_row(Glib::ustring const& name, tr_quark const key)
{
    auto* const row = get_widget<Gtk::Widget>(name)->gobj();
    adw_switch_row_set_active(ADW_SWITCH_ROW(row), gtr_pref_flag_get(key));
    adw_compat_connect_notify(
        row,
        "notify::active",
        [row, key]() { gtr_pref_flag_set(key, adw_switch_row_get_active(ADW_SWITCH_ROW(row))); });
    return row;
}

Gtk::CheckButton* PageBase::init_check_button(Glib::ustring const& name, tr_quark const key)
{
    auto* button = get_widget<Gtk::CheckButton>(name);
    button->set_active(gtr_pref_flag_get(key));
    button->signal_toggled().connect([this, button, key]() { core_->set_pref(key, button->get_active()); });
    return button;
}

void PageBase::entry_changed_cb(Gtk::Entry& w, tr_quark const key)
{
    core_->set_pref(key, w.get_text());
}

Gtk::Entry* PageBase::init_entry(Glib::ustring const& name, tr_quark const key)
{
    auto* const entry = get_widget<Gtk::Entry>(name);

    if (auto const value = gtr_pref_string_get(key); !value.empty())
    {
        entry->set_text(value);
    }

    entry->signal_changed().connect([this, entry, key]() { entry_changed_cb(*entry, key); });
    return entry;
}

Gtk::TextView* PageBase::init_text_view(Glib::ustring const& name, tr_quark const key)
{
    auto* const view = get_widget<Gtk::TextView>(name);

    auto buffer = view->get_buffer();
    buffer->set_text(gtr_pref_string_get(key));

    auto const save_buffer = [this, buffer, key]()
    {
        core_->set_pref(key, buffer->get_text());
    };

    auto focus_controller = Gtk::EventControllerFocus::create();
    focus_controller->signal_leave().connect(save_buffer);
    view->add_controller(focus_controller);

    return view;
}

void PageBase::chosen_cb(PathButton& w, tr_quark const key)
{
    core_->set_pref(key, w.get_filename());
}

PathButton* PageBase::init_chooser_button(Glib::ustring const& name, tr_quark const key)
{
    auto* const button = get_widget_derived<PathButton>(name);

    if (auto const path = gtr_pref_string_get(key); !path.empty())
    {
        button->set_filename(path);
    }

    button->signal_selection_changed().connect([this, button, key]() { chosen_cb(*button, key); });
    return button;
}

void PageBase::onIntComboChanged(Gtk::ComboBox& combo_box, tr_quark const key)
{
    core_->set_pref(key, static_cast<uint32_t>(gtr_combo_box_get_active_enum(combo_box)));
}

GtkWidget* PageBase::init_choice_row(
    Glib::ustring const& name,
    tr_quark const key,
    std::vector<std::pair<std::string, int>> const& items,
    int const current)
{
    std::vector<std::string> titles;
    for (auto const& item : items)
    {
        titles.push_back(item.first);
    }

    auto* const row = get_widget<Gtk::Widget>(name)->gobj();
    adw_compat_combo_row_set_strings(row, titles);

    auto selected = guint{ 0 };
    for (auto i = size_t{ 0 }; i < items.size(); ++i)
    {
        if (items[i].second == current)
        {
            selected = static_cast<guint>(i);
        }
    }
    adw_combo_row_set_selected(ADW_COMBO_ROW(row), selected);

    adw_compat_connect_notify(
        row,
        "notify::selected",
        [core = core_, row, key, items]()
        {
            auto const selected = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
            if (selected < items.size())
            {
                core->set_pref(key, static_cast<uint32_t>(items[selected].second));
            }
        });
    return row;
}

GtkWidget* PageBase::init_encryption_row(Glib::ustring const& name, tr_quark const key)
{
    auto const mode = gtr_pref_get<tr_encryption_mode>(key, TR_ENCRYPTION_PREFERRED);
    return init_choice_row(
        name,
        key,
        {
            { _("Allow encryption"), TR_CLEAR_PREFERRED },
            { _("Prefer encryption"), TR_ENCRYPTION_PREFERRED },
            { _("Require encryption"), TR_ENCRYPTION_REQUIRED },
        },
        static_cast<int>(mode));
}

GtkWidget* PageBase::init_time_row(Glib::ustring const& name, tr_quark const key)
{
    /* one entry every 15 minutes; the selected index is the offset divided by 15 */
    std::vector<std::string> titles;
    for (int i = 0; i < 60 * 24; i += 15)
    {
        titles.push_back(fmt::format("{:02}:{:02}", i / 60, i % 60));
    }

    auto* const row = get_widget<Gtk::Widget>(name)->gobj();
    adw_compat_combo_row_set_strings(row, titles);
    adw_combo_row_set_selected(ADW_COMBO_ROW(row), static_cast<guint>(gtr_pref_int_get<int>(key) / 15));
    adw_compat_connect_notify(
        row,
        "notify::selected",
        [core = core_, row, key]()
        {
            auto const selected = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
            if (selected != GTK_INVALID_LIST_POSITION)
            {
                core->set_pref(key, static_cast<uint32_t>(selected) * 15U);
            }
        });
    return row;
}

auto PageBase::get_weekday_string(Glib::Date::Weekday weekday)
{
    auto date = Glib::Date{};
    date.set_time_current();
    date.add_days(static_cast<int>(weekday) - static_cast<int>(date.get_weekday()));
    return date.format_string("%A");
}

GtkWidget* PageBase::init_week_row(Glib::ustring const& name, tr_quark const key)
{
    return init_choice_row(
        name,
        key,
        {
            { _("Every Day"), TR_SCHED_ALL },
            { _("Weekdays"), TR_SCHED_WEEKDAY },
            { _("Weekends"), TR_SCHED_WEEKEND },
            { get_weekday_string(Glib::Date::Weekday::MONDAY), TR_SCHED_MON },
            { get_weekday_string(Glib::Date::Weekday::TUESDAY), TR_SCHED_TUES },
            { get_weekday_string(Glib::Date::Weekday::WEDNESDAY), TR_SCHED_WED },
            { get_weekday_string(Glib::Date::Weekday::THURSDAY), TR_SCHED_THURS },
            { get_weekday_string(Glib::Date::Weekday::FRIDAY), TR_SCHED_FRI },
            { get_weekday_string(Glib::Date::Weekday::SATURDAY), TR_SCHED_SAT },
            { get_weekday_string(Glib::Date::Weekday::SUNDAY), TR_SCHED_SUN },
        },
        gtr_pref_int_get<int>(key));
}

/****
*****  Download Tab
****/

class DownloadingPage : public PageBase
{
public:
    DownloadingPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    DownloadingPage(DownloadingPage&&) = delete;
    DownloadingPage(DownloadingPage const&) = delete;
    DownloadingPage& operator=(DownloadingPage&&) = delete;
    DownloadingPage& operator=(DownloadingPage const&) = delete;
    ~DownloadingPage() override;

private:
    void on_core_prefs_changed(tr_quark key);

private:
    Glib::RefPtr<Session> const core_;

    FreeSpaceLabel* freespace_label_ = nullptr;

    sigc::connection core_prefs_tag_;
};

void DownloadingPage::on_core_prefs_changed(tr_quark const key)
{
    if (key == TR_KEY_download_dir)
    {
        std::string const download_dir = tr_sessionGetDownloadDir(core_->get_session());
        freespace_label_->set_dir(download_dir);
    }
}

DownloadingPage::~DownloadingPage()
{
    core_prefs_tag_.disconnect();
}

DownloadingPage::DownloadingPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : PageBase(cast_item, builder, core)
    , core_(core)
    , freespace_label_(get_widget_derived<FreeSpaceLabel>("download_dir_stats_label", core))
{
    core_prefs_tag_ = core_->signal_prefs_changed().connect(sigc::mem_fun(*this, &DownloadingPage::on_core_prefs_changed));

    init_switch_row("watch_dir_check", TR_KEY_watch_dir_enabled);
    init_chooser_button("watch_dir_chooser", TR_KEY_watch_dir);
    init_switch_row("show_options_dialog_check", TR_KEY_show_options_window);
    init_switch_row("start_on_add_check", TR_KEY_start_added_torrents);
    init_switch_row("trash_on_add_check", TR_KEY_trash_original_torrent_files);
    init_chooser_button("download_dir_chooser", TR_KEY_download_dir);
    init_spin_row<uint16_t>(
        "max_active_downloads_spin",
        TR_KEY_download_queue_size,
        0,
        std::numeric_limits<uint16_t>::max(),
        1);
    init_spin_row<uint16_t>(
        "max_inactive_time_spin",
        TR_KEY_queue_stalled_minutes,
        1,
        std::numeric_limits<uint16_t>::max(),
        15);
    init_switch_row("append_suffix_to_incomplete_check", TR_KEY_rename_partial_files);
    init_switch_row("incomplete_dir_check", TR_KEY_incomplete_dir_enabled);
    init_chooser_button("incomplete_dir_chooser", TR_KEY_incomplete_dir);
    init_switch_row("download_done_script_check", TR_KEY_script_torrent_done_enabled);
    init_chooser_button("download_done_script_chooser", TR_KEY_script_torrent_done_filename);

    on_core_prefs_changed(TR_KEY_download_dir);
}

/****
*****  Torrent Tab
****/

class SeedingPage : public PageBase
{
public:
    SeedingPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    SeedingPage(SeedingPage&&) = delete;
    SeedingPage(SeedingPage const&) = delete;
    SeedingPage& operator=(SeedingPage&&) = delete;
    SeedingPage& operator=(SeedingPage const&) = delete;
    ~SeedingPage() override = default;
};

SeedingPage::SeedingPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : PageBase(cast_item, builder, core)
{
    /* each limit value is only used while its switch is on */
    auto* const ratio_switch = init_switch_row("stop_seeding_ratio_check", TR_KEY_seed_ratio_limited);
    auto* const ratio_spin = init_spin_row<double>("stop_seeding_ratio_spin", TR_KEY_seed_ratio_limit, 0, 1000, 0.05);
    g_object_bind_property(ratio_switch, "active", ratio_spin, "sensitive", G_BINDING_SYNC_CREATE);

    auto* const timeout_switch = init_switch_row("stop_seeding_timeout_check", TR_KEY_idle_seeding_limit_enabled);
    auto* const timeout_spin = init_spin_row<uint16_t>("stop_seeding_timeout_spin", TR_KEY_idle_seeding_limit, 1, 40320, 5);
    g_object_bind_property(timeout_switch, "active", timeout_spin, "sensitive", G_BINDING_SYNC_CREATE);

    init_switch_row("seeding_done_script_check", TR_KEY_script_torrent_done_seeding_enabled);
    init_chooser_button("seeding_done_script_chooser", TR_KEY_script_torrent_done_seeding_filename);
}

/****
*****  Desktop Tab
****/

class DesktopPage : public PageBase
{
public:
    DesktopPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    DesktopPage(DesktopPage&&) = delete;
    DesktopPage(DesktopPage const&) = delete;
    DesktopPage& operator=(DesktopPage&&) = delete;
    DesktopPage& operator=(DesktopPage const&) = delete;
    ~DesktopPage() override = default;
};

DesktopPage::DesktopPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : PageBase(cast_item, builder, core)
{
    init_check_button("inhibit_hibernation_check", TR_KEY_inhibit_desktop_hibernation);

    /* no tray implementation yet: keep the option out of the dialog */
    get_widget<Gtk::CheckButton>("show_systray_icon_check")->hide();

    init_check_button("notify_on_torrent_add_check", TR_KEY_torrent_added_notification_enabled);
    init_check_button("notify_on_torrent_finish_check", TR_KEY_torrent_complete_notification_enabled);
    init_check_button("ding_no_torrent_finish_check", TR_KEY_torrent_complete_sound_enabled);
}

/****
*****  Peer Tab
****/

class PrivacyPage : public PageBase
{
    static auto const BlocklistUpdateResultDisplayTimeoutInSeconds = 3U;

public:
    PrivacyPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    PrivacyPage(PrivacyPage&&) = delete;
    PrivacyPage(PrivacyPage const&) = delete;
    PrivacyPage& operator=(PrivacyPage&&) = delete;
    PrivacyPage& operator=(PrivacyPage const&) = delete;
    ~PrivacyPage() override;

private:
    void updateBlocklistText();
    void onBlocklistUpdated(bool success);
    void onBlocklistUpdate();
    void on_blocklist_url_changed(Gtk::Editable* e);

private:
    Glib::RefPtr<Session> core_;

    Gtk::Button* updateBlocklistButton_ = nullptr;
    Gtk::Label* label_ = nullptr;

    sigc::connection updateBlocklistTag_;
    sigc::connection blocklist_update_result_tag_;
};

void PrivacyPage::updateBlocklistText()
{
    auto const n = tr_blocklistGetRuleCount(core_->get_session());
    auto const msg = fmt::format(
        fmt::runtime(ngettext("Blocklist has {count:L} entry", "Blocklist has {count:L} entries", n)),
        fmt::arg("count", n));
    label_->set_text(msg);
}

/* prefs dialog is being destroyed, so stop listening to blocklist updates */
PrivacyPage::~PrivacyPage()
{
    blocklist_update_result_tag_.disconnect();
    updateBlocklistTag_.disconnect();
}

/* core says the blocklist was updated */
void PrivacyPage::onBlocklistUpdated(bool success)
{
    updateBlocklistButton_->set_sensitive(true);
    label_->set_text(success ? _("Blocklist updated!") : _("Couldn't update blocklist"));

    blocklist_update_result_tag_ = Glib::signal_timeout().connect_seconds(
        sigc::bind_return(sigc::mem_fun(*this, &PrivacyPage::updateBlocklistText), false),
        BlocklistUpdateResultDisplayTimeoutInSeconds);
}

/* user pushed a button to update the blocklist */
void PrivacyPage::onBlocklistUpdate()
{
    updateBlocklistButton_->set_sensitive(false);

    label_->set_text(_("Getting new blocklist…"));
    blocklist_update_result_tag_.disconnect();

    core_->blocklist_update();
}

void PrivacyPage::on_blocklist_url_changed(Gtk::Editable* e)
{
    auto const url = e->get_chars(0, -1);
    updateBlocklistButton_->set_sensitive(tr_urlIsValid(url.c_str()));
}

PrivacyPage::PrivacyPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : PageBase(cast_item, builder, core)
    , core_(core)
    , updateBlocklistButton_(get_widget<Gtk::Button>("update_blocklist_button"))
    , label_(get_widget<Gtk::Label>("blocklist_stats_label"))
{
    init_encryption_row("encryption_mode_combo", TR_KEY_encryption);
    auto* const blocklist_switch = init_switch_row("blocklist_check", TR_KEY_blocklist_enabled);

    auto* const blocklist_url_entry = init_entry("blocklist_url_entry", TR_KEY_blocklist_url);

    updateBlocklistText();
    updateBlocklistButton_->signal_clicked().connect([this]() { onBlocklistUpdate(); });
    updateBlocklistButton_->set_sensitive(adw_switch_row_get_active(ADW_SWITCH_ROW(blocklist_switch)));
    blocklist_url_entry->signal_changed().connect([this, blocklist_url_entry]()
                                                  { on_blocklist_url_changed(blocklist_url_entry); });
    on_blocklist_url_changed(blocklist_url_entry);

    init_switch_row("blocklist_autoupdate_check", TR_KEY_blocklist_updates_enabled);

    updateBlocklistTag_ = core_->signal_blocklist_updated().connect(sigc::mem_fun(*this, &PrivacyPage::onBlocklistUpdated));
}

/****
*****  Remote Tab
****/

class RemotePage : public PageBase
{
    class WhitelistModelColumns : public Gtk::TreeModelColumnRecord
    {
    public:
        WhitelistModelColumns() noexcept
        {
            add(address);
        }

        Gtk::TreeModelColumn<Glib::ustring> address;
    };

    static WhitelistModelColumns const whitelist_cols;

public:
    RemotePage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    RemotePage(RemotePage&&) = delete;
    RemotePage(RemotePage const&) = delete;
    RemotePage& operator=(RemotePage&&) = delete;
    RemotePage& operator=(RemotePage const&) = delete;
    ~RemotePage() override = default;

private:
    void refreshWhitelist();
    void onAddressEdited(Glib::ustring const& path, Glib::ustring const& address);
    void onAddWhitelistClicked();
    void onRemoveWhitelistClicked();
    void refreshRPCSensitivity();

    static void onLaunchClutchCB();

    static Glib::RefPtr<Gtk::ListStore> whitelist_tree_model_new(std::string const& whitelist);

private:
    Glib::RefPtr<Session> core_;

    Gtk::TreeView* view_;
    Gtk::Button* remove_button_;
    Gtk::CheckButton* rpc_tb_;
    Gtk::CheckButton* auth_tb_;
    Gtk::CheckButton* whitelist_tb_;

    Glib::RefPtr<Gtk::ListStore> store_;
    std::vector<Gtk::Widget*> auth_widgets_;
    std::vector<Gtk::Widget*> whitelist_widgets_;
};

RemotePage::WhitelistModelColumns const RemotePage::whitelist_cols;

Glib::RefPtr<Gtk::ListStore> RemotePage::whitelist_tree_model_new(std::string const& whitelist)
{
    auto store = Gtk::ListStore::create(whitelist_cols);

    std::istringstream stream(whitelist);
    std::string s;

    while (std::getline(stream, s, ','))
    {
        s = gtr_str_strip(s);

        if (s.empty())
        {
            continue;
        }

        auto const iter = store->append();
        (*iter)[whitelist_cols.address] = s;
    }

    return store;
}

void RemotePage::refreshWhitelist()
{
    std::ostringstream gstr;

    for (auto const& row : store_->children())
    {
        gstr << row.get_value(whitelist_cols.address) << ",";
    }

    auto str = gstr.str();
    if (!str.empty())
    {
        str.resize(str.size() - 1); /* remove the trailing comma */
    }

    core_->set_pref(TR_KEY_rpc_whitelist, str);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void RemotePage::onAddressEdited(Glib::ustring const& path, Glib::ustring const& address)
{
    if (auto const iter = store_->get_iter(path); iter)
    {
        (*iter)[whitelist_cols.address] = address;
    }

    refreshWhitelist();
}

void RemotePage::onAddWhitelistClicked()
{
    auto const iter = store_->append();
    (*iter)[whitelist_cols.address] = "0.0.0.0";

    view_->set_cursor(store_->get_path(iter), *view_->get_column(0), true);
}

void RemotePage::onRemoveWhitelistClicked()
{
    auto const sel = view_->get_selection();

    if (auto const iter = sel->get_selected(); iter)
    {
        store_->erase(iter);
        refreshWhitelist();
    }
}

void RemotePage::refreshRPCSensitivity()
{
    bool const rpc_active = rpc_tb_->get_active();
    bool const auth_active = auth_tb_->get_active();
    bool const whitelist_active = whitelist_tb_->get_active();
    auto const sel = view_->get_selection();
    auto const have_addr = sel->get_selected();
    auto const n_rules = store_->children().size();

    for (auto* const widget : auth_widgets_)
    {
        widget->set_sensitive(rpc_active && auth_active);
    }

    for (auto* const widget : whitelist_widgets_)
    {
        widget->set_sensitive(rpc_active && whitelist_active);
    }

    remove_button_->set_sensitive(rpc_active && whitelist_active && have_addr && n_rules > 1);
}

void RemotePage::onLaunchClutchCB()
{
    gtr_open_uri(fmt::format("http://localhost:{}/", gtr_pref_int_get<uint16_t>(TR_KEY_rpc_port)));
}

RemotePage::RemotePage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core)
    : PageBase(cast_item, builder, core)
    , core_(core)
    , view_(get_widget<Gtk::TreeView>("rpc_whitelist_view"))
    , remove_button_(get_widget<Gtk::Button>("remove_from_rpc_whistlist_button"))
    , rpc_tb_(init_check_button("enable_rpc_check", TR_KEY_rpc_enabled))
    , auth_tb_(init_check_button("enable_rpc_auth_check", TR_KEY_rpc_authentication_required))
    , whitelist_tb_(init_check_button("rpc_whitelist_check", TR_KEY_rpc_whitelist_enabled))
{
    rpc_tb_->signal_toggled().connect([this]() { refreshRPCSensitivity(); });
    auto* const open_button = get_widget<Gtk::Button>("open_web_client_button");
    open_button->signal_clicked().connect(&RemotePage::onLaunchClutchCB);

    init_spin_button<uint16_t>("rpc_port_spin", TR_KEY_rpc_port, 0, std::numeric_limits<uint16_t>::max(), 1);

    auth_tb_->signal_toggled().connect([this]() { refreshRPCSensitivity(); });

    auto* const username_entry = init_entry("rpc_username_entry", TR_KEY_rpc_username);
    auth_widgets_.push_back(username_entry);
    auth_widgets_.push_back(get_widget<Gtk::Label>("rpc_username_label"));

    auto* const password_entry = init_entry("rpc_password_entry", TR_KEY_rpc_password);
    auth_widgets_.push_back(password_entry);
    auth_widgets_.push_back(get_widget<Gtk::Label>("rpc_password_label"));

    whitelist_tb_->signal_toggled().connect([this]() { refreshRPCSensitivity(); });

    {
        store_ = whitelist_tree_model_new(gtr_pref_string_get(TR_KEY_rpc_whitelist));

        view_->set_model(store_);
        setup_item_view_button_event_handling(
            *view_,
            {},
            [this](double view_x, double view_y) { return on_item_view_button_released(*view_, view_x, view_y); });

        whitelist_widgets_.push_back(view_);
        auto const sel = view_->get_selection();
        sel->signal_changed().connect([this]() { refreshRPCSensitivity(); });

        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        r->signal_edited().connect(sigc::mem_fun(*this, &RemotePage::onAddressEdited));
        r->property_editable() = true;
        auto* c = Gtk::make_managed<Gtk::TreeViewColumn>("", *r);
        c->add_attribute(r->property_text(), whitelist_cols.address);
        c->set_expand(true);
        view_->append_column(*c);

        whitelist_widgets_.push_back(get_widget<Gtk::Label>("rpc_whitelist_label"));

        remove_button_->signal_clicked().connect([this]() { onRemoveWhitelistClicked(); });
        refreshRPCSensitivity();
        auto* add_button = get_widget<Gtk::Button>("add_to_rpc_whitelist_button");
        whitelist_widgets_.push_back(add_button);
        add_button->signal_clicked().connect([this]() { onAddWhitelistClicked(); });
    }

    refreshRPCSensitivity();
}

/****
*****  Bandwidth Tab
****/

class SpeedPage : public PageBase
{
public:
    SpeedPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    SpeedPage(SpeedPage&&) = delete;
    SpeedPage(SpeedPage const&) = delete;
    SpeedPage& operator=(SpeedPage&&) = delete;
    SpeedPage& operator=(SpeedPage const&) = delete;
    ~SpeedPage() override = default;
};

SpeedPage::SpeedPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core)
    : PageBase(cast_item, builder, core)
{
    auto const speed_units_kbyps_str = Speed::units().display_name(Speed::Units::KByps);

    auto* const upload_switch = init_switch_row("upload_limit_check", TR_KEY_speed_limit_up_enabled);
    localize_row_title(upload_switch, fmt::arg("speed_units", speed_units_kbyps_str));
    auto* const upload_spin = init_spin_row<uint32_t>(
        "upload_limit_spin",
        TR_KEY_speed_limit_up,
        0,
        std::numeric_limits<uint32_t>::max(),
        5);
    g_object_bind_property(upload_switch, "active", upload_spin, "sensitive", G_BINDING_SYNC_CREATE);

    auto* const download_switch = init_switch_row("download_limit_check", TR_KEY_speed_limit_down_enabled);
    localize_row_title(download_switch, fmt::arg("speed_units", speed_units_kbyps_str));
    auto* const download_spin = init_spin_row<uint32_t>(
        "download_limit_spin",
        TR_KEY_speed_limit_down,
        0,
        std::numeric_limits<uint32_t>::max(),
        5);
    g_object_bind_property(download_switch, "active", download_spin, "sensitive", G_BINDING_SYNC_CREATE);

    localize_row_title(
        init_spin_row<uint32_t>("alt_upload_limit_spin", TR_KEY_alt_speed_up, 0, std::numeric_limits<uint32_t>::max(), 5),
        fmt::arg("speed_units", speed_units_kbyps_str));

    localize_row_title(
        init_spin_row<uint32_t>("alt_download_limit_spin", TR_KEY_alt_speed_down, 0, std::numeric_limits<uint32_t>::max(), 5),
        fmt::arg("speed_units", speed_units_kbyps_str));

    auto* const schedule_switch = init_switch_row("alt_schedule_time_check", TR_KEY_alt_speed_time_enabled);
    /* the schedule rows only apply while the schedule switch is on */
    for (auto* const row : { init_time_row("alt_speed_start_time_combo", TR_KEY_alt_speed_time_begin),
                             init_time_row("alt_speed_end_time_combo", TR_KEY_alt_speed_time_end),
                             init_week_row("alt_speed_days_combo", TR_KEY_alt_speed_time_day) })
    {
        g_object_bind_property(schedule_switch, "active", row, "sensitive", G_BINDING_SYNC_CREATE);
    }
}

/****
*****  Network Tab
****/

class NetworkPage : public PageBase
{
public:
    NetworkPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    NetworkPage(NetworkPage&&) = delete;
    NetworkPage(NetworkPage const&) = delete;
    NetworkPage& operator=(NetworkPage&&) = delete;
    NetworkPage& operator=(NetworkPage const&) = delete;
    ~NetworkPage() override;

private:
    enum PortTestStatus : uint8_t
    {
        PORT_TEST_UNKNOWN = 0U,
        PORT_TEST_CHECKING,
        PORT_TEST_OPEN,
        PORT_TEST_CLOSED,
        PORT_TEST_ERROR
    };

    void portTestSetSensitive();
    void updatePortStatusText();
    void onCorePrefsChanged(tr_quark key);
    void onPortTested(std::optional<bool> result, Session::PortTestIpProtocol ip_protocol);
    void onPortTest();

    static std::string_view getPortStatusText(PortTestStatus status) noexcept;

private:
    Glib::RefPtr<Session> core_;

    Gtk::Label* portLabel_ = nullptr;
    Gtk::Button* portButton_ = nullptr;
    GtkWidget* portSpin_ = nullptr;

    sigc::connection portTag_;
    sigc::connection prefsTag_;

    std::array<PortTestStatus, Session::NUM_PORT_TEST_IP_PROTOCOL> portTestStatus_ = {};
};

void NetworkPage::onCorePrefsChanged(tr_quark const key)
{
    if (key == TR_KEY_peer_port)
    {
        portTestStatus_[Session::PORT_TEST_IPV4] = PORT_TEST_UNKNOWN;
        portTestStatus_[Session::PORT_TEST_IPV6] = PORT_TEST_UNKNOWN;
        updatePortStatusText();
        portTestSetSensitive();
    }
}

NetworkPage::~NetworkPage()
{
    prefsTag_.disconnect();
    portTag_.disconnect();
}

std::string_view NetworkPage::getPortStatusText(PortTestStatus const status) noexcept
{
    switch (status)
    {
    case PORT_TEST_UNKNOWN:
        return C_("Port test status", "unknown");
    case PORT_TEST_CHECKING:
        return C_("Port test status", "checking…");
    case PORT_TEST_OPEN:
        return C_("Port test status", "open");
    case PORT_TEST_CLOSED:
        return C_("Port test status", "closed");
    case PORT_TEST_ERROR:
        return C_("Port test status", "error");
    default:
        return {};
    }
}

void NetworkPage::updatePortStatusText()
{
    auto const status_ipv4 = getPortStatusText(portTestStatus_[Session::PORT_TEST_IPV4]);
    auto const status_ipv6 = getPortStatusText(portTestStatus_[Session::PORT_TEST_IPV6]);

    portLabel_->set_markup(
        portTestStatus_[Session::PORT_TEST_IPV4] == portTestStatus_[Session::PORT_TEST_IPV6] ?
            fmt::format(fmt::runtime(_("Status: <b>{status}</b>")), fmt::arg("status", status_ipv4)) :
            fmt::format(
                fmt::runtime(_("Status: <b>{status_ipv4}</b> (IPv4), <b>{status_ipv6}</b> (IPv6)")),
                fmt::arg("status_ipv4", status_ipv4),
                fmt::arg("status_ipv6", status_ipv6)));
}

void NetworkPage::portTestSetSensitive()
{
    // Depend on the RPC call status instead of the UI status, so that the widgets
    // won't be enabled even if the port peer port changed while we have port_test
    // RPC call(s) in-flight.
    auto const sensitive = !core_->port_test_pending(Session::PORT_TEST_IPV4) &&
        !core_->port_test_pending(Session::PORT_TEST_IPV6);
    portButton_->set_sensitive(sensitive);
    gtk_widget_set_sensitive(portSpin_, sensitive);
}

void NetworkPage::onPortTested(std::optional<bool> const result, Session::PortTestIpProtocol const ip_protocol)
{
    auto constexpr ResultToStatus = [](std::optional<bool> const res)
    {
        if (!res)
        {
            return PORT_TEST_ERROR;
        }
        if (!*res)
        {
            return PORT_TEST_CLOSED;
        }
        return PORT_TEST_OPEN;
    };

    // Only update the UI if the current status is "checking", so that
    // we won't show the port test results for the old peer port if it
    // changed while we have port_test RPC call(s) in-flight.
    if (auto& status = portTestStatus_[ip_protocol]; status == PORT_TEST_CHECKING)
    {
        status = ResultToStatus(result);
        updatePortStatusText();
    }
    portTestSetSensitive();
}

void NetworkPage::onPortTest()
{
    portTestStatus_[Session::PORT_TEST_IPV4] = PORT_TEST_CHECKING;
    portTestStatus_[Session::PORT_TEST_IPV6] = PORT_TEST_CHECKING;
    updatePortStatusText();

    if (!portTag_.connected())
    {
        portTag_ = core_->signal_port_tested().connect(
            [this](std::optional<bool> status, Session::PortTestIpProtocol ip_protocol) { onPortTested(status, ip_protocol); });
    }

    core_->port_test(Session::PORT_TEST_IPV4);
    core_->port_test(Session::PORT_TEST_IPV6);

    portTestSetSensitive();
}

NetworkPage::NetworkPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : PageBase(cast_item, builder, core)
    , core_(core)
    , portLabel_(get_widget<Gtk::Label>("listening_port_status_label"))
    , portButton_(get_widget<Gtk::Button>("test_listening_port_button"))
    , portSpin_(init_spin_row<uint16_t>("listening_port_spin", TR_KEY_peer_port, 1, std::numeric_limits<uint16_t>::max(), 1))
{
    portButton_->signal_clicked().connect([this]() { onPortTest(); });
    updatePortStatusText();

    prefsTag_ = core_->signal_prefs_changed().connect([this](auto key) { onCorePrefsChanged(key); });

    init_switch_row("pick_random_listening_port_at_start_check", TR_KEY_peer_port_random_on_start);
    init_switch_row("enable_listening_port_forwarding_check", TR_KEY_port_forwarding_enabled);
    init_spin_row<uint16_t>(
        "max_torrent_peers_spin",
        TR_KEY_peer_limit_per_torrent,
        1,
        std::numeric_limits<uint16_t>::max(),
        5);
    init_spin_row<uint16_t>("max_total_peers_spin", TR_KEY_peer_limit_global, 1, std::numeric_limits<uint16_t>::max(), 5);

#ifdef WITH_UTP
    init_switch_row("enable_utp_check", TR_KEY_utp_enabled);
#else
    get_widget<Gtk::Widget>("enable_utp_check")->set_visible(false);
#endif

    init_switch_row("enable_pex_check", TR_KEY_pex_enabled);
    init_switch_row("enable_dht_check", TR_KEY_dht_enabled);
    init_switch_row("enable_lpd_check", TR_KEY_lpd_enabled);
    init_text_view("default_trackers_view", TR_KEY_default_trackers);
}

} // namespace

/****
*****
****/

std::unique_ptr<PrefsDialog> PrefsDialog::create(Glib::RefPtr<Session> const& core)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("PrefsDialog.ui"));
    return std::unique_ptr<PrefsDialog>(gtr_get_widget_derived<PrefsDialog>(builder, "PrefsDialog", core));
}

PrefsDialog::PrefsDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : Gtk::Widget(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core))
{
}

PrefsDialog::~PrefsDialog() = default;

PrefsDialog::Impl::Impl(PrefsDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core)
    : dialog_(dialog)
    , core_(core)
{
    gtr_get_widget_derived<SpeedPage>(builder, "speed_page_layout", core_);
    gtr_get_widget_derived<DownloadingPage>(builder, "downloading_page_layout", core_);
    gtr_get_widget_derived<SeedingPage>(builder, "seeding_page_layout", core_);
    gtr_get_widget_derived<PrivacyPage>(builder, "privacy_page_layout", core_);
    gtr_get_widget_derived<NetworkPage>(builder, "network_page_layout", core_);
    gtr_get_widget_derived<DesktopPage>(builder, "desktop_page_layout", core_);
    gtr_get_widget_derived<RemotePage>(builder, "remote_page_layout", core_);
}
