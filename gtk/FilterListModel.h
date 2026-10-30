// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <giomm/listmodel.h>
#include <gtkmm/filterlistmodel.h>

template<typename ItemT>
class FilterBase;

template<typename ItemT>
class FilterListModel : public Gtk::FilterListModel
{
public:
    using FilterType = FilterBase<ItemT>;

public:
    FilterListModel(Glib::RefPtr<Gio::ListModel> const& model, Glib::RefPtr<FilterType> const& filter);

    template<typename ModelT>
    static Glib::RefPtr<FilterListModel<ItemT>> create(
        Glib::RefPtr<ModelT> const& model,
        Glib::RefPtr<FilterType> const& filter);

private:
};
