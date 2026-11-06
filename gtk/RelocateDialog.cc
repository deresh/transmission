// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "RelocateDialog.h"

#include "AdwCompat.h"
#include "GtkCompat.h"
#include "PathButton.h"
#include "Prefs.h" /* gtr_pref_string_get */
#include "Session.h"
#include "Utils.h"

#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/ustring.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>

#include <fmt/format.h>

#include <memory>
#include <string>

namespace
{

std::string targetLocation;

}

class RelocateDialog::Impl
{
public:
    Impl(
        RelocateDialog& dialog,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        std::vector<tr_torrent_id_t> const& torrent_ids);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

private:
    void onApply();
    bool onTimer();

    void startMovingNextTorrent();

private:
    RelocateDialog& dialog_;
    /* keeps the builder's widgets alive for as long as the dialog exists */
    Glib::RefPtr<Gtk::Builder> const builder_;
    Glib::RefPtr<Session> const core_;
    std::vector<tr_torrent_id_t> torrent_ids_;

    int done_ = 0;
    bool do_move_ = false;
    sigc::connection timer_;
    GtkWidget* progress_dialog_ = nullptr;
    PathButton* chooser_ = nullptr;
    Gtk::CheckButton* move_tb_ = nullptr;
};

RelocateDialog::Impl::~Impl()
{
    timer_.disconnect();
}

/***
****
***/

void RelocateDialog::Impl::startMovingNextTorrent()
{
    auto* const tor = core_->find_torrent(torrent_ids_.back());

    if (tor != nullptr)
    {
        tr_torrentSetLocation(tor, targetLocation.c_str(), do_move_, &done_);
    }

    torrent_ids_.pop_back();

    if (progress_dialog_ != nullptr)
    {
        auto const heading = fmt::format(
            fmt::runtime(_("Moving '{torrent_name}'")),
            fmt::arg("torrent_name", tr_torrentName(tor)));
        adw_alert_dialog_set_heading(ADW_ALERT_DIALOG(progress_dialog_), heading.c_str());
    }
}

/* every once in awhile, check to see if the move is done.
 * if so, delete the dialog */
bool RelocateDialog::Impl::onTimer()
{
    if (done_ == TR_LOC_ERROR)
    {
        if (progress_dialog_ != nullptr)
        {
            adw_dialog_force_close(ADW_DIALOG(progress_dialog_));
            progress_dialog_ = nullptr;
        }

        gtr_show_error(dialog_, _("Couldn't move torrent"), {}, [this]() { adw_dialog_close(ADW_DIALOG(dialog_.gobj())); });
        return false;
    }

    if (done_ == TR_LOC_DONE)
    {
        if (!torrent_ids_.empty())
        {
            startMovingNextTorrent();
        }
        else
        {
            if (progress_dialog_ != nullptr)
            {
                adw_dialog_force_close(ADW_DIALOG(progress_dialog_));
                progress_dialog_ = nullptr;
            }
            adw_dialog_close(ADW_DIALOG(dialog_.gobj()));
            return false;
        }
    }

    return true;
}

void RelocateDialog::Impl::onApply()
{
    auto const location = chooser_->get_filename();

    do_move_ = move_tb_->get_active();

    /* pop up a dialog saying that the work is in progress */
    progress_dialog_ = adw_compat_alert_dialog_new(nullptr, _("This may take a moment…"));
    adw_compat_alert_add_response(progress_dialog_, "close", _("Close"));
    adw_alert_dialog_set_response_enabled(ADW_ALERT_DIALOG(progress_dialog_), "close", FALSE);
    adw_compat_dialog_present(progress_dialog_, GTK_WIDGET(dialog_.gobj()));

    /* remember this location for the next torrent */
    targetLocation = location;

    /* remember this location so that it can be the default next time */
    gtr_save_recent_dir("relocate", core_, location);

    /* start the move and periodically check its status */
    done_ = TR_LOC_DONE;
    timer_ = Glib::signal_timeout().connect_seconds(sigc::mem_fun(*this, &Impl::onTimer), 1);
    onTimer();
}

RelocateDialog::RelocateDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    std::vector<int> const& torrent_ids)
    : Gtk::Widget(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core, torrent_ids))
{
}

RelocateDialog::~RelocateDialog() = default;

std::unique_ptr<RelocateDialog> RelocateDialog::create(
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("RelocateDialog.ui"));
    return std::unique_ptr<RelocateDialog>(
        gtr_get_widget_derived<RelocateDialog>(builder, "RelocateDialog", core, torrent_ids));
}

RelocateDialog::Impl::Impl(
    RelocateDialog& dialog,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids)
    : dialog_(dialog)
    , builder_(builder)
    , core_(core)
    , torrent_ids_(torrent_ids)
    , chooser_(gtr_get_widget_derived<PathButton>(builder, "new_location_button"))
    , move_tb_(gtr_get_widget<Gtk::CheckButton>(builder, "move_data_radio"))
{
    gtr_get_widget<Gtk::Button>(builder, "apply_button")->signal_clicked().connect([this]() { onApply(); });
    gtr_get_widget<Gtk::Button>(builder, "cancel_button")
        ->signal_clicked()
        .connect([this]() { adw_dialog_close(ADW_DIALOG(dialog_.gobj())); });

    auto recent_dirs = gtr_get_recent_dirs("relocate");
    if (recent_dirs.empty())
    {
        /* default to download dir */
        chooser_->set_filename(gtr_pref_string_get(TR_KEY_download_dir));
    }
    else
    {
        /* set last used as target */
        chooser_->set_filename(recent_dirs.front());
        recent_dirs.pop_front();

        /* add remaining as shortcut */
        chooser_->set_shortcut_folders(recent_dirs);
    }
}
