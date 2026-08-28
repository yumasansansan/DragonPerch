// SPDX-License-Identifier: GPL-3.0-or-later
#include "control.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"
#include "win_headers.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace dp::win {
namespace {

constexpr const wchar_t* window_class = L"DragonPerch.Control";

/// Every command, so that both ends agree by construction rather than by two switch
/// statements that have to be kept in step.
constexpr std::array<std::pair<Command, std::string_view>, 5> commands{{
    {Command::quit, "quit"},
    {Command::pause, "pause"},
    {Command::resume, "resume"},
    {Command::toggle_pause, "toggle-pause"},
    {Command::reload, "reload"},
}};

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_COPYDATA) {
        auto* data = reinterpret_cast<COPYDATASTRUCT*>(lparam);
        auto* server = reinterpret_cast<ControlServer::Handler*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (data != nullptr && data->lpData != nullptr && server != nullptr && *server) {
            // Text rather than an enum value on the wire. The two ends are separate
            // processes and may be different builds -- a nightly asking an older one to
            // quit should either work or do nothing, not mean whatever the third enumerator
            // happened to be that week.
            const std::string_view text(static_cast<const char*>(data->lpData), data->cbData);
            for (const auto& [command, name] : commands) {
                if (text == name) {
                    (*server)(command);
                    return TRUE;
                }
            }
            log_line(cat("control: ignoring an unknown command '", text, "'"));
        }
        return FALSE;
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

HWND find_running()
{
    // HWND_MESSAGE windows are not found by FindWindowW: message-only windows live outside
    // the desktop's window list and only turn up when it is searched explicitly.
    return FindWindowExW(HWND_MESSAGE, nullptr, window_class, nullptr);
}

} // namespace

std::string_view name_of(Command command) noexcept
{
    for (const auto& [value, name] : commands) {
        if (value == command) {
            return name;
        }
    }
    return "?";
}

ControlServer::~ControlServer()
{
    if (window_ != nullptr) {
        DestroyWindow(static_cast<HWND>(window_));
    }
}

bool ControlServer::start(Handler handler)
{
    handler_ = std::move(handler);

    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &window_proc;
    description.hInstance = GetModuleHandleW(nullptr);
    description.lpszClassName = window_class;

    // Registering twice is what a second instance in the same process would do, and the
    // class already existing is not a failure.
    if (RegisterClassExW(&description) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    HWND hwnd = CreateWindowExW(0, window_class, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                description.hInstance, nullptr);
    if (hwnd == nullptr) {
        return false;
    }

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&handler_));
    window_ = hwnd;
    return true;
}

bool send_command(Command command)
{
    HWND target = find_running();
    if (target == nullptr) {
        return false;
    }

    const std::string_view text = name_of(command);

    COPYDATASTRUCT data{};
    data.dwData = 0;
    data.cbData = static_cast<DWORD>(text.size());
    data.lpData = const_cast<char*>(text.data());

    // SendMessage, not PostMessage: WM_COPYDATA hands over a pointer into this process's
    // memory, and it is only valid until the call returns. Posting would let this process
    // exit while the other one is still reading.
    //
    // Timed out rather than open-ended, so that a wedged DragonPerch cannot wedge the
    // program trying to stop it.
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                               SMTO_ABORTIFHUNG, 5000, &result)
           != 0;
}

} // namespace dp::win
