// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <functional>
#include <memory>

// A tray icon that follows the StatusNotifierItem protocol, exported with GDBus.
// It is shown by a StatusNotifierWatcher on the session bus (AppIndicator
// extension, KDE Plasma, ...). Without a watcher it does nothing.
class StatusNotifierItem
{
public:
    // Called when the user activates the icon (left click).
    using ActivateFunc = std::function<void()>;

    explicit StatusNotifierItem(ActivateFunc activate);
    StatusNotifierItem(StatusNotifierItem&&) = delete;
    StatusNotifierItem(StatusNotifierItem const&) = delete;
    StatusNotifierItem& operator=(StatusNotifierItem&&) = delete;
    StatusNotifierItem& operator=(StatusNotifierItem const&) = delete;
    ~StatusNotifierItem();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
