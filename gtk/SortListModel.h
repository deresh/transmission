// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <giomm/listmodel.h>
#include <gtkmm/sortlistmodel.h>

template<typename ItemT>
class SorterBase;

template<typename ItemT>
class SortListModel : public IF_GTKMM4(Gtk::SortListModel, Gtk::TreeModelSort)
{
public:
    using SorterType = SorterBase<ItemT>;

public:
    SortListModel(Glib::RefPtr<Gio::ListModel> const& model, Glib::RefPtr<SorterType> const& sorter);

    template<typename ModelT>
    static Glib::RefPtr<SortListModel<ItemT>> create(Glib::RefPtr<ModelT> const& model, Glib::RefPtr<SorterType> const& sorter);

private:
};
