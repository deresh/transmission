// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "StatsDialog.h"

#include "AdwCompat.h"
#include "GtkCompat.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/ustring.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>

#include <fmt/format.h>

#include <memory>
#include <string_view>

class StatsDialog::Impl
{
public:
    Impl(StatsDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

private:
    bool updateStats();
    void onResetClicked();

private:
    StatsDialog& dialog_;
    /* keeps the builder's widgets alive for as long as the dialog exists */
    Glib::RefPtr<Gtk::Builder> const builder_;
    Glib::RefPtr<Session> const core_;

    Gtk::Label* one_up_lb_;
    Gtk::Label* one_down_lb_;
    Gtk::Label* one_ratio_lb_;
    Gtk::Label* one_time_lb_;

    Gtk::Label* all_up_lb_;
    Gtk::Label* all_down_lb_;
    Gtk::Label* all_ratio_lb_;
    Gtk::Label* all_time_lb_;

    Gtk::Label* all_sessions_lb_;

    sigc::connection update_stats_tag_;
};

namespace
{

void setLabel(Gtk::Label* l, Glib::ustring const& str)
{
    gtr_label_set_text(*l, str);
}

void setLabelFromRatio(Gtk::Label* l, double d)
{
    setLabel(l, tr_strlratio(d));
}

auto startedTimesText(uint64_t n)
{
    return fmt::format(fmt::runtime(ngettext("Started {count:L} time", "Started {count:L} times", n)), fmt::arg("count", n));
}

} // namespace

bool StatsDialog::Impl::updateStats()
{
    auto stats = tr_sessionGetStats(core_->get_session());
    setLabel(one_up_lb_, tr_strlsize(stats.uploadedBytes));
    setLabel(one_down_lb_, tr_strlsize(stats.downloadedBytes));
    setLabel(one_time_lb_, tr_format_time(stats.secondsActive));
    setLabelFromRatio(one_ratio_lb_, stats.ratio);

    stats = tr_sessionGetCumulativeStats(core_->get_session());
    setLabel(all_sessions_lb_, startedTimesText(stats.sessionCount));
    setLabel(all_up_lb_, tr_strlsize(stats.uploadedBytes));
    setLabel(all_down_lb_, tr_strlsize(stats.downloadedBytes));
    setLabel(all_time_lb_, tr_format_time(stats.secondsActive));
    setLabelFromRatio(all_ratio_lb_, stats.ratio);

    return true;
}

StatsDialog::Impl::~Impl()
{
    update_stats_tag_.disconnect();
}

void StatsDialog::Impl::onResetClicked()
{
    auto* const dialog = adw_compat_alert_dialog_new(
        _("Reset your statistics?"),
        _("These statistics are for your information only. "
          "Resetting them doesn't affect the statistics logged by your BitTorrent trackers."));
    adw_compat_alert_add_response(dialog, "cancel", _("Cancel"));
    adw_compat_alert_add_response(dialog, "reset", _("Reset"), ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
    adw_compat_alert_on_response(
        dialog,
        [this](char const* response)
        {
            if (std::string_view{ response } == "reset")
            {
                tr_sessionClearStats(core_->get_session());
                updateStats();
            }
        });
    adw_compat_dialog_present(dialog, GTK_WIDGET(dialog_.gobj()));
}

StatsDialog::StatsDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : Gtk::Widget(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core))
{
}

StatsDialog::~StatsDialog() = default;

std::unique_ptr<StatsDialog> StatsDialog::create(Glib::RefPtr<Session> const& core)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("StatsDialog.ui"));
    return std::unique_ptr<StatsDialog>(gtr_get_widget_derived<StatsDialog>(builder, "StatsDialog", core));
}

StatsDialog::Impl::Impl(StatsDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core)
    : dialog_(dialog)
    , builder_(builder)
    , core_(core)
    , one_up_lb_(gtr_get_widget<Gtk::Label>(builder, "current_uploaded_value_label"))
    , one_down_lb_(gtr_get_widget<Gtk::Label>(builder, "current_downloaded_value_label"))
    , one_ratio_lb_(gtr_get_widget<Gtk::Label>(builder, "current_ratio_value_label"))
    , one_time_lb_(gtr_get_widget<Gtk::Label>(builder, "current_duration_value_label"))
    , all_up_lb_(gtr_get_widget<Gtk::Label>(builder, "total_uploaded_value_label"))
    , all_down_lb_(gtr_get_widget<Gtk::Label>(builder, "total_downloaded_value_label"))
    , all_ratio_lb_(gtr_get_widget<Gtk::Label>(builder, "total_ratio_value_label"))
    , all_time_lb_(gtr_get_widget<Gtk::Label>(builder, "total_duration_value_label"))
    , all_sessions_lb_(gtr_get_widget<Gtk::Label>(builder, "start_count_label"))
{
    gtr_get_widget<Gtk::Button>(builder, "reset_button")->signal_clicked().connect([this]() { onResetClicked(); });
    gtr_get_widget<Gtk::Button>(builder, "close_button")
        ->signal_clicked()
        .connect([this]() { adw_dialog_close(ADW_DIALOG(dialog_.gobj())); });

    updateStats();
    update_stats_tag_ = Glib::signal_timeout().connect_seconds(
        sigc::mem_fun(*this, &Impl::updateStats),
        SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);
}
