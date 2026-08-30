// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One place to get the Windows headers in the right order, so no translation unit has to
// remember which macros must be defined first. NOMINMAX and WIN32_LEAN_AND_MEAN come from
// the build; they are repeated nowhere.

#include <windows.h>

#include <d2d1_3.h>
#include <d3d11_4.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <system_error>

namespace dp::win {

using Microsoft::WRL::ComPtr;

/// Throws a std::system_error carrying the HRESULT, so failures name themselves instead of
/// surfacing as a silent early return three layers up.
inline void check(HRESULT hr, const char* what)
{
    if (FAILED(hr)) {
        throw std::system_error(static_cast<int>(hr), std::system_category(), what);
    }
}

inline void check_last_error(bool ok, const char* what)
{
    if (!ok) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), what);
    }
}

/// The same, for a handle, returning what it checked.
///
/// `check_last_error(hwnd != nullptr, ...)` tells a human that hwnd is good from there on
/// and tells /analyze nothing: the analyser reasons about the caller's expression, and a
/// throw inside a callee is not a postcondition it can see. An `__assume` in the callee
/// does not help either -- measured, and it does not. What does help is saying so with
/// SAL, once, here.
///
/// Without it every call site has to repeat the null test to keep C6387 quiet, which is
/// three lines of dead code per handle and a second place for the message to drift.
template <typename Handle>
[[nodiscard]] _Ret_notnull_ Handle check_handle(Handle handle, const char* what)
{
    if (handle == nullptr) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), what);
    }
    return handle;
}

} // namespace dp::win
