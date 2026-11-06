// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <glibmm/refptr.h>
#include <gtkmm/builder.h>
#include <gtkmm/widget.h>

#include <memory>

class Session;

class MakeDialog : public Gtk::Widget
{
public:
    MakeDialog(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    MakeDialog(MakeDialog&&) = delete;
    MakeDialog(MakeDialog const&) = delete;
    MakeDialog& operator=(MakeDialog&&) = delete;
    MakeDialog& operator=(MakeDialog const&) = delete;
    ~MakeDialog() override;

    static std::unique_ptr<MakeDialog> create(Glib::RefPtr<Session> const& core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
