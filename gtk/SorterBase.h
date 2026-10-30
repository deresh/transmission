// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <gtkmm/sorter.h>

template<typename T>
class SorterBase : public Gtk::Sorter
{
public:
    SorterBase() = default;
    SorterBase(SorterBase&&) = delete;
    SorterBase(SorterBase const&) = delete;
    SorterBase& operator=(SorterBase&&) = delete;
    SorterBase& operator=(SorterBase const&) = delete;
    ~SorterBase() override = default;

    virtual int compare(T const& lhs, T const& rhs) const = 0;

protected:
    // Gtk::Sorter
    Gtk::Ordering compare_vfunc(gpointer lhs, gpointer rhs) override;
    Order get_order_vfunc() override;

private:
};
