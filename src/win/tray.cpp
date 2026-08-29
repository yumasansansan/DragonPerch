// SPDX-License-Identifier: GPL-3.0-or-later
#include "tray.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"
#include "resource.h"
#include "shell.hpp"
#include "win_headers.hpp"

#include <shellapi.h>
#include <windowsx.h>  // GET_X_LPARAM

#include <utility>

namespace dp::win {
namespace {

constexpr const wchar_t* window_class = L"DragonPerch.Tray";

/// The message the shell sends us about the icon. Anything from WM_APP up is ours.
constexpr UINT tray_message = WM_APP + 1;
constexpr UINT icon_id = 1;

enum MenuId : UINT_PTR {
    menu_pause = 1,
    menu_settings,
    menu_quit,
};

/// Registered rather than a constant: the shell broadcasts it when Explorer restarts, and
/// every tray icon in the session has to be added again. Forgetting this is the classic
/// way to lose a tray icon and not notice for a week.
UINT taskbar_created()
{
    static const UINT message = RegisterWindowMessageW(L"TaskbarCreated");
    return message;
}

struct TrayState {
    TrayIcon::Handler handler;
    TrayIcon::PausedQuery paused;
    NOTIFYICONDATAW icon{};
};

bool add_icon(NOTIFYICONDATAW& icon)
{
    if (Shell_NotifyIconW(NIM_ADD, &icon) == FALSE) {
        return false;
    }

    // Version 4 changes what the callback's wParam and lParam mean, and asking for it is a
    // separate call that has to come after the add.
    icon.uVersion = NOTIFYICON_VERSION_4;
    return Shell_NotifyIconW(NIM_SETVERSION, &icon) != FALSE;
}

void show_menu(HWND hwnd, TrayState& state, POINT at)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    const bool paused = state.paused && state.paused();
    AppendMenuW(menu, MF_STRING | (paused ? MF_CHECKED : 0U), menu_pause, L"&Pause");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Greyed until milestone 10 gives it something to open. Present because leaving it out
    // and adding it later moves everything else in the menu, and people learn where an item
    // is by where it sits.
    AppendMenuW(menu, MF_STRING | MF_GRAYED, menu_settings, L"&Settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, menu_quit, L"&Quit DragonPerch");

    // Both of these are needed and neither is obvious. Without the foreground call the menu
    // does not close when clicked away from, which reads as a hang; without the posted
    // message afterwards it can leave the first click after it being swallowed.
    SetForegroundWindow(hwnd);

    const int chosen = TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                        at.x, at.y, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (!state.handler) {
        return;
    }

    switch (chosen) {
    case menu_pause:
        state.handler(Command::toggle_pause);
        break;
    case menu_quit:
        state.handler(Command::quit);
        break;
    default:
        break;
    }
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* state = reinterpret_cast<TrayState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (state == nullptr) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    if (message == taskbar_created()) {
        // Explorer restarted and took the icon with it.
        log_line("tray: the shell restarted, adding the icon again");
        (void)add_icon(state->icon);
        return 0;
    }

    if (message == tray_message) {
        // Version 4: the anchor point is in wParam and the event in the low word of lParam,
        // which is the other way round from every older example.
        const POINT at{GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)};

        const bool paused = state->paused && state->paused();

        switch (LOWORD(lparam)) {
        case WM_MOUSEMOVE:
            // The pointer has arrived over the icon but nothing has been clicked yet. This
            // is the couple of hundred milliseconds a cold WinUI process needs, and
            // spending them here is the difference between a menu that feels instant and
            // one that feels broken. Cheap and idempotent when a shell is already up.
            shell::prewarm();
            return 0;

        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            // Left and right both open the menu. A tray icon whose two buttons do different
            // things is a tray icon whose users find one of them by accident.
            //
            // The Fluent menu if a shell is listening, and this process's own Win32 one if
            // not. Not waiting for a shell that is still starting: a menu that arrives half
            // a second after the click reads as a hang, and the Win32 menu is right there.
            if (!shell::show_menu(at.x, at.y, paused)) {
                // Warm one for next time, without asking it for anything: a shell that
                // showed a menu on startup would put a second one on the screen beside the
                // Win32 one below, which is still the right thing to show for this click.
                shell::prewarm();
                show_menu(hwnd, *state, at);
            }
            return 0;

        default:
            return 0;
        }
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

TrayState& state_for(void* window)
{
    return *reinterpret_cast<TrayState*>(
        GetWindowLongPtrW(static_cast<HWND>(window), GWLP_USERDATA));
}

} // namespace

TrayIcon::~TrayIcon()
{
    if (window_ == nullptr) {
        return;
    }

    TrayState& state = state_for(window_);
    Shell_NotifyIconW(NIM_DELETE, &state.icon);

    DestroyWindow(static_cast<HWND>(window_));
    delete &state;
}

bool TrayIcon::add(Handler handler, PausedQuery paused)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &window_proc;
    description.hInstance = instance;
    description.lpszClassName = window_class;

    if (RegisterClassExW(&description) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    HWND hwnd = CreateWindowExW(0, window_class, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                instance, nullptr);
    if (hwnd == nullptr) {
        return false;
    }

    // Owned by the window rather than by this object, because the window procedure is what
    // reaches it and outlives any particular call.
    auto* state = new TrayState{std::move(handler), std::move(paused), {}};

    state->icon.cbSize = sizeof(state->icon);
    state->icon.hWnd = hwnd;
    state->icon.uID = icon_id;
    state->icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    state->icon.uCallbackMessage = tray_message;

    // LoadImage rather than LoadIcon: the .ico holds eight sizes and this is what picks the
    // one the shell asked for, instead of scaling a 256 down to 16 and smearing it.
    state->icon.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_DRAGONPERCH),
                                                      IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                      GetSystemMetrics(SM_CYSMICON),
                                                      LR_DEFAULTCOLOR));
    if (state->icon.hIcon == nullptr) {
        delete state;
        DestroyWindow(hwnd);
        return false;
    }

    wcscpy_s(state->icon.szTip, L"DragonPerch");

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

    if (!add_icon(state->icon)) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete state;
        DestroyWindow(hwnd);
        return false;
    }

    window_ = hwnd;
    return true;
}

} // namespace dp::win
