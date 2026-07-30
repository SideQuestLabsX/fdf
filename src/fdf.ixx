
module;

#if !defined(FDF_NO_FILE_IO)
    #define FDF_NO_FILE_IO false
#endif
#if !defined(FDF_NO_STD_FORMAT)
    #define FDF_NO_STD_FORMAT false
#endif
#if !defined(FDF_ASSERTIONS)
    #if defined(NDEBUG)
        #define FDF_ASSERTIONS false
    #else
        #define FDF_ASSERTIONS true
    #endif
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if FDF_ASSERTIONS
    #include <cstdio>
    #include <cstdlib>
#endif
#if !FDF_NO_FILE_IO
    #include <filesystem>
    #include <fstream>
#endif
#if !FDF_NO_STD_FORMAT
    #include <format>
#endif

export module fdf;

extern "C++"
{
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
    #elif defined(_MSC_VER)
        #pragma warning(push)
        #pragma warning(disable : 5244)
    #endif
    


    #define FDF_EXPORT export
    #include "fdf.h"
    
    

    #if defined(__clang__)
        #pragma clang diagnostic pop
    #elif defined(_MSC_VER)
        #pragma warning(pop)
    #endif
}
