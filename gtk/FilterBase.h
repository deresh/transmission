// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <gtkmm/filter.h>

#include <cstdint>

template<typename T>
class FilterBase : public IF_GTKMM4(Gtk::Filter, Glib::Object)
{
public:
    FilterBase() = default;
    FilterBase(FilterBase&&) = delete;
    FilterBase(FilterBase const&) = delete;
    FilterBase& operator=(FilterBase&&) = delete;
    FilterBase& operator=(FilterBase const&) = delete;
    ~FilterBase() override = default;

    virtual bool match(T const& item) const = 0;

    virtual bool matches_all() const;
    virtual bool matches_none() const;

protected:
    // Gtk::Filter
    bool match_vfunc(Glib::RefPtr<Glib::ObjectBase> const& object) override;
    Match get_strictness_vfunc() override;

private:
};
