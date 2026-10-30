// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "FilterBase.h"
#include "Utils.h"

template<typename T>
bool FilterBase<T>::matches_all() const
{
    return false;
}

template<typename T>
bool FilterBase<T>::matches_none() const
{
    return false;
}

template<typename T>
bool FilterBase<T>::match_vfunc(Glib::RefPtr<Glib::ObjectBase> const& object)
{
    auto const concrete_object = gtr_ptr_dynamic_cast<T>(object);
    g_return_val_if_fail(concrete_object != nullptr, false);

    return match(*concrete_object);
}

template<typename T>
typename FilterBase<T>::Match FilterBase<T>::get_strictness_vfunc()
{
    if (matches_all())
    {
        return Match::ALL;
    }

    if (matches_none())
    {
        return Match::NONE;
    }

    return Match::SOME;
}
