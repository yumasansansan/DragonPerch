// SPDX-License-Identifier: GPL-3.0-or-later
#include "paths.hpp"

#include "win_headers.hpp"

#include <array>
#include <string>

namespace dp::win {

std::filesystem::path executable_directory()
{
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    return std::filesystem::path{std::wstring_view{buffer.data(), length}}.parent_path();
}

} // namespace dp::win
