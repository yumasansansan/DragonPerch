# SPDX-License-Identifier: GPL-3.0-or-later
#
# Renames C++ keywords used as parameter names in a generated Wayland header.
#
# Wayland protocols are specified for C, where `namespace` is an ordinary identifier, and
# `zwlr_layer_shell_v1.get_layer_surface` takes one:
#
#     zwlr_layer_shell_v1_get_layer_surface(..., const char *namespace)
#
# wayland-scanner copies argument names through verbatim, so the header it writes will not
# compile as C++ at all. Every C++ Wayland client hits this; the usual answer is
# `#define namespace wl_namespace` around the include, which is a macro over a keyword and
# affects every header that follows it in that translation unit.
#
# Renaming the parameter in the generated file instead keeps the workaround in one place,
# out of the source, and out of everything else's way. The parameter name is not part of
# any ABI -- it appears only in the inline wrapper's own signature and body.
#
# Invoked as: cmake -DHEADER=<path> -P RenameCxxKeywords.cmake

# Deliberately only the keywords C++ has and C does not. The file being rewritten is valid
# C, so a C keyword cannot be an identifier in it -- and leaving them out is what stops this
# from mangling `(void)` casts and `sizeof(int)`, which the narrow pattern below would
# otherwise match.
set(CXX_ONLY_KEYWORDS
    and and_eq bitand bitor catch class compl concept const_cast decltype delete dynamic_cast
    explicit export friend mutable namespace new not not_eq operator or or_eq private
    protected public reinterpret_cast requires static_cast template this throw try typeid
    typename using virtual xor xor_eq)

file(READ "${HEADER}" contents)

foreach(keyword IN LISTS CXX_ONLY_KEYWORDS)
    # Only where it is being used as a parameter or an argument: preceded by a pointer star,
    # an opening bracket, a comma or a space, and followed by a comma or a closing bracket.
    # Narrow on purpose -- a blanket rename would also rewrite the type names in the very
    # declarations being fixed.
    string(REGEX REPLACE "([ *(,])${keyword}([),])" "\\1wl_${keyword}\\2" contents "${contents}")
endforeach()

file(WRITE "${HEADER}" "${contents}")
