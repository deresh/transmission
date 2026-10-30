// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cairommconfig.h>
#include <glibmmconfig.h>
#include <gtkmmconfig.h>
#include <pangommconfig.h>

#include <cstddef>

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

#ifndef GTKMM_CHECK_VERSION
#define GTKMM_CHECK_VERSION(major, minor, micro) \
    (GTKMM_MAJOR_VERSION > (major) || (GTKMM_MAJOR_VERSION == (major) && GTKMM_MINOR_VERSION > (minor)) || \
     (GTKMM_MAJOR_VERSION == (major) && GTKMM_MINOR_VERSION == (minor) && GTKMM_MICRO_VERSION >= (micro)))
#endif

#ifndef GLIBMM_CHECK_VERSION
#define GLIBMM_CHECK_VERSION(major, minor, micro) \
    (GLIBMM_MAJOR_VERSION > (major) || (GLIBMM_MAJOR_VERSION == (major) && GLIBMM_MINOR_VERSION > (minor)) || \
     (GLIBMM_MAJOR_VERSION == (major) && GLIBMM_MINOR_VERSION == (minor) && GLIBMM_MICRO_VERSION >= (micro)))
#endif

#ifndef CAIROMM_CHECK_VERSION
#define CAIROMM_CHECK_VERSION(major, minor, micro) \
    (CAIROMM_MAJOR_VERSION > (major) || (CAIROMM_MAJOR_VERSION == (major) && CAIROMM_MINOR_VERSION > (minor)) || \
     (CAIROMM_MAJOR_VERSION == (major) && CAIROMM_MINOR_VERSION == (minor) && CAIROMM_MICRO_VERSION >= (micro)))
#endif

#ifndef PANGOMM_CHECK_VERSION
#define PANGOMM_CHECK_VERSION(major, minor, micro) \
    (PANGOMM_MAJOR_VERSION > (major) || (PANGOMM_MAJOR_VERSION == (major) && PANGOMM_MINOR_VERSION > (minor)) || \
     (PANGOMM_MAJOR_VERSION == (major) && PANGOMM_MINOR_VERSION == (minor) && PANGOMM_MICRO_VERSION >= (micro)))
#endif

#if CAIROMM_CHECK_VERSION(1, 16, 0)
#define IF_CAIROMM1_16(ThenValue, ElseValue) ThenValue
#else
#define IF_CAIROMM1_16(ThenValue, ElseValue) ElseValue
#endif

#if PANGOMM_CHECK_VERSION(2, 48, 0)
#define IF_PANGOMM2_48(ThenValue, ElseValue) ThenValue
#else
#define IF_PANGOMM2_48(ThenValue, ElseValue) ElseValue
#endif

#define TR_CAIRO_SURFACE_FORMAT(Code) IF_CAIROMM1_16(Cairo::Surface::Format::Code, Cairo::FORMAT_##Code)
#define TR_CAIRO_CONTEXT_OPERATOR(Code) IF_CAIROMM1_16(Cairo::Context::Operator::Code, Cairo::OPERATOR_##Code)

#define TR_PANGO_ALIGNMENT(Code) IF_PANGOMM2_48(Pango::Alignment::Code, Pango::ALIGN_##Code)
#define TR_PANGO_ELLIPSIZE_MODE(Code) IF_PANGOMM2_48(Pango::EllipsizeMode::Code, Pango::ELLIPSIZE_##Code)
#define TR_PANGO_WEIGHT(Code) IF_PANGOMM2_48(Pango::Weight::Code, Pango::WEIGHT_##Code)

// NOLINTEND(cppcoreguidelines-macro-usage)

namespace Glib
{

} // namespace Glib
