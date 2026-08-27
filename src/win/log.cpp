// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.hpp"

#include "win_headers.hpp"

#include <array>
#include <cstdio>
#include <iostream>
#include <string>

namespace dp::win {

std::string to_utf8(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes,
                        nullptr, nullptr);
    return out;
}

namespace {

std::string make_log_path()
{
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    std::wstring path(buffer.data(), n);

    const std::size_t slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
        path.resize(slash + 1);
    }
    path += L"dragonperch.log";

    return to_utf8(path);
}

const std::string& log_file_path()
{
    static const std::string path = make_log_path();
    return path;
}

std::FILE* log_file()
{
    static std::FILE* file = [] {
        std::FILE* f = nullptr;
        (void)fopen_s(&f, log_file_path().c_str(), "w");
        return f;
    }();
    return file;
}

} // namespace

void log_line(std::string_view text)
{
    std::cout << text << '\n';
    std::cout.flush();

    if (std::FILE* f = log_file(); f != nullptr) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fputc('\n', f);
        std::fflush(f);
    }

    OutputDebugStringA(std::string(text).append("\n").c_str());
}

const char* log_path()
{
    return log_file_path().c_str();
}

} // namespace dp::win
