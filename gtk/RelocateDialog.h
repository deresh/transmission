// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h>

#include <glibmm/refptr.h>
#include <gtkmm/builder.h>
#include <gtkmm/widget.h>

#include <memory>
#include <vector>

class Session;

class RelocateDialog : public Gtk::Widget
{
public:
    RelocateDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        std::vector<tr_torrent_id_t> const& torrent_ids);
    RelocateDialog(RelocateDialog&&) = delete;
    RelocateDialog(RelocateDialog const&) = delete;
    RelocateDialog& operator=(RelocateDialog&&) = delete;
    RelocateDialog& operator=(RelocateDialog const&) = delete;
    ~RelocateDialog() override;

    static std::unique_ptr<RelocateDialog> create(
        Glib::RefPtr<Session> const& core,
        std::vector<tr_torrent_id_t> const& torrent_ids);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
