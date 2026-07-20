
module;
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
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
