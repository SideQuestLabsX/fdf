// FDF_FUZZ_TARGET_* selects parser, round-trip or combine coverage at build time
// diagnostics are counted without asserting recovery policy

// include std headers before module import to avoid duplicate declarations
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#if FDF_USE_CPP_MODULES
    import fdf;
#else
    #include "fdf.h"
#endif

namespace
{
    size_t g_diagnostics = 0;

    constexpr void CountDiagnostic(const fdf::Diagnostic&) noexcept
    {
        g_diagnostics++;
    }

#if defined(FDF_FUZZ_TARGET_ROUNDTRIP)
    // invariant failures must stop both libFuzzer and replay
    [[noreturn]] void Fail(const char* what) noexcept
    {
        std::fprintf(stderr, "fdf fuzz invariant failed: %s\n", what);
        std::fflush(stderr);
        std::abort();
    }

    // text equality covers scalar payloads, this checks structure
    bool ShapesMatch(const fdf::Entry& a, const fdf::Entry& b) noexcept
    {
        if(a.GetType() != b.GetType() || a.GetChildCount() != b.GetChildCount())
            return false;
        if(a.GetIdentifier() != b.GetIdentifier())
            return false;

        for(uint32_t i = 0; i < a.GetChildCount(); i++)
        {
            const fdf::Entry* childA = a.GetDirectChild(i);
            const fdf::Entry* childB = b.GetDirectChild(i);
            if(!childA || !childB || !ShapesMatch(*childA, *childB))
                return false;
        }
        return true;
    }
#endif
}

// libFuzzer entry point, also called by the replay driver
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) noexcept;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) noexcept
{
    const std::string_view input(reinterpret_cast<const char*>(data), size);

#if defined(FDF_FUZZ_TARGET_COMBINE)
    const size_t half = size / 2;
    fdf::UniqueEntryPtr base = fdf::ParseBuffer(input.substr(0, half), CountDiagnostic);
    if(!base)
        return 0;

    (void)base->ParseCombineBuffer(input.substr(half), CountDiagnostic);
    return 0;
#elif defined(FDF_FUZZ_TARGET_ROUNDTRIP)
    fdf::UniqueEntryPtr first = fdf::ParseBuffer(input, CountDiagnostic);
    if(!first)
        return 0;

    const fdf::String text = fdf::WriteBuffer(*first);
    fdf::UniqueEntryPtr second = fdf::ParseBuffer(std::string_view(text), CountDiagnostic);

    if(!second)
        Fail("writer produced text the parser rejects");

    const fdf::String again = fdf::WriteBuffer(*second);
    if(std::string_view(text) != std::string_view(again))
        Fail("second write differs, the round trip does not settle");

    if(!ShapesMatch(*first, *second))
        Fail("re-parsed tree has a different shape");

    return 0;
#else
    (void)fdf::ParseBuffer(input, CountDiagnostic);
    return 0;
#endif
}
