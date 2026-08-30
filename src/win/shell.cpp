// SPDX-License-Identifier: GPL-3.0-or-later
#include "shell.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"
#include "win_headers.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <system_error>

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

/// One request to a listening shell. False when it was not understood or did not land.
bool ask(HWND target, const std::string& request)
{
    // Windows will not let an arbitrary process take the foreground, and a window that
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

    // The same wire as the daemon's own control window: WM_COPYDATA carrying text. Text
    // because the two ends are separate programs and may be different builds -- a shell
    // that does not understand a request should ignore it, not act on whatever the third
    // enumerator happened to be that week.
    return ask(target, cat("menu ", x, " ", y, paused ? " paused" : " running"));
}

namespace {

/// Starts a shell process, with whatever is on `arguments` appended to the command line.
///
/// Returns false when there is nothing to start. Everything else is best effort: a shell
/// that fails to appear leaves the daemon exactly where it was.
bool start(const wchar_t* arguments)
{
    const std::filesystem::path path = executable_path();
    std::error_code failed;
    if (path.empty() || !std::filesystem::exists(path, failed)) {
        return false;
    }

    // The path has to stay wide -- somebody's user name is not necessarily representable in
    // the active code page -- but the tail is digits and ASCII words, so widening it a
    // character at a time is exact. `cat` is narrow-only on purpose; see
    // dragonperch/text.hpp for why there is no <format> anywhere near this.
    std::wstring command = L"\"" + path.wstring() + L"\"";
    command += arguments;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);

    // No "application starting" cursor. CreateProcess gives one by default -- the arrow
    // with the spinning ring -- and it stays up until the new process has a message queue
    // Windows is satisfied with, or a couple of seconds pass. The visible cursor is only
    // refreshed on the next mouse move, so a person who starts the pointer moving towards
    // the tray icon and then stops watches it spin until they move again.
    //
    // That feedback is for a program somebody launched and is waiting for. Nobody asked
    // for this one; it is started because the pointer passed over an icon, and it has no
    // window to wait for.
    startup.dwFlags |= STARTF_FORCEOFFFEEDBACK;

    PROCESS_INFORMATION process{};

    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, path.parent_path().c_str(), &startup, &process)
        == FALSE) {
        log_line(cat("tray: could not start the shell (", GetLastError(), ")"));
        return false;
    }

    // Neither handle is wanted. The shell is not a child to be waited for: it outlives
    // individual menus, and the daemon must not care when it goes. The shell watches the
    // daemon rather than the other way round, and exits when this process does.
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

} // namespace

bool is_installed()
{
    // Not cached. The answer can change while the daemon runs -- somebody installing the
    // shell beside a running daemon is exactly the case where a greyed menu item that
    // never comes back would be wrong -- and this is a stat on a fixed path, asked once
    // per menu.
    const std::filesystem::path path = executable_path();
    std::error_code failed;
    return !path.empty() && std::filesystem::exists(path, failed);
}

bool open_settings()
{
    // Asked of a running shell first, and only started when there is none. Starting one
    // unconditionally does not work: a second shell finds the first and exits without
    // showing anything, and "there is a shell, it was merely too slow to draw the menu" is
    // exactly the case this item is reached in. That fault is silent -- a menu item that
    // does nothing at all -- which is why it is worth the two paths.
    //
    // Only ever reached from the menu the *daemon* drew. When the shell draws the menu it
    // opens the window itself and never comes through here.
    if (HWND target = find(); target != nullptr) {
        return ask(target, "settings");
    }

    return start(L" --settings");
}

void prewarm()
{
    // Two states worth remembering between calls, because this is called on every mouse
    // move over the icon: there is no shell to start, and one has just been started and
    // has not finished appearing yet.
    static bool missing = false;
    static ULONGLONG last_attempt = 0;

    if (missing || is_listening()) {
        return;
    }

    // A cold shell takes a moment to create its window. Without this the next few mouse
    // moves each start another one, and all but the first exit again on finding a shell
    // already listening -- harmless, but a handful of processes started and thrown away
    // for one hover.
    const ULONGLONG now = GetTickCount64();
    if (last_attempt != 0 && now - last_attempt < 5000) {
        return;
    }
    last_attempt = now;

    // Deliberately no menu request on the command line. The shell accepts one, but a shell
    // that opened a menu as it started would put a second one on the screen next to the
    // Win32 menu the daemon shows for that same click, and on a hover it would open a menu
    // nobody asked for. Both happened.
    if (!start(L"")) {
        // Said once, because the answer will not change while this process runs. `start`
        // has already said its piece when the failure was CreateProcess rather than an
        // absence, so only the absence is worth a second line.
        if (!is_installed()) {
            log_line("tray: no DragonPerch.Shell.exe beside the daemon; using the Win32 menu");
        }
        missing = true;
    }
}

} // namespace dp::win::shell
