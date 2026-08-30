// SPDX-License-Identifier: GPL-3.0-or-later
#include "paths.hpp"

#include <system_error>

namespace dp::wl {

std::filesystem::path executable_directory()
{
    std::error_code failed;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", failed);
    if (failed) {
        return std::filesystem::current_path();
    }
    return self.parent_path();
}

} // namespace dp::wl
