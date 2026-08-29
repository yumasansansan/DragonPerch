// SPDX-License-Identifier: GPL-3.0-or-later
#include "shell.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"
#include "win_headers.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace dp::win::shell {
namespace {

/// Matched by shell/windows/ShellServer.cs.
constexpr const wchar_t* window_class = L"DragonPerch.Shell";

constexpr const wchar_t* executable = L"DragonPerch.Shell.exe";

HWND find()
{
    // Message-only windows live outside the desktop's window list, so they only turn up
    // when HWND_MESSAGE is searched explicitly.
    return FindWindowExW(HWND_MESSAGE, nullptr, window_class, nullptr);
}

/// Beside the daemon. Not searched for on PATH: the shell that belongs to this build is
/// the one installed next to it, and finding somebody else's is worse than finding none.
std::filesystem::path executable_path()
{
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n == 0 || n >= buffer.size()) {
        return {};
    }

    return std::filesystem::path{std::wstring{buffer.data(), n}}.replace_filename(executable);
}

} // namespace

bool is_listening()
{
    return find() != nullptr;
}

bool show_menu(int x, int y, bool paused)
{
    HWND target = find();
    if (target == nullptr) {
        return false;
    }

    // Windows will not let an arbitrary process take the foreground, and a menu that
    // cannot take it is shown and then immediately dismissed -- it appears for one frame
    // and vanishes, which reads as a menu that does not work rather than as a permission
    // problem. The daemon is the one holding the right at this moment, because the click
    // on the tray icon is what gave it, so it has to hand that right over explicitly.
    //
    // This is the whole reason the daemon asks the shell rather than the shell watching
    // for clicks itself.
    DWORD shell_pid = 0;
    (void)GetWindowThreadProcessId(target, &shell_pid);
    if (shell_pid != 0) {
        (void)AllowSetForegroundWindow(shell_pid);
    }

    // The same wire as the daemon's own control window: WM_COPYDATA carrying text. Text
    // because the two ends are separate programs and may be different builds -- a shell
    // that does not understand a request should ignore it, not act on whatever the third
    // enumerator happened to be that week.
    const std::string request = cat("menu ", x, " ", y, paused ? " paused" : " running");

    COPYDATASTRUCT data{};
    data.dwData = 0;
    data.cbData = static_cast<DWORD>(request.size());
    data.lpData = const_cast<char*>(request.data());

    // SendMessageTimeout, not PostMessage: WM_COPYDATA hands over a pointer into this
    // process, so the shell has to be finished with it before `request` goes away. The
    // timeout is what stops a wedged shell wedging the pets with it -- this runs on the
    // render thread.
    DWORD_PTR result = 0;
    const LRESULT sent = SendMessageTimeoutW(target, WM_COPYDATA, 0,
                                             reinterpret_cast<LPARAM>(&data),
                                             SMTO_ABORTIFHUNG, 1000, &result);
    return sent != 0 && result != 0;
}

void start(int menu_x, int menu_y, bool paused)
{
    // Two states worth remembering between calls, because this is called on every mouse
    // move over the icon: there is no shell to start, and one has just been started and
    // has not finished appearing yet.
    static bool missing = false;
    static ULONGLONG last_attempt = 0;

    if (missing || is_listening()) {
        return;
    }

    // A cold WinUI process takes a moment to create its window. Without this the next few
    // mouse moves each start another one, and all but the first exit again on finding a
    // shell already listening -- harmless, but a handful of processes started and thrown
    // away for one hover.
    const ULONGLONG now = GetTickCount64();
    if (last_attempt != 0 && now - last_attempt < 5000) {
        return;
    }
    last_attempt = now;

    const std::filesystem::path path = executable_path();
    if (path.empty() || !std::filesystem::exists(path)) {
        // Said once, because the answer will not change while this process runs.
        log_line("tray: no DragonPerch.Shell.exe beside the daemon; using the Win32 menu");
        missing = true;
        return;
    }

    // Given the menu request on the command line as well as being sent one later, so that
    // the very first right-click shows a menu rather than starting a shell that then has
    // nothing to do.
    const std::string tail = cat(" --menu ", menu_x, " ", menu_y,
                                 paused ? " paused" : " running");

    // The path has to stay wide -- somebody's user name is not necessarily representable
    // in the active code page -- but the tail is digits and ASCII words, so widening it a
    // character at a time is exact. `cat` is narrow-only on purpose; see
    // dragonperch/text.hpp for why there is no <format> anywhere near this.
    std::wstring command = L"\"" + path.wstring() + L"\"";
    command.append(tail.begin(), tail.end());

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, path.parent_path().c_str(), &startup, &process)
        == FALSE) {
        log_line(cat("tray: could not start the shell (", GetLastError(), ")"));
        missing = true;
        return;
    }

    // Neither handle is wanted. The shell is not a child to be waited for: it outlives
    // individual menus, and the daemon must not care when it goes.
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

} // namespace dp::win::shell
