
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <print>
#include <random>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #include <io.h>
#else
    #include <unistd.h>
#endif

#if FDF_USE_CPP_MODULES
    import fdf;
#else
    #include "fdf.h"
#endif

#ifndef FDF_OUTPUT_DIRECTORY
    #define FDF_OUTPUT_DIRECTORY FDF_TEST_DIRECTORY "/output"
#endif




namespace fdf::detail
{
    // order matches TokenType in fdf.h
    constexpr std::string_view TOKEN_TYPE_TO_STRING[] =
    {
        "NonExisting     ",
        "Invalid         ",
        "NewLine         ",
        "EndOfFile       ",
        "Comment         ",

        "Equal           ",
        "Comma           ",
        "Pipe            ",

        "CurlyBraceOpen  ",
        "CurlyBraceClose ",
        "SquareBraceOpen ",
        "SquareBraceClose",

        "StringLiteral   ",
        "Atom            ",
    };

    // order matches Type in fdf.h
    constexpr std::string_view ENTRY_TYPE_TO_STRING[] =
    {
        "Map      ",
        "Array    ",
        "Null     ",
        "Bool     ",
        "Int      ",
        "Float    ",
        "String   ",
        "Hex      ",
        "Version  ",
        "Timestamp",
        "Duration "
    };
}




namespace fdf::test
{
    inline int g_checks = 0;
    inline bool g_bStress = false;
    inline int g_failed = 0;
    inline int g_caseChecks = 0;
    inline int g_caseFailed = 0;
    inline std::vector<std::string> g_failedCases;

    // a TTY rewrites the RUN line while captured logs print only the result
#if defined(_WIN32)
    inline const bool g_bIsTty = _isatty(_fileno(stdout)) != 0;
#else
    inline const bool g_bIsTty = isatty(fileno(stdout)) != 0;
#endif
    inline bool g_bCaseLineOpen = false;

    inline int g_diagnostics = 0;
    inline fdf::Diagnostic g_lastDiagnostic = {};
    inline bool g_sawInvalidIdentifier = false;
    inline bool g_sawAlreadyHasComment = false;
}

inline void CountDiagnostics(const fdf::Diagnostic& diagnostic) noexcept
{
    fdf::test::g_diagnostics++;
    fdf::test::g_lastDiagnostic = diagnostic;
    if(diagnostic.type == fdf::DiagnosticType::InvalidIdentifier)
        fdf::test::g_sawInvalidIdentifier = true;
    if(diagnostic.type == fdf::DiagnosticType::AlreadyHasComment)
        fdf::test::g_sawAlreadyHasComment = true;
}

[[nodiscard]] constexpr std::string_view FirstString(const fdf::Entry& e) noexcept
{
    const std::span<const fdf::String> parts = e.GetValue<fdf::String>();
    return parts.empty()? std::string_view{} : std::string_view(parts[0]);
}

template<typename T>
[[nodiscard]] constexpr bool HexEquals(const fdf::Hex& hex, const T& expected, const size_t byteOffset = 0) noexcept
{
    T value{};
    return hex.Read(value, byteOffset) && value == expected;
}

struct Rgb { uint8_t r = 0, g = 0, b = 0; };
[[nodiscard]] constexpr bool ReadHex(fdf::HexReader& reader, Rgb& value) noexcept
{
    return reader.Read(value.r) && reader.Read(value.g) && reader.Read(value.b);
}
[[nodiscard]] constexpr bool WriteHex(fdf::HexWriter& writer, const Rgb& value) noexcept
{
    return writer.Write(value.r) && writer.Write(value.g) && writer.Write(value.b);
}

// WriteHex only
struct RgbWriteOnly { uint8_t r = 0, g = 0, b = 0; };
[[nodiscard]] constexpr bool WriteHex(fdf::HexWriter& writer, const RgbWriteOnly& value) noexcept
{
    return writer.Write(value.r) && writer.Write(value.g) && writer.Write(value.b);
}

struct EmptyHook {};
[[nodiscard]] constexpr bool WriteHex(fdf::HexWriter&, const EmptyHook&) noexcept
{
    return true;
}

struct HookFailure { uint8_t byte; };
[[nodiscard]] constexpr bool WriteHex(fdf::HexWriter& writer, const HookFailure& value) noexcept
{
    return writer.Write(value.byte) && false;
}

struct CountedUnbounded
{
    int* calls;
    uint8_t byte;
};
[[nodiscard]] constexpr bool WriteHex(fdf::HexWriter& writer, const CountedUnbounded& value) noexcept
{
    (*value.calls)++;
    return writer.Write(value.byte);
}

struct GrowingUnbounded { int* calls; };
[[nodiscard]] constexpr bool WriteHex(fdf::HexWriter& writer, const GrowingUnbounded& value) noexcept
{
    (*value.calls)++;
    return writer.Write(uint64_t{0x0102030405060708ull});
}

struct ResourceValue { fdf::String bytes; };
[[nodiscard]] constexpr bool WriteHex(fdf::HexWriter& writer, const ResourceValue& value) noexcept
{
    for(const char byte : value.bytes)
    {
        if(!writer.Write(static_cast<uint8_t>(byte)))
            return false;
    }
    return true;
}

struct NestedHook
{
    RgbWriteOnly color;
    uint8_t tail;
};
[[nodiscard]] constexpr bool WriteHex(fdf::HexWriter& writer, const NestedHook& value) noexcept
{
    return writer.Write(value.color) && writer.Write(value.tail);
}

struct ReadNested
{
    Rgb color;
    uint8_t tail;
};
[[nodiscard]] constexpr bool ReadHex(fdf::HexReader& reader, ReadNested& value) noexcept
{
    return reader.Read(value.color) && reader.Read(value.tail);
}

struct PoisonParent { uint8_t tail; };
[[nodiscard]] constexpr bool WriteHex(fdf::HexWriter& writer, const PoisonParent& value) noexcept
{
    (void)writer.Write(HookFailure{ 0x11 });
    return writer.Write(value.tail);
}

[[nodiscard]] constexpr bool WritePair(fdf::HexWriter& writer, const uint8_t a, const uint8_t b) noexcept
{
    return writer.Write(a) && writer.Write(b);
}

struct HelperComposed { uint8_t head = 0, a = 0, b = 0, tail = 0; };
[[nodiscard]] constexpr bool WriteHex(fdf::HexWriter& writer, const HelperComposed& value) noexcept
{
    return writer.Write(value.head)
        && WritePair(writer, value.a, value.b) && writer.Write(value.tail);
}

struct ReaderPoison
{
    uint16_t wide = 0xCAFE;
    uint8_t tail = 0xA5;
};
[[nodiscard]] constexpr bool ReadHex(fdf::HexReader& reader, ReaderPoison& value) noexcept
{
    (void)reader.Read(value.wide);
    return reader.Read(value.tail);
}

struct HookLess { uint16_t a; };
template<typename T>
inline constexpr bool HexTransfers = requires(fdf::Hex& h, T& v) { h.Read(v); h.Write(v); };
template<typename T>
inline constexpr bool HexWrites = requires(fdf::Hex& h, const T& v) { h.Write(v); };
static_assert(HexTransfers<uint32_t> && HexTransfers<Rgb> && !HexTransfers<HookLess>);
static_assert(HexWrites<RgbWriteOnly> && !HexWrites<HookLess>);
static_assert(fdf::detail::HasHexWriter<Rgb> && fdf::detail::HasHexWriter<RgbWriteOnly>
    && !fdf::detail::HasHexWriter<HookLess>);

[[nodiscard]] constexpr bool FirstIntEquals(const fdf::Entry* e, int64_t expected) noexcept
{
    if(!e)
        return false;
    const auto values = e->GetValue<int64_t>();
    return values.size() == 1 && values[0] == expected;
}

namespace fdf::test
{

    // cases that print their own output call this first so the open RUN line is closed
    inline void CloseCaseLine() noexcept
    {
        if(!g_bCaseLineOpen)
            return;
        std::println();
        g_bCaseLineOpen = false;
    }

    inline bool ReportCheck(bool bCond, const char* expr, const char* file, int line, std::string_view msg = {}) noexcept
    {
        g_checks++;
        g_caseChecks++;
        if(bCond)
            return true;

        g_failed++;
        g_caseFailed++;
        CloseCaseLine();
        if(msg.empty())
            std::println("  [FAIL] {}:{}  {}", file, line, expr);
        else
            std::println("  [FAIL] {}:{}  {}  --  {}", file, line, expr, msg);
        std::fflush(stdout);
        return false;
    }

    inline void RunCase(std::string_view name, void (*caseFn)()) noexcept
    {
        g_caseChecks = 0;
        g_caseFailed = 0;
        if(g_bIsTty)
        {
            std::print("[ RUN  ] {}", name);
            std::fflush(stdout);   // std::print does not flush, the RUN line must show while the case runs
            g_bCaseLineOpen = true;
        }
        const auto startTime = std::chrono::steady_clock::now();
        caseFn();
        const double tookMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();

        std::string result;
        if(g_caseFailed == 0)
            result = std::format("[ PASS ] {} ({} checks) -- {:.2f}ms", name, g_caseChecks, tookMs);
        else
        {
            result = std::format("[ FAIL ] {} ({}/{} checks failed) -- {:.2f}ms", name, g_caseFailed, g_caseChecks, tookMs);
            g_failedCases.emplace_back(name);
        }

        if(g_bCaseLineOpen)
        {
            g_bCaseLineOpen = false;
            // pad so the overwrite leaves no leftover characters from the RUN line
            const size_t runLineLength = 9 + name.size();
            if(result.size() < runLineLength)
                result.append(runLineLength - result.size(), ' ');
            std::println("\r{}", result);
        }
        else
            std::println("{}", result);
        std::fflush(stdout);   // pipes are fully buffered, flush so each case streams live
    }
}

#define CHECK(cond)        fdf::test::ReportCheck((cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, m) fdf::test::ReportCheck((cond), #cond, __FILE__, __LINE__, (m))

// CHECK that returns before dependent assertions can dereference bad state
#define REQUIRE(cond)        do { if(!CHECK(cond))          return; } while(false)
#define REQUIRE_MSG(cond, m) do { if(!CHECK_MSG(cond, (m))) return; } while(false)

// GCC cannot infer pointer validity through ReportCheck
#define REQUIRE_PTR(p) do { if(!CHECK((p) != nullptr) || (p) == nullptr) return; } while(false)




namespace fdf::detail
{
    struct TestDirectories
    {
        TestDirectories(const std::filesystem::path& file)
        {
            inputFile = file.generic_string();
            outputFile = FDF_OUTPUT_DIRECTORY;
            outputFile.push_back('/');
            outputFile.append(file.stem().generic_string());

            tokenizedFile = outputFile + "-Tokenized.txt";
            entriesFile = outputFile + "-Entries.txt";
            outputFile = outputFile + "-Output.fdf";
        }

        std::string inputFile, outputFile, tokenizedFile, entriesFile;
    };



    std::vector<TestDirectories> filesToTest;
    size_t longestFilename = 0;
    std::string output;




    struct Test
    {
        static bool PrintAllTokens(std::string_view inFile, std::string_view outFile)
        {
            std::ifstream iFile(inFile.data());
            std::ofstream oFile(outFile.data());
            if(!iFile || !oFile)
                return false;

            iFile.seekg(0, std::ios::end);
            const std::streampos fileSize = iFile.tellg();
            if(fileSize < 0 || static_cast<uintmax_t>(fileSize) > String::max_size())
                return false;
            String content(static_cast<size_t>(fileSize), '\0');
            iFile.seekg(0, std::ios::beg);
            if(!content.empty() && !iFile.read(content.data(), static_cast<std::streamsize>(content.size())))
                return false;
            Tokenizer tokenizer = Tokenizer(content);

            std::vector<Token> tokens;
            std::vector<std::string_view> views;
            tokens.push_back(tokenizer.Current());
            views.push_back(tokenizer.ToView(tokens[0]));
            while(true)
            {
                Token token = tokenizer.Advance();
                tokens.push_back(token);
                views.push_back(tokenizer.ToView(token));

                if(token.type == TokenType::EndOfFile || token.type == TokenType::Invalid)
                    break;
            }

            String buffer;
            auto addToBuffer = [&buffer](std::string_view value)
            {
                for(char c : value)
                {
                    if(c == '\n')
                        buffer.append("\\n");
                    else
                        buffer.push_back(c);
                }
            };

            size_t tokenIndex = 0;
            while(tokenIndex < tokens.size())
            {
                addToBuffer(std::format("id={:03}--Type={}--Extra8={:02}--Value={}", tokenIndex, TOKEN_TYPE_TO_STRING[static_cast<size_t>(tokens[tokenIndex].type)], tokens[tokenIndex].extra8, views[tokenIndex]));
                buffer.push_back('\n');
                tokenIndex++;
            }

            oFile << std::string_view(buffer);
            return static_cast<bool>(oFile);
        }

        static bool PrintAllEntries(const Entry* e, std::string_view outFile)
        {
            std::ofstream file(outFile.data());
            if(!file)
                return false;

            String buffer;
            auto addToBuffer = [&buffer](std::string_view value)
            {
                for(char c : value)
                {
                    if(c == '\n')
                        buffer.append("\\n");
                    else
                        buffer.push_back(c);
                }
            };

            String temp;
            e->ForEach<ForEachFlags::Recursive | ForEachFlags::Group>([&](const Entry& entry)
            {
                addToBuffer(std::format("{:<{}}Type={}--Size={:03}--Name={:<20}--Value={:<50}--Comment={}", "", 4 * entry.CalculateDepth(), ENTRY_TYPE_TO_STRING[static_cast<size_t>(entry.GetType())], entry.GetChildCount(), std::string_view(entry.GetFullIdentifier()), entry.DataToView(temp), std::string_view(entry.GetComment())));
                buffer.push_back('\n');
            });

            file << std::string_view(buffer);
            return static_cast<bool>(file);
        }

        template<Style STYLE = {}>
        static bool PrintFile(const Entry* e, std::string_view outFile)
        {
            return WriteFile<STYLE>(*e, outFile);
        }




        static void ParseTest()
        {
            fdf::test::CloseCaseLine();
            for(size_t i = 0; i < filesToTest.size(); i++)
            {
                const TestDirectories& directories = filesToTest[i];
                std::print("[{:02}/{:02}] {:<{}}", i + 1, filesToTest.size(), directories.inputFile, longestFilename);

                auto startTime = std::chrono::high_resolution_clock::now();
                UniqueEntryPtr e = ParseFile(std::filesystem::path(directories.inputFile));
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = duration_cast<std::chrono::nanoseconds>(endTime - startTime);

                String durationString;
                std::format_to(std::back_inserter(durationString), "{:.6f}ms",
                    static_cast<double>(duration.count()) / 1'000'000.0);
                std::print(" -- Result: {:<7} -- Took: {:<9}", e? "SUCCESS" : "FAIL", durationString);

                if(!output.empty())
                {
                    std::println("{}", output);
                    output.clear();
                }
                std::println();

                PrintAllTokens(directories.inputFile, directories.tokenizedFile);

                CHECK_MSG(static_cast<bool>(e), directories.inputFile);
                if(e)
                {
                    PrintAllEntries(e.get(), directories.entriesFile);
                    PrintFile<Style{.bCommas = false}>(e.get(), directories.outputFile);
                }
            }
        }




        // READ_DOC keeps structural expectations beside the fixture
        // covers scalars, packs, containers and string escapes
        static constexpr std::string_view READ_DOC =
            "appVersion = 1.0.0.0\n"
            "name = \"MyGame\"\n"
            "enabled1 = true\n"
            "id = 12345\n"
            "uuid = \"a123-xyz\"\n"
            "pi = 3.14\n"
            "value = null\n"
            "value2 = nil\n"
            "escaped5 = \"asd\\tasd\\p\"\n"
            "escaped6 = \"\\\\asd\\\\\"\n"
            "authors = \"ann\"|\"bo\"\n"
            "timeout = 1h30m\n"
            "delays = 1s|500ms\n"
            "gameSettings1 {\n"
            "    resolution = 1920|1080\n"
            "    fullscreen = false\n"
            "    tags [ \"a\", \"b\", \"c\" ]\n"
            "}\n"
            "players [\n"
            "    { name = \"p1\", score = 10 },\n"
            "    { name = \"p2\", score = 20 }\n"
            "]\n";

        static void ReadTest()
        {
            UniqueEntryPtr e = ParseBuffer(READ_DOC);
            CHECK_MSG(static_cast<bool>(e), "Failed to parse the embedded read document");
            if(!e)
                return;

            // Derived from READ_DOC: 15 top-level entries, recursive adds gameSettings1's 3 children
            // + 3 tag elements, and players' 2 maps + their 4 leaves = 27
            CHECK_MSG(e->GetChildCountRecursive() == 27, std::format("recursive count = {}", e->GetChildCountRecursive()));
            CHECK_MSG(e->GetChildCount() == 15, std::format("top level count = {}", e->GetChildCount()));

            if(Entry* entry = e->GetChild("appVersion"); CHECK(entry && entry->GetType() == Type::Version))
            {
                auto val = entry->GetValue<Version>();
                CHECK(val.size() == 1 && val[0].bHasRevision && val[0].major == 1 && val[0].minor == 0 && val[0].patch == 0 && val[0].revision == 0);
            }

            if(Entry* entry = e->GetChild("name"); CHECK(entry && entry->GetType() == Type::String))
                CHECK(FirstString(*entry) == "MyGame");

            if(Entry* entry = e->GetChild("enabled1"); CHECK(entry && entry->GetType() == Type::Bool))
            {
                auto val = entry->GetValue<bool>();
                CHECK(val.size() == 1 && val[0] == true);
            }

            if(Entry* entry = e->GetChild("id"); CHECK(entry && entry->GetType() == Type::Int))
            {
                auto val = entry->GetValue<int64_t>();
                CHECK(val.size() == 1 && val[0] == 12345);
            }

            if(Entry* entry = e->GetChild("uuid"); CHECK(entry && entry->GetType() == Type::String))
                CHECK(FirstString(*entry) == "a123-xyz");

            if(Entry* entry = e->GetChild("pi"); CHECK(entry && entry->GetType() == Type::Float))
            {
                auto val = entry->GetValue<double>();
                CHECK(val.size() == 1 && val[0] > 3.13 && val[0] < 3.15);
            }

            if(Entry* entry = e->GetChild("value"); CHECK(entry))
                CHECK(entry->GetType() == Type::Null);

            if(Entry* entry = e->GetChild("value2"); CHECK(entry))
                CHECK(entry->GetType() == Type::Null);

            if(Entry* entry = e->GetChild("authors"); CHECK(entry && entry->GetType() == Type::String))
            {
                const std::span<const String> parts = entry->GetValue<String>();
                CHECK(parts.size() == 2 && parts[0] == "ann" && parts[1] == "bo");
            }

            if(Entry* entry = e->GetChild("timeout"); CHECK(entry && entry->GetType() == Type::Duration))
            {
                const std::span<const Duration> parts = entry->GetValue<Duration>();
                CHECK(parts.size() == 1 && parts[0] == Duration::Minutes(90));
            }

            if(Entry* entry = e->GetChild("delays"); CHECK(entry && entry->GetType() == Type::Duration))
            {
                const std::span<const Duration> parts = entry->GetValue<Duration>();
                CHECK(parts.size() == 2 && parts[0] == Duration::Seconds(1) && parts[1] == Duration::Millis(500));
            }

            if(Entry* entry = e->GetChild("gameSettings1.resolution"); CHECK(entry && entry->GetType() == Type::Int))
            {
                auto val = entry->GetValue<int64_t>();
                CHECK(val.size() == 2 && val[0] == 1920 && val[1] == 1080);
            }

            CHECK(e->GetChild("NON_EXISTING") == nullptr);

            // Escape handling: \t -> tab, unknown \p kept literally
            if(Entry* entry = e->GetChild("escaped5"); CHECK(entry && entry->GetType() == Type::String))
                CHECK(FirstString(*entry) == "asd\tasd\\p");

            // doubled backslash collapses to one
            if(Entry* entry = e->GetChild("escaped6"); CHECK(entry && entry->GetType() == Type::String))
                CHECK(FirstString(*entry) == "\\asd\\");
        }




        static void WriteTest()
        {
            UniqueEntryPtr root = NewEntry();
            if(!CHECK(static_cast<bool>(root)))
                return;

            if(Entry* e = root->Emplace("name"); CHECK(e))
                e->SetValue("Test");

            if(Entry* e = root->Emplace("pi"); CHECK(e))
                e->SetValue(3.14);

            if(Entry* e = root->Emplace("position"); CHECK(e))
            {
                double position[3] = {0.0, 0.0, 100.0};
                e->SetValue(std::span(position, 3));
            }

            if(Entry* e = root->Emplace("results"); CHECK(e))
            {
                e->SetValue(ArrayType());

                if(Entry* ee = e->Emplace(""); CHECK(ee))
                    ee->SetValue(42);
                if(Entry* ee = e->Emplace(""); CHECK(ee))
                    ee->SetValue(0.75f);
                if(Entry* ee = e->Emplace(""); CHECK(ee))
                    ee->SetValue(false);
                if(Entry* ee = e->Emplace(""); CHECK(ee))
                    ee->SetValue("UNKNOWN!");

                if(Entry* ee = e->Emplace(""); CHECK(ee))
                {
                    ee->SetValue(MapType());

                    if(Entry* eee = ee->Emplace("found"); CHECK(eee))
                        eee->SetValue(true);
                    if(Entry* eee = ee->Emplace("value"); CHECK(eee))
                        eee->SetValue(815);
                }
            }

            CHECK((PrintFile<Style{.singleLineContainerLimit = 40}>(root.get(), FDF_OUTPUT_DIRECTORY "/WriteTest.fdf")));

            // Round-trip: re-parse what we wrote and verify a couple of values survived
            UniqueEntryPtr reparsed = ParseFile(std::filesystem::path(FDF_OUTPUT_DIRECTORY "/WriteTest.fdf"));
            if(CHECK_MSG(static_cast<bool>(reparsed), "WriteTest round-trip parse"))
            {
                if(Entry* e = reparsed->GetChild("name"); CHECK(e && e->GetType() == Type::String))
                    CHECK(FirstString(*e) == "Test");
                if(Entry* e = reparsed->GetChild("results.4.value"); CHECK(e && e->GetType() == Type::Int))
                {
                    auto val = e->GetValue<int64_t>();
                    CHECK(val.size() == 1 && val[0] == 815);
                }
            }

            const std::filesystem::path relativePath = "WriteRelative.fdf";
            std::error_code ec;
            std::filesystem::remove(relativePath, ec);
            CHECK(!WriteFile(*root, relativePath, false));
            CHECK(WriteFile(*root, relativePath, true));
            CHECK(WriteFile(*root, relativePath, false));
            CHECK(static_cast<bool>(ParseFile(relativePath)));
            std::filesystem::remove(relativePath, ec);

            const std::filesystem::path nestedDir = FDF_OUTPUT_DIRECTORY "/new/nested";
            const std::filesystem::path nestedFile = nestedDir / "created.fdf";
            std::filesystem::remove_all(FDF_OUTPUT_DIRECTORY "/new", ec);
            CHECK(!WriteFile(*root, nestedFile, false));
            CHECK(!std::filesystem::exists(nestedFile));
            CHECK(WriteFile(*root, nestedFile, true));
            CHECK(std::filesystem::is_regular_file(nestedFile));
            CHECK(static_cast<bool>(ParseFile(nestedFile)));
            CHECK(!WriteFile(*root, nestedDir, true));

            {
                const std::filesystem::path replaced = nestedDir / "replace.fdf";
                std::filesystem::remove(replaced, ec);
                CHECK(WriteFile(*root, replaced, true));
                CHECK(WriteFile(*root, replaced, false));
                CHECK(std::filesystem::is_regular_file(replaced));
                CHECK(static_cast<bool>(ParseFile(replaced)));
                CHECK(!std::filesystem::exists(replaced.string() + ".0.tmp"));

                const std::filesystem::path blockedTemp = replaced.string() + ".0.tmp";
                CHECK(std::filesystem::create_directory(blockedTemp, ec) && !ec);
                CHECK(WriteFile(*root, replaced, false));
                CHECK(std::filesystem::is_directory(blockedTemp));
                CHECK(!std::filesystem::exists(replaced.string() + ".1.tmp"));
                std::filesystem::remove(blockedTemp, ec);
            }

            const std::filesystem::path emptyFile = nestedDir / "empty.fdf";
            { std::ofstream createEmpty(emptyFile, std::ios::binary); }
            if(UniqueEntryPtr empty = ParseFile(emptyFile); CHECK(static_cast<bool>(empty)))
                CHECK(empty->GetChildCount() == 0);

            const std::filesystem::path combineFile = nestedDir / "combine.fdf";
            {
                std::ofstream file(combineFile, std::ios::binary);
                file << "fromFile=7\r\nsection { nested=8 }\r\n";
            }
            UniqueEntryPtr combined = ParseBuffer("existing=6\n");
            if(CHECK(combined && combined->ParseCombineFile(combineFile)))
            {
                CHECK(combined->GetChild("existing") && combined->GetChild("fromFile"));
                if(Entry* nested = combined->GetChild("section.nested"); CHECK(nested))
                    CHECK(FirstIntEquals(nested, 8));
            }
            std::filesystem::remove_all(FDF_OUTPUT_DIRECTORY "/new", ec);
        }




        // SetValue/GetValue scalar and span coverage
        static void ValueTest()
        {
            static_assert(std::is_same_v<decltype(std::declval<const Entry&>().GetChildren()), std::span<const Entry* const>>);
            static_assert(std::is_same_v<decltype(std::declval<      Entry&>().GetChildren()), std::span<      Entry*      >>);

            UniqueEntryPtr root = NewEntry();
            if(!CHECK(static_cast<bool>(root)))
                return;

            if(Entry* e = root->Emplace("i"); CHECK(e))
            {
                e->SetValue(-7);
                auto v = e->GetValue<int64_t>();
                CHECK(e->GetType() == Type::Int && v.size() == 1 && v[0] == -7);
            }

            if(Entry* e = root->Emplace("u"); CHECK(e))
            {
                e->SetValue(42u);
                auto v = e->GetValue<uint64_t>();
                CHECK(e->GetType() == Type::Int && v.size() == 1 && v[0] == 42u);
            }

            if(Entry* e = root->Emplace("f"); CHECK(e))
            {
                e->SetValue(2.5);
                auto v = e->GetValue<double>();
                CHECK(e->GetType() == Type::Float && v.size() == 1 && v[0] == 2.5);
            }

            if(Entry* e = root->Emplace("b"); CHECK(e))
            {
                e->SetValue(true);
                auto v = e->GetValue<bool>();
                CHECK(e->GetType() == Type::Bool && v.size() == 1 && v[0] == true);
            }

            if(Entry* e = root->Emplace("s"); CHECK(e))
            {
                e->SetValue("hello");
                CHECK(e->GetType() == Type::String && FirstString(*e) == "hello");
            }

            // Span overloads: set from an array reference, read every element back
            if(Entry* e = root->Emplace("ints"); CHECK(e))
            {
                int64_t ints[3] = { 10, -20, 30 };
                e->SetValue(std::span(ints, 3));
                auto v = e->GetValue<int64_t>();
                CHECK(e->GetType() == Type::Int && v.size() == 3 && v[0] == 10 && v[1] == -20 && v[2] == 30);
            }

            if(Entry* e = root->Emplace("uints"); CHECK(e))
            {
                uint64_t uints[2] = { 1u, 2u };
                e->SetValue(std::span(uints, 2));
                auto v = e->GetValue<uint64_t>();
                CHECK(e->GetType() == Type::Int && v.size() == 2 && v[0] == 1u && v[1] == 2u);
            }

            if(Entry* e = root->Emplace("dbls"); CHECK(e))
            {
                double dbls[3] = { 1.0, 2.5, 3.0 };
                e->SetValue(std::span(dbls, 3));
                auto v = e->GetValue<double>();
                CHECK(e->GetType() == Type::Float && v.size() == 3 && v[0] == 1.0 && v[1] == 2.5 && v[2] == 3.0);
            }

            // overwrites reuse the buffer: same storage type via resize, other types retype in place
            if(Entry* e = root->Emplace("reuse"); CHECK(e))
            {
                int64_t big[4] = { 1, 2, 3, 4 };
                e->SetValue(std::span(big, 4));
                const void* p = e->GetValue<int64_t>().data();
                int64_t small[2] = { 9, 8 };
                e->SetValue(std::span(small, 2));
                auto v = e->GetValue<int64_t>();
                CHECK(v.data() == p && v.size() == 2 && v[0] == 9 && v[1] == 8);
                e->SetValue(3.5);
                CHECK(e->GetType() == Type::Float && e->GetValue<double>()[0] == 3.5
                    && static_cast<const void*>(e->GetValue<double>().data()) == p);
            }

            if(Entry* e = root->Emplace("reuseStr"); CHECK(e))
            {
                std::string_view parts[2] = { "alpha", "beta" };
                e->SetValue(std::span<const std::string_view>(parts, 2));
                const void* p = e->GetValue<String>().data();
                int64_t ints[2] = { 5, 6 };
                e->SetValue(std::span(ints, 2));   // String -> Int retypes the block, frees the char payloads
                auto v = e->GetValue<int64_t>();
                CHECK(e->GetType() == Type::Int && static_cast<const void*>(v.data()) == p && v[0] == 5 && v[1] == 6);

                e->SetValue(std::span<const std::string_view>());   // empty span -> one empty string, no stale text
                auto s = e->GetValue<String>();
                CHECK(e->GetType() == Type::String && s.size() == 1 && s[0].empty());
            }

            // container and value blocks interchange freely: children die, bytes stay
            if(Entry* e = root->Emplace("reuseKids"); CHECK(e))
            {
                e->SetValue(MapType{});
                CHECK(e->Emplace("a") && e->Emplace("b"));
                const void* p = static_cast<const void*>(e->GetChildren().data());
                e->SetValue(ArrayType{});
                CHECK(e->GetType() == Type::Array && e->GetChildCount() == 0);
                Entry* kid = e->Emplace("c");
                CHECK(kid && e->GetChildCount() == 1
                    && static_cast<const void*>(e->GetChildren().data()) == p);

                int64_t ints[2] = { 7, 8 };
                e->SetValue(std::span(ints, 2));   // container -> value, child dies with the retype
                auto v = e->GetValue<int64_t>();
                CHECK(e->GetType() == Type::Int && static_cast<const void*>(v.data()) == p
                    && v.size() == 2 && v[0] == 7 && v[1] == 8);

                e->SetValue(MapType{});            // value -> container, block becomes child capacity
                Entry* again = e->Emplace("d");
                CHECK(again && e->GetChildCount() == 1
                    && static_cast<const void*>(e->GetChildren().data()) == p);
            }

            // retype across element sizes: capacity converts to the new unit, block stays
            if(Entry* e = root->Emplace("reuseSizes"); CHECK(e))
            {
                e->SetValue(true);
                const void* p = static_cast<const void*>(e->GetValue<bool>().data());
                e->SetValue(static_cast<int64_t>(-9));
                // a bool scalar block fits an int64 only via slab bucket slack (MIN_BUCKET 8)
                const bool bSameBlock = FDF_DISABLE_SLAB_ALLOCATOR
                    || static_cast<const void*>(e->GetValue<int64_t>().data()) == p;
                CHECK(e->GetType() == Type::Int && bSameBlock && e->GetValue<int64_t>()[0] == -9);

                Version vers[2] = { { .major = 1, .minor = 2, .patch = 3 }, { .major = 4, .minor = 5, .patch = 6 } };
                e->SetValue(std::span<const Version>(vers, 2));   // 8 -> 32 bytes, must reallocate
                const void* pv = static_cast<const void*>(e->GetValue<Version>().data());
                double dbls[4] = { 1.0, 2.0, 3.0, 4.0 };
                e->SetValue(std::span(dbls, 4));   // Version[2] -> Float[4], same 32 bytes
                auto f = e->GetValue<double>();
                CHECK(e->GetType() == Type::Float && static_cast<const void*>(f.data()) == pv
                    && f.size() == 4 && f[0] == 1.0 && f[3] == 4.0);
            }

            if(Entry* e = root->Emplace("bools"); CHECK(e))
            {
                bool bools[3] = { true, false, true };
                e->SetValue(std::span(bools, 3));
                auto v = e->GetValue<bool>();
                CHECK(e->GetType() == Type::Bool && v.size() == 3 && v[0] == true && v[1] == false && v[2] == true);
            }

            if(Entry* e = root->Emplace("versions"); CHECK(e))
            {
                const Version versions[2] =
                {
                    { .bHasRevision = false, .major = 1, .minor = 2, .patch = 3, .revision = 0 },
                    { .bHasRevision = true,  .major = 4, .minor = 5, .patch = 6, .revision = 0 }
                };
                e->SetValue(std::span<const Version>(versions));
                const std::span<Version> v = e->GetValue<Version>();
                CHECK(e->GetType() == Type::Version);
                REQUIRE(v.size() == 2);
                CHECK(!v[0].bHasRevision && v[0].major == 1 && v[0].minor == 2 && v[0].patch == 3 && v[0].revision == 0);
                CHECK(v[1].bHasRevision && v[1].major == 4 && v[1].minor == 5 && v[1].patch == 6 && v[1].revision == 0);
                v[0].patch = 9;
                const std::span<const Version> reread = std::as_const(*e).GetValue<Version>();
                REQUIRE(reread.size() == 2);
                CHECK(reread[0].patch == 9);
            }

            if(Entry* e = root->Emplace("t"); CHECK(e))
            {
                const Timestamp ts = Timestamp::DateTime(2024, 12, 24, 15, 30, 0);
                e->SetValue(ts);
                CHECK(e->GetType() == Type::Timestamp);
                const std::span<Timestamp> got = e->GetValue<Timestamp>();
                REQUIRE(got.size() == 1);
                CHECK(got[0].year == 2024 && got[0].month == 12 && got[0].day == 24);
                got[0].minute = 45;
                const std::span<const Timestamp> reread = std::as_const(*e).GetValue<Timestamp>();
                REQUIRE(reread.size() == 1);
                CHECK(reread[0].minute == 45);
            }

            // Re-set overwrites type and value cleanly
            if(Entry* e = root->Emplace("re"); CHECK(e))
            {
                e->SetValue(123);
                e->SetValue("now a string");
                CHECK(e->GetType() == Type::String && FirstString(*e) == "now a string");
            }
        }

        // container growth, removal, orphaning and indexed lookup
        static void MutateTest()
        {
            UniqueEntryPtr root = NewEntry();
            if(!CHECK(static_cast<bool>(root)))
                return;

            // Grow past the initial capacity of 4 to force reallocation
            constexpr uint32_t count = 20;
            for(uint32_t i = 0; i < count; i++)
            {
                if(Entry* e = root->Emplace(std::format("k{}", i)); CHECK(e))
                    e->SetValue(static_cast<int64_t>(i));
            }
            CHECK(root->GetChildCount() == count);

            // GetDirectChild(index) and value integrity after growth
            if(Entry* e = root->GetDirectChild(7u); CHECK(e && e->GetIdentifier() == "k7"))
            {
                auto val = e->GetValue<int64_t>();
                CHECK(val.size() == 1 && val[0] == 7);
            }

            // Remove from the middle, remaining children shift down and stay reachable
            CHECK(root->RemoveChild(0u));
            CHECK(root->GetChildCount() == count - 1);
            if(Entry* e = root->GetDirectChild(0u); CHECK(e))
                CHECK(e->GetIdentifier() == "k1");
            CHECK(root->GetChild("k0") == nullptr);
            CHECK(root->GetChild("k19") != nullptr);

            // Orphan detaches the node without destroying it
            if(UniqueEntryPtr orphan = root->OrphanChild("k10"); CHECK(static_cast<bool>(orphan)) && orphan)
            {
                CHECK(orphan->GetParent() == nullptr);
                CHECK(root->GetChild("k10") == nullptr);
                CHECK(root->GetChildCount() == count - 2);
            }

            CHECK(root->ClearChildren());
            CHECK(root->GetChildCount() == 0);
            if(Entry* e = root->Emplace("afterClear"); CHECK(e))
            {
                e->SetValue(static_cast<int64_t>(42));
                CHECK(root->GetDirectChild(0u) == e);
            }

            if(Entry* group = root->Emplace("group"); CHECK(group))
            {
                CHECK(group->Emplace("a"));
                CHECK(group->Emplace("b"));
                std::vector<UniqueEntryPtr> orphans = group->OrphanChildren();
                CHECK(orphans.size() == 2 && !orphans[0]->GetParent() && !orphans[1]->GetParent());
                CHECK(group->GetChildCount() == 0);
                CHECK(group->Emplace("afterOrphan"));
            }

            // scalar storage must never be read as Entry**
            if(Entry* scalar = root->Emplace("scalar"); CHECK(scalar))
            {
                scalar->SetValue(static_cast<int64_t>(7));
                CHECK(scalar->OrphanChildren().empty());
                CHECK(scalar->GetValue<int64_t>().size() == 1 && scalar->GetValue<int64_t>()[0] == 7);
            }

            // Resize preserves existing components and zero-fills new ones
            if(Entry* e = root->Emplace("vec"); CHECK(e))
            {
                int64_t init[2] = {10, 20};
                e->SetValue(std::span(init, 2));

                e->Resize(4);
                auto grown = e->GetValue<int64_t>();
                CHECK(grown.size() == 4 && grown[0] == 10 && grown[1] == 20 && grown[2] == 0 && grown[3] == 0);

                e->Resize(1);
                auto shrunk = e->GetValue<int64_t>();
                CHECK(shrunk.size() == 1 && shrunk[0] == 10);

                // grow back into freed slack: reuse path, no realloc
                e->Resize(3);
                auto regrown = e->GetValue<int64_t>();
                CHECK(regrown.size() == 3 && regrown[0] == 10 && regrown[1] == 0 && regrown[2] == 0);

                e->Resize(0);
                CHECK(e->GetValue<int64_t>().empty());
                e->Resize(2);
                auto fromEmpty = e->GetValue<int64_t>();
                CHECK(fromEmpty.size() == 2 && fromEmpty[0] == 0 && fromEmpty[1] == 0);
            }

            if(Entry* e = root->Emplace("flags"); CHECK(e))
            {
                e->SetValue(true);
                e->Resize(3);
                auto v = e->GetValue<bool>();
                CHECK(v.size() == 3 && v[0] == true && v[1] == false && v[2] == false);
            }

            // Version rides the flat path (construct_at into the slack, no ownership)
            if(Entry* e = root->Emplace("vers"); CHECK(e))
            {
                const Version vs[2] =
                {
                    { .bHasRevision = false, .major = 1, .minor = 2, .patch = 3, .revision = 0 },
                    { .bHasRevision = true,  .major = 4, .minor = 5, .patch = 6, .revision = 7 }
                };
                e->SetValue(std::span<const Version>(vs));
                e->Resize(3);
                const std::span<const Version> grown = std::as_const(*e).GetValue<Version>();
                REQUIRE(grown.size() == 3);
                CHECK(grown[0].major == 1 && grown[1].major == 4
                    && grown[2].major == 0 && !grown[2].bHasRevision);
                e->Resize(1);
                const std::span<const Version> shrunk = std::as_const(*e).GetValue<Version>();
                REQUIRE(shrunk.size() == 1);
                CHECK(shrunk[0].major == 1);

                // revision without bHasRevision is normalized to 0 on ingest
                e->SetValue(Version{ .bHasRevision = false, .major = 9, .minor = 0, .patch = 0, .revision = 42 });
                const std::span<const Version> normalized = std::as_const(*e).GetValue<Version>();
                REQUIRE(normalized.size() == 1);
                CHECK(normalized[0].revision == 0);
            }

            // String elements own heap chunks: shrink frees the dropped ones, grow adds empties
            if(Entry* e = root->Emplace("strs"); CHECK(e))
            {
                const std::string_view names[2] = { "alpha", "beta" };
                e->SetValue(std::span<const std::string_view>(names));

                e->Resize(4);
                auto grown = e->GetValue<String>();
                CHECK(grown.size() == 4 && grown[0] == "alpha" && grown[1] == "beta"
                    && grown[2] == "" && grown[3] == "");

                e->Resize(1);
                auto shrunk = e->GetValue<String>();
                CHECK(shrunk.size() == 1 && shrunk[0] == "alpha");

                // reuse path after a shrink: destroyed slots get reconstructed empty
                e->Resize(3);
                auto regrown = e->GetValue<String>();
                CHECK(regrown.size() == 3 && regrown[0] == "alpha" && regrown[1] == "" && regrown[2] == "");
                e->GetValue<String>()[1] = "beta";
                CHECK(e->GetValue<String>()[1] == "beta");

                e->Resize(0);
                CHECK(e->GetValue<String>().empty());
                e->Resize(2);
                auto fromEmpty = e->GetValue<String>();
                CHECK(fromEmpty.size() == 2 && fromEmpty[0] == "" && fromEmpty[1] == "");
            }

            // Resize can grow an empty payload after SetType
            if(Entry* e = root->Emplace("retyped"); CHECK(e))
            {
                int64_t nums[5] = { 1, 2, 3, 4, 5 };
                e->SetValue(std::span(nums, 5));
                e->SetType(Type::Bool);
                e->Resize(3);
                auto v = e->GetValue<bool>();
                CHECK(e->GetType() == Type::Bool && v.size() == 3 && v[0] == false && v[1] == false && v[2] == false);
            }

            if(UniqueEntryPtr doc = ParseBuffer("h = 0xFF|0x80\nt = 2024-01-02|2024-03-04\n"))
            {
                if(Entry* h = doc->GetChild("h"); CHECK(h && h->GetType() == Type::Hex))
                {
                    h->Resize(4);
                    auto grown = h->GetValue<Hex>();
                    CHECK(grown.size() == 4 && HexEquals<uint8_t>(grown[0], 0xFF) && HexEquals<uint8_t>(grown[1], 0x80)
                        && grown[2].IsEmpty() && grown[3].IsEmpty());
                    h->Resize(1);
                    auto shrunk = h->GetValue<Hex>();
                    CHECK(shrunk.size() == 1 && HexEquals<uint8_t>(shrunk[0], 0xFF));
                }
                if(Entry* t = doc->GetChild("t"); CHECK(t && t->GetType() == Type::Timestamp))
                {
                    t->Resize(4);
                    auto grown = t->GetValue<Timestamp>();
                    CHECK(grown.size() == 4 && grown[0].day == 2 && grown[1].day == 4
                        && !grown[2].IsValid() && !grown[3].IsValid());
                    t->Resize(1);
                    auto shrunk = t->GetValue<Timestamp>();
                    CHECK(shrunk.size() == 1 && shrunk[0].day == 2);
                }
            }

            if(UniqueEntryPtr paths = ParseBuffer("tags [ \"a\", \"b\" ]\nplayers [ { name=\"p\" } ]\n"))
            {
                const Entry* tag = paths->GetChild("tags.1");
                const Entry* name = paths->GetChild("players.0.name");
                REQUIRE(tag && name);
                CHECK(tag->GetFullIdentifier() == "tags.1");
                CHECK(name->GetFullIdentifier() == "players.0.name");
            }

            UniqueEntryPtr base = ParseBuffer("keep=1\nsection { old=2 }\n");
            UniqueEntryPtr incoming = ParseBuffer("add=3\nsection { fresh=4 }\n");
            if(CHECK(base && incoming && base->Combine(incoming)))
            {
                CHECK(!incoming);
                CHECK(base->GetChild("keep") && base->GetChild("add"));
                Entry* section = base->GetChild("section");
                Entry* fresh = base->GetChild("section.fresh");
                CHECK(section && fresh && fresh->GetParent() == section);
                CHECK(FirstIntEquals(base->GetChild("section.old"), 2));
            }
            UniqueEntryPtr empty;
            CHECK(base && !base->Combine(empty));
            CHECK(base && !base->ParseCombineBuffer("broken=\"unterminated"));
        }




        static void RegressionTest()
        {
            UniqueEntryPtr paths = NewEntry();
            CHECK(paths->GetFullIdentifier().empty());
            Entry* items = paths->Emplace("items");
            if(CHECK(items))
            {
                items->SetValue(ArrayType{});
                for(int64_t i = 0; i < 13; i++)
                {
                    Entry* child = items->Emplace("");
                    CHECK(child);
                    if(child)
                        child->SetValue(i);
                }

                CHECK(items->GetFullIdentifier() == "items");
                Entry* firstItem = items->GetDirectChild(0u);
                Entry* lastItem = items->GetDirectChild(12u);
                CHECK(firstItem && firstItem->GetFullIdentifier() == "items.0");
                CHECK(lastItem && lastItem->GetFullIdentifier() == "items.12");
                CHECK(paths->GetChild("items.12") == lastItem);
                CHECK(paths->GetChild("items.4294967296") == nullptr);
                CHECK(paths->GetChild("items.12x") == nullptr);
                CHECK(paths->GetChild("items.") == nullptr);
                CHECK(paths->GetChild("items..0") == nullptr);

                Entry* elementMap = items->GetDirectChild(5u);
                CHECK(elementMap);
                if(elementMap)
                {
                    elementMap->SetValue(MapType{});
                    Entry* values = elementMap->Emplace("values");
                    CHECK(values);
                    if(values)
                    {
                        values->SetValue(ArrayType{});
                        Entry* first = values->Emplace("");
                        Entry* second = values->Emplace("");
                        CHECK(first && second);
                        if(first && second)
                        {
                            first->SetValue(10);
                            second->SetValue(20);
                            CHECK(second->GetFullIdentifier() == "items.5.values.1");
                        }
                    }
                    CHECK(elementMap->GetFullIdentifier() == "items.5");
                }

                UniqueEntryPtr detachedElement = items->OrphanChild(12u);
                CHECK(detachedElement && detachedElement->GetParent() == nullptr);
                CHECK(detachedElement && detachedElement->GetFullIdentifier().empty());
            }

            Entry* deep = paths->Emplace("a");
            CHECK(deep);
            if(deep)
            {
                deep->SetValue(MapType{});
                deep = deep->Emplace("b");
            }
            if(deep)
            {
                deep->SetValue(MapType{});
                deep = deep->Emplace("c");
            }
            if(deep)
            {
                deep->SetValue(MapType{});
                deep = deep->Emplace("leaf");
            }
            CHECK(deep);
            if(deep)
            {
                deep->SetValue(1);
                CHECK(deep->GetFullIdentifier() == "a.b.c.leaf");
            }

            UniqueEntryPtr detachedNamed = paths->OrphanChild("a");
            CHECK(detachedNamed && detachedNamed->GetFullIdentifier() == "a");
            Entry* detachedLeaf = detachedNamed ? detachedNamed->GetChild("b.c.leaf") : nullptr;
            CHECK(detachedLeaf && detachedLeaf->GetFullIdentifier() == "a.b.c.leaf");

            // quote termination follows backslash parity
            constexpr std::string_view escaped =
                R"(one="a\"b")" "\n"
                R"(two="a\\")" "\n"
                R"(three="a\\\"b")" "\n"
                R"(four="a\\\\")" "\n"
                R"(five="a\\\\\"b")" "\n"
                "after=1\n";
            if(UniqueEntryPtr parsed = ParseBuffer(escaped); CHECK(static_cast<bool>(parsed)))
            {
                auto hasString = [&parsed](std::string_view identifier, std::string_view expected)
                {
                    Entry* child = parsed->GetChild(identifier);
                    return child && FirstString(*child) == expected;
                };
                CHECK(hasString("one", "a\"b"));
                CHECK(hasString("two", "a\\"));
                CHECK(hasString("three", "a\\\"b"));
                CHECK(hasString("four", "a\\\\"));
                CHECK(hasString("five", "a\\\\\"b"));
                Entry* after = parsed->GetChild("after");
                CHECK(FirstIntEquals(after, 1));
            }

        #if !FDF_NO_COMMENTS
            if(UniqueEntryPtr comments = ParseBuffer(
                "/*a*/\na=1\n"
                "/*a/b*/\nb=2\n"
                "/**/\nc=3\n"
                "/*trim */\nd=4\n"
                "/*line\nnext\n*/\ne=5\n"); CHECK(static_cast<bool>(comments)))
            {
                auto commentIs = [&comments](std::string_view identifier, std::string_view expected)
                {
                    Entry* child = comments->GetChild(identifier);
                    return child && child->GetComment() == expected;
                };
                CHECK(commentIs("a", "a"));
                CHECK(commentIs("b", "a/b"));
                CHECK(commentIs("c", ""));
                CHECK(commentIs("d", "trim"));
                CHECK(commentIs("e", "line\nnext"));
            }

            // CRLF input must not leak '\r' into single-line comment text or written output
            if(UniqueEntryPtr crlf = ParseBuffer("a=1 // note\r\nb=2\r\n/*multi\r\nline*/\r\nc=3\r\n"); CHECK(static_cast<bool>(crlf)))
            {
                Entry* a = crlf->GetChild("a");
                CHECK(a && a->GetComment() == "note");
                String out = WriteBuffer<Style{ .bCommas = false }>(*crlf);
                CHECK_MSG(out.contains("// note") && out.contains("// multi line"), out);
                CHECK(!out.contains('\r'));
            }

            auto combinedComment = [](CommentCombineStrategy strategy, std::string_view existing, std::string_view incoming)
            {
                UniqueEntryPtr base = NewEntry();
                UniqueEntryPtr other = NewEntry();
                base->GetComment() = existing;
                other->GetComment() = incoming;
                Entry* a = base->Emplace("a");
                Entry* b = other->Emplace("b");
                if(!a || !b)
                    return std::pair{false, String{}};
                a->SetValue(1);
                b->SetValue(2);
                const bool bCombined = base->Combine(other, strategy);
                return std::pair{bCombined && !other && base->GetChild("a") && base->GetChild("b"), base->GetComment()};
            };
            CHECK((combinedComment(CommentCombineStrategy::UseExisting, "old", "new") == std::pair{true, String("old")}));
            CHECK((combinedComment(CommentCombineStrategy::UseNew, "old", "new") == std::pair{true, String("new")}));
            CHECK((combinedComment(CommentCombineStrategy::UseNewIfExistingIsEmpty, "", "new") == std::pair{true, String("new")}));
            CHECK((combinedComment(CommentCombineStrategy::UseNewIfExistingIsEmpty, "old", "new") == std::pair{true, String("old")}));
            CHECK((combinedComment(CommentCombineStrategy::Merge, "old", "new") == std::pair{true, String("old\nnew")}));
            CHECK((combinedComment(CommentCombineStrategy::Clear, "old", "new") == std::pair{true, String()}));
        #endif

            test::g_lastDiagnostic = {};
            CHECK(static_cast<bool>(ParseBuffer<&CountDiagnostics>(
                "s=\"first\nsecond\"\n"
                "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx=1\n")));
            CHECK(test::g_lastDiagnostic.line == 3 && test::g_lastDiagnostic.column == 1);

            test::g_lastDiagnostic = {};
            CHECK(static_cast<bool>(ParseBuffer<&CountDiagnostics>(
                "/* first\nsecond */\n"
                "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx=1\n")));
            CHECK(test::g_lastDiagnostic.line == 3 && test::g_lastDiagnostic.column == 1);

            UniqueEntryPtr left = NewEntry();
            UniqueEntryPtr right = NewEntry();
            left->SetValue(ArrayType{});
            right->SetValue(ArrayType{});
            Entry* leftValue = left->Emplace("");
            Entry* rightValue1 = right->Emplace("");
            Entry* rightValue2 = right->Emplace("");
            CHECK(leftValue && rightValue1 && rightValue2);
            if(leftValue && rightValue1 && rightValue2)
            {
                leftValue->SetValue(1);
                rightValue1->SetValue(2);
                rightValue2->SetValue(3);
            }
            if(CHECK(left->Combine(right)))
            {
                CHECK(!right && left->GetChildCount() == 3);
                Entry* combined0 = left->GetDirectChild(0u);
                Entry* combined1 = left->GetDirectChild(1u);
                Entry* combined2 = left->GetDirectChild(2u);
                CHECK(combined0 && combined1 && combined2);
                CHECK(FirstIntEquals(combined0, 1));
                CHECK(FirstIntEquals(combined1, 2));
                CHECK(FirstIntEquals(combined2, 3));
                for(Entry* child : left->GetChildren())
                    CHECK(child->GetParent() == left.get());
            }

            UniqueEntryPtr map = ParseBuffer("stable=7\n");
            UniqueEntryPtr incompatible = NewEntry();
            incompatible->SetValue(ArrayType{});
            Entry* incompatibleRaw = incompatible.get();
            CHECK(!map->Combine(incompatible));
            CHECK(incompatible.get() == incompatibleRaw && map->GetChild("stable"));
            const uint32_t stableCount = map->GetChildCount();
            CHECK(!map->ParseCombineBuffer("bad=\"unterminated"));
            CHECK(map->GetChildCount() == stableCount && map->GetChild("stable"));

            UniqueEntryPtr emptyMap = NewEntry();
            CHECK(map->Combine(emptyMap));
            CHECK(!emptyMap && map->GetChildCount() == stableCount);

            String allocatedEmpty;
            allocatedEmpty.reserve(32);
            CHECK(allocatedEmpty.begin() == allocatedEmpty.end() && allocatedEmpty.data() != nullptr);
            String movedEmpty = std::move(allocatedEmpty);
            CHECK(allocatedEmpty.begin() == allocatedEmpty.end());
            CHECK(movedEmpty.begin() == movedEmpty.end() && movedEmpty.data() != nullptr);
            size_t iterations = 0;
            for([[maybe_unused]] char c : movedEmpty)
                iterations++;
            CHECK(iterations == 0);
        }




        // A malformed top-level line should be reported and skipped, not abort the whole parse
        static void RecoveryTest()
        {
            test::g_diagnostics = 0;
            constexpr std::string_view buffer =
                "good1 = 1\n"
                "bad = = 5\n"       // double equals: grammar error
                "= orphan\n"        // line not starting with an identifier
                "good2 = 2\n";

            UniqueEntryPtr e = ParseBuffer<&CountDiagnostics>(buffer);
            if(!CHECK_MSG(static_cast<bool>(e), "parse should recover, not return null"))
                return;

            CHECK(test::g_diagnostics >= 2);
            CHECK(e->GetChildCount() == 2);
            CHECK(e->GetChild("bad") == nullptr);
            CHECK(e->GetChild("orphan") == nullptr);

            if(Entry* g1 = e->GetChild("good1"); CHECK(g1 && g1->GetType() == Type::Int))
            {
                auto v = g1->GetValue<int64_t>();
                CHECK(v.size() == 1 && v[0] == 1);
            }
            if(Entry* g2 = e->GetChild("good2"); CHECK(g2 && g2->GetType() == Type::Int))
            {
                auto v = g2->GetValue<int64_t>();
                CHECK(v.size() == 1 && v[0] == 2);
            }

            // map recovery is the same on one line or several
            auto checkMap = [](std::string_view src)
            {
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(src);
                if(!CHECK_MSG(static_cast<bool>(root), src))
                    return;
                Entry* m = root->GetChild("m");
                if(CHECK(m && m->GetType() == Type::Map) && m)
                {
                    CHECK(m->GetChildCount() == 2);
                    CHECK(m->GetChild("a") != nullptr);
                    CHECK(m->GetChild("b") != nullptr);
                    CHECK(m->GetChild("bad") == nullptr);
                }
            };
            checkMap("m { a = 1, bad = = 5, b = 2 }\n");
            checkMap("m {\n    a = 1\n    bad = = 5\n    b = 2\n}\n");

            // Recovery inside an array (bad element between good ones)
            if(UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("arr [ 1, = , 3 ]\n"); CHECK(static_cast<bool>(root)))
            {
                Entry* arr = root->GetChild("arr");
                if(CHECK(arr && arr->GetType() == Type::Array) && arr)
                    CHECK(arr->GetChildCount() == 2);
            }

            // An over-long identifier (> 30 chars) is reported precisely as InvalidIdentifier and skipped
            test::g_sawInvalidIdentifier = false;
            if(UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa = 1\n"
                   "ok = 2\n"); CHECK(static_cast<bool>(root)))
            {
                CHECK(test::g_sawInvalidIdentifier);
                CHECK(root->GetChildCount() == 1);
                CHECK(root->GetChild("ok") != nullptr);
            }

        #if !FDF_NO_COMMENTS
            // A second comment on the same entry warns with AlreadyHasComment but is not fatal
            test::g_sawAlreadyHasComment = false;
            if(UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(
                   "// first\n"
                   "// second\n"
                   "x = 1\n"); CHECK(static_cast<bool>(root)))
            {
                CHECK(test::g_sawAlreadyHasComment);
                CHECK(root->GetChild("x") != nullptr);
            }

            if(UniqueEntryPtr root = ParseBuffer("/*abc*/\nx=1\n"); CHECK(static_cast<bool>(root)))
            {
                Entry* x = root->GetChild("x");
                CHECK(x && x->GetComment() == "abc");
            }
            CHECK(static_cast<bool>(ParseBuffer("//")));
        #endif

            // consume mismatched closers so recovery makes progress
            {
                test::g_diagnostics = 0;
                if(UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("a { ] }\n"); CHECK(static_cast<bool>(root)))
                {
                    Entry* a = root->GetChild("a");
                    CHECK(a && a->GetType() == Type::Map && a->GetChildCount() == 0);
                }
                CHECK(test::g_diagnostics >= 1);

                CHECK(static_cast<bool>(ParseBuffer<&CountDiagnostics>("a [ } ]\n")));
                CHECK(static_cast<bool>(ParseBuffer<&CountDiagnostics>("a { b { ] } }\n")));

                if(UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("a { ]\n"); CHECK(static_cast<bool>(root)))
                    CHECK(root->GetChild("a") == nullptr);
            }

            // non-finite floats
            {
                test::g_diagnostics = 0;
                if(UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("f = 1.0e9999\n"); CHECK(static_cast<bool>(root)))
                    CHECK(root->GetChild("f") == nullptr);
                CHECK(test::g_diagnostics >= 1);

                UniqueEntryPtr built = NewEntry();
                if(Entry* f = built->Emplace("f"); CHECK(f))
                {
                    f->SetValue(std::numeric_limits<double>::infinity());
                    const String written = WriteBuffer<Style{ .bCommas = false }>(*built);
                    CHECK(written == "f=null\n");
                    UniqueEntryPtr reparsed = ParseBuffer(written);
                    CHECK(reparsed && reparsed->GetChild("f") != nullptr);
                }
            }

            // malformed duplicates preserve the earlier valid value
            {
                for(std::string_view src : { "a=1\na=1.0e9999\n", "a=1\na=5m3h\n", "m { a=1, a=1.0e9999 }\n" })
                {
                    UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(src);
                    if(!CHECK_MSG(static_cast<bool>(root), src))
                        continue;
                    const Entry* scope = src.starts_with("m")? root->GetChild("m") : root.get();
                    const Entry* a = scope? scope->GetChild("a") : nullptr;
                    if(CHECK_MSG(a && a->GetType() == Type::Int, src) && a)
                        CHECK_MSG(FirstIntEquals(a, 1), src);
                }
            }

            // reject ancestor adoption to prevent ownership cycles
            {
                UniqueEntryPtr root = NewEntry();
                Entry* a = root->Emplace("a");
                Entry* b = a? a->Emplace("b") : nullptr;
                if(CHECK(a && b))
                {
                    UniqueEntryPtr orphan = root->OrphanChild(*a);
                    if(CHECK(static_cast<bool>(orphan)))
                    {
                        CHECK(b->AddChild(orphan, DuplicateKeyPolicy::Reject) == nullptr);
                        CHECK(static_cast<bool>(orphan));  // ownership remains with caller
                        CHECK(orphan->AddChild(orphan) == nullptr);
                    }
                }
            }

            // Diagnostic carries line / column / offset of the offending token
            {
                // "ok = 1\n" is 7 bytes; the bad identifier starts at offset 7, line 2, column 1
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(
                    "ok = 1\n"
                    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx = 9\n");
                if(CHECK(static_cast<bool>(root)))
                {
                    CHECK(test::g_lastDiagnostic.type == DiagnosticType::InvalidIdentifier);
                    CHECK(test::g_lastDiagnostic.line == 2);
                    CHECK(test::g_lastDiagnostic.column == 1);
                    CHECK(test::g_lastDiagnostic.offset == 7);
                }
            }

            {
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(
                    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx = 9\n"
                    "ok = 1\n");
                if(CHECK(static_cast<bool>(root)))
                {
                    CHECK(test::g_lastDiagnostic.line == 1);
                    CHECK(test::g_lastDiagnostic.column == 1);
                    CHECK(test::g_lastDiagnostic.offset == 0);
                }
            }

            // An unterminated string is a fatal lexer error reported as UnterminatedString
            {
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("s = \"abc\n");
                CHECK(root == nullptr);
                CHECK(test::g_lastDiagnostic.type == DiagnosticType::UnterminatedString);
                CHECK(test::g_lastDiagnostic.severity == DiagnosticSeverity::Fatal);
            }

            // Invalid UTF-8 is non-fatal: warned with the bad-byte offset, bytes still pass through
            {
                test::g_lastDiagnostic = {};
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("s = \"\xFF\"\n");  // 0xFF at offset 5
                if(CHECK(static_cast<bool>(root)))
                {
                    CHECK(root->GetChild("s") != nullptr);
                    CHECK(test::g_lastDiagnostic.type == DiagnosticType::InvalidUtf8);
                    CHECK(test::g_lastDiagnostic.severity == DiagnosticSeverity::Warning);
                    CHECK(test::g_lastDiagnostic.offset == 5);
                }
            }

            // Valid UTF-8 in a value raises no diagnostic
            {
                test::g_diagnostics = 0;
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("name = \"caf\xC3\xA9\"\n");
                if(CHECK(static_cast<bool>(root)))
                    CHECK(test::g_diagnostics == 0);
            }

            // A leading UTF-8 BOM is stripped, not folded into the first key
            {
                test::g_diagnostics = 0;
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("\xEF\xBB\xBFok = 1\n");
                if(CHECK(static_cast<bool>(root)))
                {
                    CHECK(root->GetChild("ok") != nullptr);
                    CHECK(test::g_diagnostics == 0);
                }
            }
        }


        static void NestingDepthTest()
        {
            auto nestedMap = [](size_t levels)
            {
                std::string src;
                for(size_t i = 0; i < levels; i++)
                    src += "a {";
                src += " x=1 ";
                for(size_t i = 0; i < levels; i++)
                    src += "}";
                return src + "\n";
            };
            auto nestedArray = [](size_t levels)
            {
                std::string src = "v";
                for(size_t i = 0; i < levels; i++)
                    src += "[";
                src += "1";
                for(size_t i = 0; i < levels; i++)
                    src += "]";
                return src + "\n";
            };

            // The root map does not pass through ParseContainer
            constexpr size_t DEEPEST = MAX_PARSE_DEPTH;
            for(const auto& build : { +nestedMap, +nestedArray })
            {
                if(UniqueEntryPtr root = ParseBuffer(build(DEEPEST)); CHECK(static_cast<bool>(root)) && root)
                    CHECK(root->GetChildCount() == 1);

                test::g_diagnostics = 0;
                test::g_lastDiagnostic = {};
                const std::string overLimit = build(DEEPEST + 1) + "after=7\n";
                if(UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(overLimit);
                   CHECK(static_cast<bool>(root)) && root)
                {
                    CHECK(FirstIntEquals(root->GetChild("after"), 7));
                    CHECK(root->GetChildCount() == 2);
                }
                CHECK(test::g_diagnostics >= 1);
                CHECK(test::g_lastDiagnostic.type == DiagnosticType::NestingTooDeep);
                CHECK(test::g_lastDiagnostic.severity == DiagnosticSeverity::Error);
            }

            CHECK(static_cast<bool>(ParseBuffer("a { b { c=1 } }\n")));
        }

        static void DuplicateKeyPolicyTest()
        {
            test::g_diagnostics = 0;
            test::g_lastDiagnostic = {};
            if(UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("a=1\na=2\n"); CHECK(static_cast<bool>(root)))
            {
                CHECK(root->GetChildCount() == 1);
                CHECK(FirstIntEquals(root->GetChild("a"), 1));
            }
            CHECK(test::g_diagnostics == 1);
            CHECK(test::g_lastDiagnostic.type == DiagnosticType::DuplicateKey);
            CHECK(test::g_lastDiagnostic.severity == DiagnosticSeverity::Error);
            CHECK(test::g_lastDiagnostic.line == 2);
            CHECK(test::g_lastDiagnostic.column == 1);
            CHECK(test::g_lastDiagnostic.offset == 4);

            test::g_diagnostics = 0;
            test::g_lastDiagnostic = {};
            if(UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("m { a=1, a=2 }\n"); CHECK(static_cast<bool>(root)))
            {
                Entry* m = root->GetChild("m");
                if(CHECK(m && m->GetType() == Type::Map) && m)
                {
                    CHECK(m->GetChildCount() == 1);
                    CHECK(FirstIntEquals(m->GetChild("a"), 1));
                }
            }
            CHECK(test::g_diagnostics == 1);
            CHECK(test::g_lastDiagnostic.type == DiagnosticType::DuplicateKey);
            CHECK(test::g_lastDiagnostic.severity == DiagnosticSeverity::Error);

            test::g_diagnostics = 0;
            if(UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("a=1\na=2\nb=3\n"); CHECK(static_cast<bool>(root)))
            {
                CHECK(root->GetChildCount() == 2);
                CHECK(FirstIntEquals(root->GetChild("a"), 1));
                CHECK(FirstIntEquals(root->GetChild("b"), 3));
            }
            CHECK(test::g_diagnostics == 1);

            auto orphanValue = [](std::string_view source, std::string_view identifier)
            {
                UniqueEntryPtr owner = ParseBuffer(source);
                return owner? owner->OrphanChild(identifier) : UniqueEntryPtr{};
            };

            {
                UniqueEntryPtr root = ParseBuffer("a=1\n");
                UniqueEntryPtr incoming = orphanValue("a=2\n", "a");
                Entry* existing = root? root->GetChild("a") : nullptr;
                Entry* incomingRaw = incoming.get();
                if(CHECK(root && existing && incoming))
                {
                    Entry* result = root->AddChild(incoming, DuplicateKeyPolicy::Reject);
                    CHECK(result == nullptr);
                    CHECK(incoming.get() == incomingRaw);
                    CHECK(root->GetChild("a") == existing);
                    CHECK(FirstIntEquals(existing, 1));
                }
            }

            {
                UniqueEntryPtr root = ParseBuffer("a=1\n");
                UniqueEntryPtr incoming = orphanValue("a=2\n", "a");
                Entry* existing = root? root->GetChild("a") : nullptr;
                if(CHECK(root && existing && incoming))
                {
                    Entry* result = root->AddChild(incoming, DuplicateKeyPolicy::KeepFirst);
                    CHECK(result == existing);
                    CHECK(!incoming);
                    CHECK(FirstIntEquals(existing, 1));
                }
            }

            {
                UniqueEntryPtr root = ParseBuffer("a=1\n");
                UniqueEntryPtr incoming = orphanValue("a { nested=2 }\n", "a");
                Entry* existing = root? root->GetChild("a") : nullptr;
                Entry* nested = incoming? incoming->GetChild("nested") : nullptr;
                if(CHECK(root && existing && incoming && nested))
                {
                    Entry* result = root->AddChild(incoming, DuplicateKeyPolicy::KeepLast);
                    CHECK(result == existing);
                    CHECK(!incoming);
                    CHECK(existing->GetType() == Type::Map);
                    CHECK(existing->GetChild("nested") == nested);
                    CHECK(nested->GetParent() == existing);
                }
            }

            {
                UniqueEntryPtr root = ParseBuffer("a=1\n");
                UniqueEntryPtr incoming = orphanValue("a=2\n", "a");
                Entry* existing = root? root->GetChild("a") : nullptr;
                if(CHECK(root && existing && incoming))
                {
                    Entry* result = root->AddChild(incoming);
                    CHECK(result == existing);
                    CHECK(!incoming);
                    CHECK(FirstIntEquals(existing, 2));
                }
            }

            {
                UniqueEntryPtr root = ParseBuffer("m { a=1, nested { left=2 } }\n");
                UniqueEntryPtr incoming = orphanValue("m { a=3, b=4, nested { right=5 } }\n", "m");
                Entry* existing = root? root->GetChild("m") : nullptr;
                if(CHECK(root && existing && incoming))
                {
                    Entry* result = root->AddChild(incoming, DuplicateKeyPolicy::Merge);
                    CHECK(result == existing);
                    CHECK(!incoming);
                    CHECK(FirstIntEquals(existing->GetChild("a"), 3));
                    CHECK(FirstIntEquals(existing->GetChild("b"), 4));
                    CHECK(FirstIntEquals(existing->GetChild("nested.left"), 2));
                    CHECK(FirstIntEquals(existing->GetChild("nested.right"), 5));
                }
            }

            {
                UniqueEntryPtr root = ParseBuffer("a=1\n");
                UniqueEntryPtr incoming = orphanValue("a=2\n", "a");
                Entry* existing = root? root->GetChild("a") : nullptr;
                if(CHECK(root && existing && incoming))
                {
                    Entry* result = root->AddChild(incoming, DuplicateKeyPolicy::Merge);
                    CHECK(result == existing);
                    CHECK(!incoming);
                    CHECK(FirstIntEquals(existing, 2));
                }
            }

            {
                UniqueEntryPtr base = ParseBuffer("a=1\n");
                UniqueEntryPtr incoming = ParseBuffer("a=2\nb=3\n");
                Entry* existing = base? base->GetChild("a") : nullptr;
                if(CHECK(base && incoming && existing))
                {
                    CHECK(base->Combine(incoming, CommentCombineStrategy::UseExisting, DuplicateKeyPolicy::Reject));
                    CHECK(!incoming);
                    CHECK(base->GetChild("a") == existing);
                    CHECK(FirstIntEquals(existing, 1));
                    CHECK(FirstIntEquals(base->GetChild("b"), 3));
                }
            }

            {
                UniqueEntryPtr array = NewEntry();
                if(array)
                    array->SetValue(ArrayType{});
                Entry* first = array? array->Emplace("") : nullptr;
                if(first)
                    first->SetValue(1);
                UniqueEntryPtr incoming = orphanValue("value=2\n", "value");
                Entry* incomingRaw = incoming.get();
                if(CHECK(array && first && incoming))
                {
                    Entry* result = array->AddChild(incoming, DuplicateKeyPolicy::Reject);
                    CHECK(result == incomingRaw);
                    CHECK(!incoming);
                    CHECK(array->GetChildCount() == 2);
                    CHECK(array->GetDirectChild(1u) == incomingRaw);
                    CHECK(incomingRaw->GetParent() == array.get());
                    CHECK(FirstIntEquals(incomingRaw, 2));
                }
            }
        }




        // malformed inputs report the expected DiagnosticType
        // fatal lexer errors return no tree and recoverable errors keep valid entries
        static void NegativeTest()
        {
            // type, source and whether the error is fatal
            struct Case { DiagnosticType type; std::string_view src; bool bFatal; };
            constexpr Case cases[] =
            {
                { DiagnosticType::InvalidTimestamp,    "ts = 2024-13-45\nok = 1\n",                          false },
                { DiagnosticType::InvalidTimestamp,    "ts = 25:99:00\nok = 1\n",                            false },
                { DiagnosticType::InvalidTimestamp,    "ts = 2024-359\nok = 1\n",                            false },
                { DiagnosticType::InvalidTimestamp,    "ts = 2024-W52-2\nok = 1\n",                          false },
                { DiagnosticType::InvalidTimestamp,    "ts = 2024-W52-2junk\nok = 1\n",                      false },
                { DiagnosticType::InvalidTimestamp,    "ts = 15:30:00-05:00\nok = 1\n",                      false },
                { DiagnosticType::InvalidTimestamp,    "ts = 15:30:00.1234567890\nok = 1\n",                 false },
                { DiagnosticType::InvalidDuration,     "ts = 9223372036854775808ns\nok = 1\n",                false },
                { DiagnosticType::InvalidDuration,     "ts = 106751d23h47m16s854ms775us808ns\nok = 1\n",      false },
                { DiagnosticType::InvalidDuration,     "ts = 0.5ns\nok = 1\n",                                false },
                { DiagnosticType::InvalidDuration,     "ts = 1s1m\nok = 1\n",                                false },
                { DiagnosticType::InvalidDuration,     "ts = 1h2h\nok = 1\n",                                false },
                { DiagnosticType::InvalidDuration,     "ts = 1mo\nok = 1\n",                                 false },
                { DiagnosticType::InvalidDuration,     "ts = 1sjunk\nok = 1\n",                              false },
                { DiagnosticType::InvalidIdentifier,   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa = 1\nok = 1\n", false },  // > 30 chars
                { DiagnosticType::InvalidIdentifier,   "true = 1\nok = 1\n",                                 false },  // keyword can't be a key
                { DiagnosticType::InvalidIdentifier,   "ts-x = 1\nok = 1\n",                                 false },  // '-' not allowed in identifier
                { DiagnosticType::InvalidIdentifier,   "1ts = 1\nok = 1\n",                                  false },  // identifier can't start with a digit
                { DiagnosticType::InvalidNumber,       "ts = 0xGG\nok = 1\n",                                false },  // malformed value, recoverable
                { DiagnosticType::UnterminatedString,  "s = \"oops\n",                                       true  },
                { DiagnosticType::UnterminatedComment, "x = 1 /* never closed",                              true  },
                { DiagnosticType::InvalidToken,        "v = $\n",                                            true  },
                { DiagnosticType::UnexpectedToken,     "ts = { a = 1 }\nok = 1\n",                           false },  // '=' before map is illegal
                { DiagnosticType::UnexpectedToken,     "ts = [ 1, 2 ]\nok = 1\n",                            false },  // '=' before array is illegal
            };

            for(const Case& c : cases)
            {
                test::g_lastDiagnostic = {};
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(c.src);

                CHECK_MSG(test::g_lastDiagnostic.type == c.type, c.src);
                if(c.bFatal)
                    CHECK_MSG(root == nullptr, c.src);
                else if(CHECK_MSG(static_cast<bool>(root), c.src))
                {
                    CHECK(root->GetChild("ok") != nullptr);  // valid sibling survived recovery
                    CHECK(root->GetChild("ts") == nullptr);  // bad entry was skipped
                }
            }

            // Accepted timestamp forms parse and write canonically
            struct TimestampCase
            {
                std::string_view source;
                std::string_view canonical;
            };
            constexpr TimestampCase goodTs[] =
            {
                { "2024-12-24", "2024-12-24" },
                { "2024-02-29", "2024-02-29" },
                { "15:30:00", "15:30:00" },
                { "15:30:00.1", "15:30:00.1" },
                { "2024-12-24T15:30:00", "2024-12-24T15:30:00" },
                { "2024-12-24t15:30:00z", "2024-12-24T15:30:00Z" },
                { "2024-12-24T15:30:00.123456789Z", "2024-12-24T15:30:00.123456789Z" },
                { "2024-12-24T15:30:00+05:30", "2024-12-24T15:30:00+05:30" },
                { "2024-12-24T15:30:00-05:00", "2024-12-24T15:30:00-05:00" },
            };
            for(const TimestampCase& ts : goodTs)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("t = {}\n", ts.source));
                Entry* e = root? root->GetChild("t") : nullptr;
                if(CHECK_MSG(e && e->GetType() == Type::Timestamp, ts.source) && e)
                {
                    CHECK_MSG(e->GetValue<Timestamp>().size() == 1, ts.source);
                    String out = WriteBuffer<Style{ .bCommas = false }>(*root);
                    CHECK_MSG(out == std::format("t={}\n", ts.canonical), out);
                }
            }

            // a fake oversized view tests the 32-bit input limit without allocating 4GB
            {
                test::g_lastDiagnostic = {};
                static constexpr char oneByte[1] = { 'x' };
                const std::string_view oversized(oneByte, static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
                CHECK(ParseBuffer<&CountDiagnostics>(oversized) == nullptr);
                CHECK(test::g_lastDiagnostic.type == DiagnosticType::InputTooLarge);
            }
        }




        static void PackTest()
        {
            auto checkInt = [](std::string_view src, std::initializer_list<int64_t> exp)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("v = {}\n", src));
                Entry* e = root? root->GetChild("v") : nullptr;
                if(!CHECK_MSG(e && e->GetType() == Type::Int, src) || !e)
                    return;
                const std::span<const int64_t> s = std::as_const(*e).GetValue<int64_t>();
                if(!CHECK_MSG(s.size() == exp.size(), src) || s.size() != exp.size())
                    return;
                size_t i = 0;
                for(int64_t x : exp)
                    CHECK_MSG(s[i++] == x, src);
            };
            auto checkFloat = [](std::string_view src, std::initializer_list<double> exp)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("v = {}\n", src));
                Entry* e = root? root->GetChild("v") : nullptr;
                if(!CHECK_MSG(e && e->GetType() == Type::Float, src) || !e)
                    return;
                const std::span<const double> s = std::as_const(*e).GetValue<double>();
                if(!CHECK_MSG(s.size() == exp.size(), src) || s.size() != exp.size())
                    return;
                size_t i = 0;
                for(double x : exp)
                    CHECK_MSG(s[i++] == x, src);
            };
            auto checkBool = [](std::string_view src, std::initializer_list<bool> exp)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("v = {}\n", src));
                Entry* e = root? root->GetChild("v") : nullptr;
                if(!CHECK_MSG(e && e->GetType() == Type::Bool, src) || !e)
                    return;
                const std::span<const bool> s = std::as_const(*e).GetValue<bool>();
                if(!CHECK_MSG(s.size() == exp.size(), src) || s.size() != exp.size())
                    return;
                size_t i = 0;
                for(bool x : exp)
                    CHECK_MSG(s[i++] == x, src);
            };
            auto checkStrings = [](std::string_view src, std::initializer_list<std::string_view> exp)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("v = {}\n", src));
                Entry* e = root? root->GetChild("v") : nullptr;
                if(!CHECK_MSG(e && e->GetType() == Type::String, src) || !e)
                    return;
                const std::span<const String> parts = std::as_const(*e).GetValue<String>();
                if(!CHECK_MSG(parts.size() == exp.size(), src) || parts.size() != exp.size())
                    return;
                size_t i = 0;
                for(std::string_view x : exp)
                    CHECK_MSG(parts[i++] == x, src);
            };
            auto checkVersions = [](std::string_view src, std::initializer_list<Version> exp)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("v = {}\n", src));
                Entry* e = root? root->GetChild("v") : nullptr;
                if(!CHECK_MSG(e && e->GetType() == Type::Version, src) || !e)
                    return;
                const std::span<const Version> versions = std::as_const(*e).GetValue<Version>();
                if(!CHECK_MSG(versions.size() == exp.size(), src) || versions.size() != exp.size())
                    return;
                size_t i = 0;
                for(const Version& version : exp)
                    CHECK_MSG(versions[i++] == version, src);
            };

            // integer packs with several sizes and negative positions
            checkInt("1|2",         { 1, 2 });
            checkInt("1|2|3",       { 1, 2, 3 });
            checkInt("1|2|3|4",     { 1, 2, 3, 4 });
            checkInt("1|2|3|4|5",   { 1, 2, 3, 4, 5 });
            checkInt("-1|2",        { -1, 2 });
            checkInt("1|-2",        { 1, -2 });
            checkInt("1|2|-3",      { 1, 2, -3 });
            checkInt("-1|-2|-3",    { -1, -2, -3 });
            checkInt("-9223372036854775808", { std::numeric_limits<int64_t>::min() });
            checkInt("-9223372036854775808|0|9223372036854775807",
                { std::numeric_limits<int64_t>::min(), 0, std::numeric_limits<int64_t>::max() });

            // no fixed dimension cap
            checkInt("1|2|3|4|5|6", { 1, 2, 3, 4, 5, 6 });

            // '|' is a structural token, whitespace around it is fine
            checkInt("1 | 2",       { 1, 2 });

            // float, incl exponents and mixed signs
            checkFloat("1.0|2.0",             { 1.0, 2.0 });
            checkFloat("0.5|-0.5|1.0",        { 0.5, -0.5, 1.0 });
            checkFloat("-1.5|2.5|-3.5|4.5",   { -1.5, 2.5, -3.5, 4.5 });
            checkFloat("1.5e3|-2.5",          { 1.5e3, -2.5 });

            // one float component widens the whole pack to float
            checkFloat("1|2.0",       { 1.0, 2.0 });
            checkFloat("1|2.5|3",     { 1.0, 2.5, 3.0 });
            checkFloat("-1|2.5",      { -1.0, 2.5 });
            checkFloat("1|-2.5|3",    { 1.0, -2.5, 3.0 });

            // bool packs
            checkBool("true|false",       { true, false });
            checkBool("true|false|true",  { true, false, true });

            checkVersions("1.2.3", {
                { .bHasRevision = false, .major = 1, .minor = 2, .patch = 3, .revision = 0 }
            });
            checkVersions("1.2.3|4.5.6.0", {
                { .bHasRevision = false, .major = 1, .minor = 2, .patch = 3, .revision = 0 },
                { .bHasRevision = true, .major = 4, .minor = 5, .patch = 6, .revision = 0 }
            });
            checkVersions("2147483647.4294967295.4294967295.4294967295", {
                { .bHasRevision = true, .major = 2147483647, .minor = 4294967295U, .patch = 4294967295U, .revision = 4294967295U }
            });

            // components above INT64_MAX keep their bit pattern and read back through the unsigned view
            if(UniqueEntryPtr root = ParseBuffer("v = 1|2|18446744073709551615\n"))
            {
                Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::Int) && e)
                {
                    auto s = e->GetValue<uint64_t>();
                    CHECK(s.size() == 3 && s[0] == 1 && s[1] == 2 && s[2] == 18446744073709551615ull);
                }
            }

            // negative and above-INT64_MAX components may mix, bits decide the interpretation
            if(UniqueEntryPtr root = ParseBuffer("v = -1|18446744073709551615\n"))
            {
                Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::Int) && e)
                {
                    auto s = e->GetValue<int64_t>();
                    CHECK(s.size() == 2 && s[0] == -1 && s[1] == -1);
                }
            }

            // string packs decode escapes and preserve quoted pipes
            checkStrings("\"alice\"|\"bob\"|\"carol\"",  { "alice", "bob", "carol" });
            checkStrings("\"with|pipe\"|\"b\"",          { "with|pipe", "b" });
            checkStrings("\"a\\\"q\"|'b\\tc'",           { "a\"q", "b\tc" });
            checkStrings("\"\"|\"x\"",                   { "", "x" });

            if(UniqueEntryPtr root = ParseBuffer("v = 2024-12-24|15:30:00\n"))
            {
                const Entry* e = root? root->GetChild("v") : nullptr;
                if(CHECK(e && e->GetType() == Type::Timestamp) && e)
                {
                    const std::span<const Timestamp> parts = e->GetValue<Timestamp>();
                    CHECK(parts.size() == 2);
                    CHECK(parts[0].bHasDate && !parts[0].bHasTime && parts[0].day == 24);
                    CHECK(!parts[1].bHasDate && parts[1].bHasTime && parts[1].hour == 15);
                }
            }

            if(UniqueEntryPtr root = ParseBuffer("v = 0xFF|0xAA\n"))
            {
                const Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::Hex) && e)
                {
                    const std::span<const Hex> parts = e->GetValue<Hex>();
                    CHECK(parts.size() == 2
                        && parts[0].Size() == 1 && HexEquals<uint8_t>(parts[0], 0xFF)
                        && parts[1].Size() == 1 && HexEquals<uint8_t>(parts[1], 0xAA));
                }
            }
            if(UniqueEntryPtr root = ParseBuffer("v = 0x1|0x2|0x3\n"))
            {
                const Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::Hex) && e)
                {
                    const std::span<const Hex> parts = e->GetValue<Hex>();
                    CHECK(parts.size() == 3);
                    for(size_t i = 0; i < parts.size(); i++)
                    {
                        CHECK(parts[i].Size() == 1
                            && HexEquals<uint8_t>(parts[i], static_cast<uint8_t>(i + 1)));
                    }
                }
            }

            // Timestamp pack components are contiguous and mutable
            if(UniqueEntryPtr root = ParseBuffer("v = 2024-12-24T15:30:00Z|2024-12-25t16:45:30+05:30\n"))
            {
                Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::Timestamp) && e)
                {
                    const std::span<Timestamp> parts = e->GetValue<Timestamp>();
                    REQUIRE(parts.size() == 2);
                    CHECK(parts[0].day == 24 && parts[1].day == 25);
                    parts[1].minute = 30;
                    const std::span<const Timestamp> reread = std::as_const(*e).GetValue<Timestamp>();
                    REQUIRE(reread.size() == 2);
                    CHECK(reread[1].minute == 30);
                }
            }

            {
                constexpr std::string_view src = "v = 2024-12-24|15:30:00\n";
                UniqueEntryPtr root = ParseBuffer(src);
                if(CHECK_MSG(static_cast<bool>(root), src))
                {
                    String out = WriteBuffer<Style{ .bCommas = false }>(*root);
                    CHECK_MSG(!out.contains('"') && !out.contains('\''), out);

                    UniqueEntryPtr reparsed = ParseBuffer(out);
                    const Entry* a = root->GetChild("v");
                    const Entry* b = reparsed? reparsed->GetChild("v") : nullptr;
                    if(CHECK_MSG(a && b && a->GetType() == b->GetType(), out))
                    {
                        const std::span<const Timestamp> pa = a->GetValue<Timestamp>();
                        const std::span<const Timestamp> pb = b->GetValue<Timestamp>();
                        CHECK_MSG(SpanEqual(pa, pb), out);
                    }
                }
            }

            {
                constexpr std::string_view src = "v = 0xFF|0xaBc\n";
                UniqueEntryPtr root = ParseBuffer(src);
                if(CHECK_MSG(static_cast<bool>(root), src))
                {
                    String out = WriteBuffer<Style{ .bCommas = false }>(*root);
                    CHECK_MSG(!out.contains('"') && !out.contains('\''), out);

                    UniqueEntryPtr reparsed = ParseBuffer(out);
                    const Entry* a = root->GetChild("v");
                    const Entry* b = reparsed? reparsed->GetChild("v") : nullptr;
                    if(CHECK_MSG(a && b && a->GetType() == b->GetType(), out))
                    {
                        const std::span<const Hex> pa = a->GetValue<Hex>();
                        const std::span<const Hex> pb = b->GetValue<Hex>();
                        if(CHECK_MSG(pa.size() == pb.size(), out))
                        {
                            for(size_t i = 0; i < pa.size(); i++)
                                CHECK_MSG(pa[i] == pb[i], out);
                        }
                    }
                }
            }

            for(std::string_view src : { "v = 1.2.3|4.5.6.0\n", "v = 2147483647.4294967295.4294967295.4294967295\n" })
            {
                UniqueEntryPtr root = ParseBuffer(src);
                String out = WriteBuffer<Style{ .bCommas = false }>(*root);
                UniqueEntryPtr reparsed = ParseBuffer(out);
                const Entry* a = root->GetChild("v");
                const Entry* b = reparsed? reparsed->GetChild("v") : nullptr;
                CHECK_MSG(a && b && SpanEqual(a->GetValue<Version>(), b->GetValue<Version>()), out);
            }

            // scalar accessors on a string pack fall back to the first component
            if(UniqueEntryPtr root = ParseBuffer("v = \"first\"|\"second\"\n"))
            {
                Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::String) && e)
                {
                    CHECK(FirstString(*e) == "first");

                    const std::span<const String> parts = e->GetValue<String>();
                    String joined;
                    for(const String& part : parts)
                        joined.append(std::string_view(part));
                    CHECK(joined == "firstsecond");
                }
            }

            // SetValue builds a pack, replaces one, and a scalar SetValue tears one down
            if(UniqueEntryPtr root = NewEntry())
            {
                Entry* e = root->Emplace("v");
                if(CHECK(static_cast<bool>(e)))
                {
                    const std::string_view parts[] = { "x", "yy", "zzz" };
                    e->SetValue(std::span<const std::string_view>(parts));
                    std::span<String> comps = e->GetValue<String>();
                    CHECK(e->GetType() == Type::String && comps.size() == 3 && comps[0] == "x" && comps[1] == "yy" && comps[2] == "zzz");

                    const std::string_view shorter[] = { "a", "b" };
                    e->SetValue(std::span<const std::string_view>(shorter));
                    comps = e->GetValue<String>();
                    CHECK(comps.size() == 2 && comps[0] == "a" && comps[1] == "b");

                    e->SetValue(std::string_view("plain"));
                    comps = e->GetValue<String>();
                    CHECK(comps.size() == 1 && comps[0] == "plain" && FirstString(*e) == "plain");
                }
            }

            // component growth may reallocate its string but not the surrounding span
            if(UniqueEntryPtr root = NewEntry())
            {
                Entry* e = root->Emplace("v");
                if(CHECK(static_cast<bool>(e)))
                {
                    const std::string_view parts[] = { "aa", "bb", "cc" };
                    e->SetValue(std::span<const std::string_view>(parts));
                    std::span<String> comps = e->GetValue<String>();
                    const String* before = comps.data();

                    comps[1] = "much longer than before";                 // grows that component
                    comps[2] = "";                                        // shrink to empty, no realloc needed
                    CHECK(comps[0] == "aa" && comps[1] == "much longer than before" && comps[2] == "");
                    CHECK(comps[2].empty());
                    CHECK(e->GetValue<String>().data() == before);        // span never moves
                    CHECK(FirstString(*e) == "aa");                       // component 0 unchanged
                }
            }

            // an empty span becomes one empty string component
            if(UniqueEntryPtr root = NewEntry())
            {
                Entry* e = root->Emplace("v");
                if(CHECK(static_cast<bool>(e)))
                {
                    e->SetValue(std::span<const std::string_view>());
                    std::span<String> comps = e->GetValue<String>();
                    CHECK(e->GetType() == Type::String && comps.size() == 1 && comps[0].empty());
                }
            }

            // string pack write -> reparse round trip, incl escapes and embedded pipes
            if(UniqueEntryPtr root = NewEntry())
            {
                Entry* e = root->Emplace("v");
                if(CHECK(static_cast<bool>(e)))
                {
                    const std::string_view parts[] = { "plain", "with|pipe", "with \"quote\"", "tab\there", "" };
                    e->SetValue(std::span<const std::string_view>(parts));

                    String out = WriteBuffer<Style{ .bCommas = false }>(*root);

                    UniqueEntryPtr reparsed = ParseBuffer(out);
                    Entry* r = reparsed? reparsed->GetChild("v") : nullptr;
                    if(CHECK_MSG(r && r->GetType() == Type::String, out) && r)
                    {
                        const std::span<const String> comps = r->GetValue<String>();
                        if(CHECK_MSG(comps.size() == 5, out))
                        {
                            for(size_t i = 0; i < 5; i++)
                                CHECK_MSG(comps[i] == parts[i], out);
                        }
                    }
                }
            }

            // malformed packs skip the bad entry without losing its sibling
            // a parse failure must not leave scalar storage tagged as a Map
            constexpr std::string_view recover[] =
            {
                "bad = 1|99999999999999999999999\nok = 7\n",   // component overflows u64
                "bad = -1|99999999999999999999999\nok = 7\n",  // negative then unsigned-overflow
                "bad = -9223372036854775809\nok = 7\n",         // one below int64 minimum
                "bad = 1..0\nok = 7\n",                         // empty version component
                "bad = -|1\nok = 7\n",                          // empty leading component
                "bad = 1|\nok = 7\n",                           // empty trailing component
                "bad = 1||2\nok = 7\n",                         // empty middle component
                "bad = 1.0|-\nok = 7\n",                        // unparsable float component
                "bad = 1x2\nok = 7\n",                          // old 'x' separator is just an invalid value
            };
            for(std::string_view src : recover)
            {
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(src);
                if(CHECK_MSG(static_cast<bool>(root), src))
                {
                    CHECK_MSG(root->GetChild("bad") == nullptr, src);   // malformed entry skipped
                    Entry* ok = root->GetChild("ok");
                    if(CHECK_MSG(ok && ok->GetType() == Type::Int, src) && ok)
                        CHECK_MSG(FirstIntEquals(ok, 7), src);
                }
            }

            // pack-specific diagnostics
            struct PackCase { DiagnosticType type; std::string_view src; };
            constexpr PackCase packCases[] =
            {
                { DiagnosticType::InvalidPack,      "bad = 1|true\nok = 7\n" },               // non-widenable mix
                { DiagnosticType::InvalidPack,      "bad = 1|\nok = 7\n" },                   // dangling '|'
                { DiagnosticType::InvalidPack,      "bad = 1|\"a\"\nok = 7\n" },              // number/string mix
                { DiagnosticType::InvalidPack,      "bad = null|null\nok = 7\n" },            // null has no pack form
                { DiagnosticType::InvalidPack,      "bad = 1.0.0|2\nok = 7\n" },              // version/number mix
                { DiagnosticType::InvalidNumber,    "bad = 2147483648.0.0\nok = 7\n" },       // major exceeds 31 bits
                { DiagnosticType::InvalidNumber,    "bad = 1.4294967296.0\nok = 7\n" },       // component exceeds uint32
                { DiagnosticType::InvalidPack,      "bad = 0xFF|1\nok = 7\n" },               // hex/number mix
                { DiagnosticType::InvalidPack,      "bad = 0xFF|2024-12-24\nok = 7\n" },      // hex/timestamp mix
                { DiagnosticType::InvalidPack,      "bad = 2024-12-24|\"a\"\nok = 7\n" },     // timestamp/string mix
                { DiagnosticType::InvalidTimestamp, "bad = 2024-12-24|25:99:00\nok = 7\n" },  // bad component in a timestamp pack
                { DiagnosticType::InvalidDuration,  "bad = 1h|0.5ns\nok = 7\n" },              // bad component in a duration pack
                { DiagnosticType::InvalidPack,      "bad = 1h|2\nok = 7\n" },                  // duration/number mix
                { DiagnosticType::InvalidNumber,    "bad = 1x2\nok = 7\n" },                  // old 'x' syntax
            };
            for(const PackCase& c : packCases)
            {
                test::g_lastDiagnostic = {};
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(c.src);
                CHECK_MSG(test::g_lastDiagnostic.type == c.type, c.src);
                if(CHECK_MSG(static_cast<bool>(root), c.src))
                    CHECK_MSG(root->GetChild("ok") != nullptr, c.src);
            }

            // a bare '.' can't start an Atom, so '1.0|.' dies in the lexer and the whole parse is fatal
            {
                test::g_lastDiagnostic = {};
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("bad = 1.0|.\nok = 7\n");
                CHECK(root == nullptr);
                CHECK(test::g_lastDiagnostic.type == DiagnosticType::InvalidToken);
                CHECK(test::g_lastDiagnostic.severity == DiagnosticSeverity::Fatal);
            }
        }




        // strings with embedded quotes survive writing and reparsing
        static void StringRoundTripTest()
        {
            // three backslashes means one literal backslash plus an escaped quote
            if(UniqueEntryPtr root = ParseBuffer(R"(value = "a\\\"b")" "\n"))
            {
                Entry* value = root->GetChild("value");
                CHECK(value && FirstString(*value) == "a\\\"b");
            }

            constexpr std::string_view values[] =
            {
                "no quotes here",
                "has \"double\" quotes",
                "has 'single' quotes",
                "has \"both\" and 'kinds'",
                "back\\slash",                 // backslash must be re-escaped
                "tab\there and new\nline",     // control chars must be re-escaped
                "everything \" ' \\ \t \n \r mixed",
                "ends with backslash\\",
                "",
            };
            for(std::string_view want : values)
            {
                UniqueEntryPtr root = NewEntry();
                if(!CHECK(static_cast<bool>(root)))
                    continue;
                if(Entry* e = root->Emplace("s"); CHECK(e))
                    e->SetValue(want);

                String out = WriteBuffer<Style{ .bCommas = false }>(*root);

                UniqueEntryPtr reparsed = ParseBuffer(out);
                if(Entry* e = reparsed? reparsed->GetChild("s") : nullptr; CHECK_MSG(e && e->GetType() == Type::String, out))
                    CHECK_MSG(FirstString(*e) == want, out);
            }
        }




        // scalar string storage is a mutable String[1] span
        static void StringStorageTest()
        {
            CHECK(sizeof(fdf::String) == 8);

            if(UniqueEntryPtr root = ParseBuffer("v = \"hello\"\n"))
            {
                Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::String) && e)
                {
                    const std::span<String> comps = e->GetValue<String>();
                    CHECK(comps.size() == 1 && comps[0] == "hello" && comps[0].size() == 5);

                    const Entry& ce = *e;
                    const std::span<const String> constComps = ce.GetValue<String>();
                    CHECK(constComps.size() == 1 && constComps[0] == "hello");
                }
            }

            if(UniqueEntryPtr root = NewEntry())
            {
                Entry* e = root->Emplace("v");
                if(CHECK(static_cast<bool>(e)))
                {
                    e->SetValue(std::string_view("aa"));
                    std::span<String> comps = e->GetValue<String>();
                    const String* before = comps.data();

                    comps[0] = "much longer than it was before";
                    CHECK(comps[0] == "much longer than it was before");
                    CHECK(e->GetValue<String>().data() == before);
                    CHECK(FirstString(*e) == "much longer than it was before");

                    comps[0] = "";
                    CHECK(comps[0].empty() && FirstString(*e) == "");
                }
            }

            if(UniqueEntryPtr root = NewEntry())
            {
                Entry* e = root->Emplace("v");
                if(CHECK(static_cast<bool>(e)))
                {
                    e->SetValue(std::string_view(""));
                    const std::span<String> comps = e->GetValue<String>();
                    if(CHECK(e->GetType() == Type::String && comps.size() == 1))
                    {
                        CHECK(comps[0].empty() && comps[0].size() == 0);
                        CHECK(comps[0].data() == nullptr);   // empty = no allocation
                    }
                    CHECK(FirstString(*e) == "");
                }
            }

            if(UniqueEntryPtr root = NewEntry())
            {
                Entry* e = root->Emplace("v");
                if(CHECK(static_cast<bool>(e)))
                {
                    e->SetValue(std::string_view("first"));
                    e->SetValue(std::string_view("second value"));
                    CHECK(FirstString(*e) == "second value");

                    String out = WriteBuffer<Style{ .bCommas = false }>(*root);
                    UniqueEntryPtr reparsed = ParseBuffer(out);
                    Entry* r = reparsed? reparsed->GetChild("v") : nullptr;
                    if(CHECK_MSG(r && r->GetType() == Type::String, out) && r)
                        CHECK_MSG(FirstString(*r) == "second value", out);
                }
            }

            if(UniqueEntryPtr root = ParseBuffer("h = 0xFF5733\nt = 2024-12-24T15:30:00\n"))
            {
                Entry* h = root->GetChild("h");
                Entry* t = root->GetChild("t");
                CHECK(h && h->GetType() == Type::Hex);
                CHECK(t && t->GetType() == Type::Timestamp);
                if(h && t)
                {
                    const std::span<const Timestamp> timestamps = std::as_const(*t).GetValue<Timestamp>();
                    CHECK(timestamps.size() == 1 && timestamps[0].year == 2024
                        && timestamps[0].month == 12 && timestamps[0].day == 24
                        && timestamps[0].hour == 15 && timestamps[0].minute == 30);
                    CHECK(std::as_const(*t).GetValue<String>().empty());
                    CHECK(t->GetValue<String>().empty());
                    CHECK(std::as_const(*h).GetValue<String>().empty());
                    CHECK(h->GetValue<String>().empty());
                    CHECK(h->GetValue<Hex>().size() == 1 && HexEquals<uint32_t>(h->GetValue<Hex>()[0], 0x00FF5733u));
                }
            }
        }




        static void HexStorageTest()
        {
            static_assert(sizeof(Hex) == 16 && alignof(Hex) == 8);

            if(UniqueEntryPtr root = ParseBuffer("h = 0xFF5733\n"))
            {
                Entry* e = root->GetChild("h");
                if(CHECK(e && e->GetType() == Type::Hex) && e)
                {
                    const std::span<Hex> s = e->GetValue<Hex>();
                    REQUIRE(s.size() == 1);
                    CHECK(s[0].Size() == 3 && !s[0].IsEmpty() && s[0].DigitCount() == 6);

                    const std::span<const std::byte> bytes = std::as_const(s[0]).Bytes();
                    CHECK(bytes.size() == 3 && bytes[0] == std::byte{0xFF}
                        && bytes[1] == std::byte{0x57} && bytes[2] == std::byte{0x33});

                    CHECK(HexEquals<uint32_t>(s[0], 0x00FF5733u));
                    CHECK(HexEquals<uint64_t>(s[0], 0x00FF5733ull));
                    CHECK(HexEquals<uint8_t>(s[0], 0xFF));
                    CHECK(HexEquals<uint8_t>(s[0], 0x57, 1));
                    CHECK(HexEquals<uint16_t>(s[0], 0x5733, 1));

                    CHECK(s[0].Write<uint8_t>(0x99, 1));
                    CHECK(HexEquals<uint32_t>(s[0], 0x00FF9933u));
                    s[0].Bytes()[0] = std::byte{0x01};
                    CHECK(HexEquals<uint32_t>(s[0], 0x00019933u));
                }
            }

            // odd literal re-emits padded
            if(UniqueEntryPtr root = ParseBuffer("h = 0xABC\n"))
            {
                Entry* e = root->GetChild("h");
                if(CHECK(e && e->GetType() == Type::Hex) && e)
                {
                    const std::span<Hex> s = e->GetValue<Hex>();
                    REQUIRE(s.size() == 1);
                    CHECK(s[0].Size() == 2 && s[0].DigitCount() == 4);
                    CHECK(HexEquals<uint16_t>(s[0], 0x0ABC));

                    String out = WriteBuffer<Style{ .bCommas = false }>(*root);
                    CHECK_MSG(out.contains("h=0x0ABC"), out);
                    UniqueEntryPtr reparsed = ParseBuffer(out);
                    CHECK(reparsed && TreeEqual(*root, *reparsed));
                }
            }

            if(UniqueEntryPtr root = ParseBuffer("h = 0XfF5733\n"))
            {
                String upper = WriteBuffer<Style{ .bCommas = false }>(*root);
                CHECK_MSG(upper.contains("h=0xFF5733"), upper);
                String lower = WriteBuffer<Style{ .bCommas = false, .bUppercaseHex = false }>(*root);
                CHECK_MSG(lower.contains("h=0xff5733"), lower);
            }

            if(UniqueEntryPtr root = ParseBuffer("h = 0x01020304\n"))
            {
                Entry* e = root->GetChild("h");
                if(CHECK(e && e->GetType() == Type::Hex) && e)
                {
                    const std::span<Hex> s = e->GetValue<Hex>();
                    REQUIRE(s.size() == 1);
                    CHECK(HexEquals<uint32_t>(s[0], 0x01020304u) && HexEquals<uint16_t>(s[0], 0x0304, 2));
                    CHECK(s[0].Write<uint32_t>(0xAABBCCDDu, 0));
                    const std::span<const std::byte> bytes = std::as_const(s[0]).Bytes();
                    CHECK(bytes[0] == std::byte{0xAA} && bytes[3] == std::byte{0xDD});

                    CHECK(s[0].Write<float>(1.0f, 0));
                    CHECK(HexEquals<uint32_t>(s[0], 0x3F800000u));
                    CHECK(HexEquals<float>(s[0], 1.0f));
                }
            }

            // bool is one byte, nonzero reads true
            if(UniqueEntryPtr root = ParseBuffer("h = 0x0100FF\n"))
            {
                Entry* e = root->GetChild("h");
                if(CHECK(e && e->GetType() == Type::Hex) && e)
                {
                    const std::span<Hex> s = e->GetValue<Hex>();
                    REQUIRE(s.size() == 1);
                    bool a = false, b = true, c = false;
                    CHECK(s[0].Read(a) && a);              // 0x01
                    CHECK(s[0].Read(b, 1) && !b);          // 0x00
                    CHECK(s[0].Read(c, 2) && c);           // 0xFF nonzero
                    CHECK(s[0].Write(true, 1) && std::as_const(s[0]).Bytes()[1] == std::byte{1});
                    CHECK(s[0].Write(false, 0) && std::as_const(s[0]).Bytes()[0] == std::byte{0});

                    Hex flag;
                    bool set = false;
                    CHECK(flag.Write(true) && flag.Size() == 1 && flag.Read(set) && set);
                }
            }

            {
                UniqueEntryPtr root = ParseBuffer("h = 0xFF5733\n");
                Entry* e = root? root->GetChild("h") : nullptr;
                if(CHECK(e && e->GetType() == Type::Hex) && e)
                {
                    const std::span<Hex> s = e->GetValue<Hex>();
                    REQUIRE(s.size() == 1);
                    Rgb c{};
                    CHECK(s[0].Read(c) && c.r == 0xFF && c.g == 0x57 && c.b == 0x33);
                    CHECK(s[0].Write(Rgb{ 0x11, 0x22, 0x33 }, 0));
                    CHECK(HexEquals<uint32_t>(s[0], 0x00112233u));

                    // an offset past the end has nothing to overwrite
                    Hex fresh;
                    CHECK(!fresh.Write(Rgb{ 0xAA, 0xBB, 0xCC }, 1) && fresh.IsEmpty());
                    CHECK(!fresh.Write(uint8_t{0xAA}, 1) && fresh.IsEmpty());

                    Hex appended;
                    CHECK(appended.Write(uint8_t{0x11}) && appended.Write(RgbWriteOnly{ 0x22, 0x33, 0x44 })
                        && appended.Size() == 4 && HexEquals<uint32_t>(appended, 0x11223344u));

                    Hex explicitOffset;
                    CHECK(explicitOffset.Write(uint16_t{0xAABB}, 0)
                        && explicitOffset.Write(RgbWriteOnly{ 0xCC, 0xDD, 0xEE }, 2)
                        && explicitOffset.Size() == 5 && HexEquals<uint64_t>(explicitOffset, 0xAABBCCDDEEull));

                    Hex emptySlice;
                    CHECK(emptySlice.Write(EmptyHook{}) && emptySlice.Size() == 0);
                    CHECK(!emptySlice.Write(EmptyHook{}, 2) && emptySlice.Size() == 0);

                    Hex resource;
                    CHECK(resource.Write(ResourceValue{ String("owned") })
                        && resource.Size() == 5 && resource.Bytes()[0] == static_cast<std::byte>('o')
                        && resource.Bytes()[4] == static_cast<std::byte>('d'));

                    Hex nested;
                    CHECK(nested.Write(NestedHook{ { 5, 6, 7 }, 8 })
                        && HexEquals<uint32_t>(nested, 0x05060708u));

                    ReadNested nestedRead{};
                    CHECK(nested.Read(nestedRead) && nestedRead.color.r == 5
                        && nestedRead.color.g == 6 && nestedRead.color.b == 7 && nestedRead.tail == 8);

                    // interior overwrite keeps the suffix, an overlapping one extends
                    Hex interior;
                    CHECK(interior.Write(uint32_t{0xA1A2A3A4u}) && interior.Write(Rgb{ 1, 2, 3 }, 0)
                        && interior.Size() == 4 && HexEquals<uint32_t>(interior, 0x010203A4u));
                    CHECK(interior.Write(Rgb{ 4, 5, 6 }, 2) && interior.Size() == 5
                        && HexEquals<uint64_t>(interior, 0x0102040506ull));
                }
            }

            // sticky poison survives a hook that ignores a failed write
            {
                const std::byte raw[] = { std::byte{0x10}, std::byte{0x20}, std::byte{0x30} };
                Hex h(std::span<const std::byte>{raw});
                const Hex original = h;
                CHECK(!h.Write(PoisonParent{ 0xEE }, 1));
                CHECK(h == original && h.Size() == original.Size());
            }

            {
                Hex composed;
                CHECK(composed.Write(HelperComposed{ 1, 2, 3, 4 }) && HexEquals<uint32_t>(composed, 0x01020304u));
            }

            // a failure mid-hook restores the overwrite and append paths
            {
                const std::byte raw[] = { std::byte{0x10}, std::byte{0x20}, std::byte{0x30} };
                for(const size_t offset : { size_t{0}, size_t{1}, size_t{3} })
                {
                    Hex h(std::span<const std::byte>{raw});
                    const Hex original = h;
                    CHECK(!h.Write(HookFailure{ 0xEE }, offset));
                    CHECK(h == original && h.Size() == original.Size());
                }

                Hex h(std::span<const std::byte>{raw});
                const Hex original = h;
                CHECK(!h.Write(HookFailure{ 0xEE }, 4) && h == original);
                CHECK(!h.Write(RgbWriteOnly{ 1, 2, 3 }, 4) && h == original);
                CHECK(!h.Decode("AB", 4) && h == original);
            }

            // allocation failure restores bytes and size
            {
                const std::byte raw[] = { std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40} };

                Hex appendFailure;
                int unboundedCalls = 0;
                detail::GlobalAllocator::FailAllocationAfter(0);
                CHECK(!appendFailure.Write(CountedUnbounded{ &unboundedCalls, 0xEE }));
                detail::GlobalAllocator::ResetAllocationFailure();
                CHECK(unboundedCalls == 1 && appendFailure.IsEmpty());

                Hex growthFailure(std::span<const std::byte>{raw});
                const Hex growthFailureOriginal = growthFailure;
                unboundedCalls = 0;
                detail::GlobalAllocator::FailAllocationAfter(0);
                CHECK(!growthFailure.Write(GrowingUnbounded{ &unboundedCalls }, 1));
                detail::GlobalAllocator::ResetAllocationFailure();
                CHECK(unboundedCalls == 1 && growthFailure == growthFailureOriginal
                    && growthFailure.Size() == growthFailureOriginal.Size());
            }

            // cursor reads want the complete scalar, poison sticks
            {
                const std::byte one[] = { std::byte{0x7F} };
                Hex shortInput(std::span<const std::byte>{one});
                Rgb rgb{ 1, 2, 3 };
                CHECK(!shortInput.Read(rgb));

                ReaderPoison poisoned{};
                CHECK(!shortInput.Read(poisoned));
                CHECK(poisoned.wide == 0xCAFE && poisoned.tail == 0xA5);

                uint32_t widened = 0;
                CHECK(shortInput.Read(widened) && widened == 0x7Fu);
            }

            if(UniqueEntryPtr root = NewEntry())
            {
                Entry* e = root->Emplace("h");
                if(CHECK(static_cast<bool>(e)))
                {
                    const std::byte raw[3] = { std::byte{0xFF}, std::byte{0x57}, std::byte{0x33} };
                    e->SetValue(Hex(std::span<const std::byte>(raw, 3)));
                    CHECK(e->GetType() == Type::Hex && e->GetValue<Hex>().size() == 1
                        && HexEquals<uint32_t>(e->GetValue<Hex>()[0], 0x00FF5733u));

                    Hex source(std::span<const std::byte>(raw, 2));
                    e->SetValue(source);
                    CHECK(source.Write<uint16_t>(0x0000, 0));
                    const std::span<const Hex> stored = std::as_const(*e).GetValue<Hex>();
                    REQUIRE(stored.size() == 1);
                    CHECK(HexEquals<uint16_t>(stored[0], 0xFF57));

                    const Hex pack[2] = { Hex(std::span<const std::byte>(raw, 1)), Hex(std::span<const std::byte>(raw, 3)) };
                    e->SetValue(std::span<const Hex>(pack, 2));
                    const std::span<Hex> s = e->GetValue<Hex>();
                    REQUIRE(s.size() == 2);
                    CHECK(s[0].Size() == 1 && s[1].Size() == 3 && s[0] == pack[0] && s[1] == pack[1]);

                    s[0] = pack[1];
                    CHECK(s[0] == pack[1] && HexEquals<uint32_t>(s[0], 0x00FF5733u));

                    e->SetValue(std::span<const Hex>(s).subspan(1));
                    CHECK(e->GetValue<Hex>().size() == 1 && e->GetValue<Hex>()[0] == pack[1]);

                    // moving from a component of this very entry survives Repurpose
                    e->SetValue(std::move(e->GetValue<Hex>()[0]));
                    CHECK(e->GetValue<Hex>().size() == 1 && e->GetValue<Hex>()[0] == pack[1]);

                    String owned("moved");
                    e->SetValue(std::move(owned));
                    CHECK(e->GetType() == Type::String && owned.empty()
                        && std::as_const(*e).GetValue<String>()[0] == "moved");
                }
            }

            {
                Hex h;
                CHECK(h.Assign("0xFF5733") && h.Size() == 3 && HexEquals<uint32_t>(h, 0x00FF5733u));
                CHECK(h.Assign("aBc") && h.Size() == 2 && HexEquals<uint16_t>(h, 0x0ABC));
                CHECK(h.Assign("0X01") && h.Size() == 1 && HexEquals<uint8_t>(h, 0x01));
                CHECK(h.Assign("") && h.IsEmpty());
                CHECK(h.Assign("0x") && h.IsEmpty());

                Hex built;
                CHECK(built.Decode("0xFF57") && built.Decode("33")
                    && built.Size() == 3 && HexEquals<uint32_t>(built, 0x00FF5733u));

                CHECK(h.Assign("FF5733") && h.Decode("ABC")
                    && h.Size() == 5 && HexEquals<uint64_t>(h, 0xFF57330ABCull));
                CHECK(h.Decode("0x01") && h.Size() == 6);

                CHECK(h.Assign("FF5733") && h.Decode("99", 1)
                    && h.Size() == 3 && HexEquals<uint32_t>(h, 0x00FF9933u));
                // an overlapping overwrite extends
                CHECK(h.Decode("AABB", 2) && h.Size() == 4
                    && HexEquals<uint32_t>(h, 0xFF99AABBu));
                CHECK(h.Decode("CC", 4) && h.Size() == 5);
                CHECK(h.Decode("", 2) && h.Size() == 5);
                CHECK(!h.Decode("DD", 6) && h.Size() == 5);
                CHECK(!h.Decode("", 6) && h.Size() == 5);

                CHECK(h.Assign("0xFF_57_33") && h.Size() == 3 && HexEquals<uint32_t>(h, 0x00FF5733u));
                CHECK(h.Assign("A_BC") && h.Size() == 2 && HexEquals<uint16_t>(h, 0x0ABC));
                CHECK(h.Assign("FF_5733") && h.Decode("A_BC")
                    && h.Size() == 5 && HexEquals<uint64_t>(h, 0xFF57330ABCull));
                CHECK(h.Assign("FF5733") && h.Decode("9_9", 1)
                    && h.Size() == 3 && HexEquals<uint32_t>(h, 0x00FF9933u));

                // a rejected string leaves the value untouched on every entry point
                const Hex original = h;
                for(const std::string_view bad : { "0xZZ", "12G4", " 12", "0x1 2", "-1",
                    "_FF", "FF_", "F__F", "0x_FF", "_", "0x_" })
                {
                    CHECK_MSG(!h.Assign(bad), bad);
                    CHECK_MSG(h == original && h.Size() == original.Size(), bad);
                    CHECK_MSG(!h.Decode(bad), bad);
                    CHECK_MSG(h == original && h.Size() == original.Size(), bad);
                    CHECK_MSG(!h.Decode(bad, 1), bad);
                    CHECK_MSG(h == original && h.Size() == original.Size(), bad);
                }
            }

            if(UniqueEntryPtr root = NewEntry())
            {
                Entry* e = root->Emplace("reuseHex");
                if(CHECK(static_cast<bool>(e)))
                {
                    const std::byte raw[3] = { std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC} };
                    int64_t ints[2] = { 1, 2 };
                    e->SetValue(std::span(ints, 2));
                    const void* p = static_cast<const void*>(e->GetValue<int64_t>().data());
                    e->SetValue(Hex(std::span<const std::byte>(raw, 3)));
                    CHECK(e->GetType() == Type::Hex
                        && static_cast<const void*>(e->GetValue<Hex>().data()) == p
                        && HexEquals<uint32_t>(e->GetValue<Hex>()[0], 0x00AABBCCu));

                    const Hex pack[2] = { Hex(std::span<const std::byte>(raw, 1)), Hex(std::span<const std::byte>(raw, 2)) };
                    e->SetValue(std::span<const Hex>(pack, 2));
                    const void* pv = static_cast<const void*>(e->GetValue<Hex>().data());
                    double dbls[4] = { 1.0, 2.0, 3.0, 4.0 };
                    e->SetValue(std::span(dbls, 4));
                    CHECK(e->GetType() == Type::Float
                        && static_cast<const void*>(e->GetValue<double>().data()) == pv
                        && e->GetValue<double>()[3] == 4.0);
                }
            }

            {
                Hex h;
                CHECK(!h.Write<uint16_t>(0xABCD, 1) && h.IsEmpty());
                CHECK(h.Write<uint8_t>(0x00, 0) && h.Write<uint16_t>(0xABCD, 1));
                CHECK(h.Size() == 3 && h.Bytes()[0] == std::byte{0}
                    && h.Bytes()[1] == std::byte{0xAB} && h.Bytes()[2] == std::byte{0xCD});
                CHECK(HexEquals<uint32_t>(h, 0x00ABCDu));

                CHECK(h.Write<uint8_t>(0xEF) && h.Size() == 4 && HexEquals<uint32_t>(h, 0x00ABCDEFu));
                // rejected before anything is allocated
                const Hex original = h;
                for(const size_t bad : { size_t{5}, Hex::MaxSize() - 2, Hex::MaxSize(),
                                         std::numeric_limits<size_t>::max() })
                {
                    CHECK(!h.Write<uint8_t>(0x12, bad) && h == original && h.Size() == original.Size());
                    CHECK(!h.Write(RgbWriteOnly{ 1, 2, 3 }, bad) && h == original);
                }

                uint32_t unchanged = 123;
                CHECK(!h.Read(unchanged, std::numeric_limits<size_t>::max()) && unchanged == 123);

                Hex tooShort;
                float decodedFloat = 42.0f;
                CHECK(!tooShort.Read(decodedFloat) && decodedFloat == 42.0f);
            }

            if(UniqueEntryPtr root = ParseBuffer("empty = 0x\npack = 0xFF|0x\n"))
            {
                Entry* empty = root->GetChild("empty");
                Entry* pack = root->GetChild("pack");
                REQUIRE_PTR(empty);
                REQUIRE_PTR(pack);
                const std::span<const Hex> emptyValues = std::as_const(*empty).GetValue<Hex>();
                const std::span<const Hex> packValues = std::as_const(*pack).GetValue<Hex>();
                CHECK(empty->GetType() == Type::Hex);
                REQUIRE(emptyValues.size() == 1 && packValues.size() == 2);
                CHECK(emptyValues[0].IsEmpty() && emptyValues[0].DigitCount() == 0);
                CHECK(HexEquals<uint32_t>(emptyValues[0], 0u));
                CHECK(packValues[1].IsEmpty());
                String out = WriteBuffer<Style{ .bCommas = false }>(*root);
                CHECK_MSG(out.contains("empty=0x") && out.contains("pack=0xFF|0x"), out);
                UniqueEntryPtr reparsed = ParseBuffer(out);
                CHECK(reparsed && TreeEqual(*root, *reparsed));
            }

            // no size cap
            {
                std::string large = "big = 0x";
                large.append(131071, 'F');
                large += "\n";
                UniqueEntryPtr root = ParseBuffer(large);
                if(CHECK(static_cast<bool>(root)))
                {
                    Entry* e = root->GetChild("big");
                    if(CHECK(e && e->GetType() == Type::Hex) && e)
                    {
                        const std::span<Hex> s = e->GetValue<Hex>();
                        REQUIRE(s.size() == 1);
                        CHECK(s[0].Size() == 65536
                            && s[0].Bytes()[0] == std::byte{0x0F} && s[0].Bytes()[65535] == std::byte{0xFF});
                    }
                }
            }
        }




        // fdf::String API and storage invariants
        static void StringApiTest()
        {
            using fdf::String;

            // c_str terminator invariant: strlen tracks size across every mutation
            {
                String s;
                CHECK(s.c_str()[0] == '\0' && std::string_view(s.c_str()).empty());   // null state -> ""
                CHECK(s.begin() == s.end() && std::as_const(s).begin() == std::as_const(s).end());
                s.assign("hello");
                CHECK(std::strlen(s.c_str()) == s.size() && s == "hello");
                s.append(" world");
                CHECK(std::strlen(s.c_str()) == s.size() && s == "hello world");
                s.erase(5, 6);
                CHECK(std::strlen(s.c_str()) == s.size() && s == "hello");
                s.insert(0, ">> ");
                CHECK(std::strlen(s.c_str()) == s.size() && s == ">> hello");
                s.resize(4);
                CHECK(std::strlen(s.c_str()) == s.size() && s == ">> h");
                s.clear();
                CHECK(s.empty() && s.c_str()[0] == '\0');   // allocated, size 0, still terminated
            }

            // growth preserves existing content
            {
                String s("abc");
                s.append("defghijklmnopqrstuvwxyz0123456789");   // forces a realloc
                CHECK(s == "abcdefghijklmnopqrstuvwxyz0123456789");
                String t("12");
                t.insert(1, "----------------------------------------");   // realloc mid-insert
                CHECK(t == "1----------------------------------------2");
            }

            // reserve then stay within capacity: data() must not move
            {
                String s("x");
                s.reserve(64);
                const char* stable = s.data();
                const size_t cap = s.capacity();
                CHECK(cap >= 64);
                s.append("0123456789");
                CHECK(s.capacity() == cap && s.data() == stable && s == "x0123456789");
            }

            // edit-op semantics
            {
                String s("hello");
                s.push_back('!');
                CHECK(s == "hello!");
                s.pop_back();
                CHECK(s == "hello");
                s.replace(0, 1, "J");
                CHECK(s == "Jello");
                s.replace(1, 4, "umanji");   // value longer than removed, tail shifts right
                CHECK(s == "Jumanji");
                s.replace(3, 4, "p");        // value shorter than removed, tail shifts left
                CHECK(s == "Jump");
                s.erase(3);                  // erase to end
                CHECK(s == "Jum");
                s += "ble";
                CHECK(s == "Jumble");
                s += '!';
                CHECK(s == "Jumble!");
                s.resize(9, '.');
                CHECK(s == "Jumble!..");
                String other("swap");
                s.swap(other);
                CHECK(s == "swap" && other == "Jumble!..");
            }

            // read API
            {
                String s("hello world");
                CHECK(s.size() == 11 && s.length() == 11 && !s.empty());
                CHECK(s.front() == 'h' && s.back() == 'd' && s[4] == 'o');
                CHECK(s.find("world") == 6 && s.find('z') == String::npos);
                CHECK(s.rfind('o') == 7 && s.find_first_of("aeiou") == 1 && s.find_last_of("aeiou") == 7);
                CHECK(s.find_first_not_of("hel") == 4 && s.find_last_not_of("ld") == 8);
                CHECK(s.starts_with("hello") && s.ends_with("world") && s.contains("o w"));
                CHECK(s.compare("hello world") == 0 && s.substr(6) == "world" && s.substr(0, 5) == "hello");
                size_t vowels = 0;
                for(char c : s)   // iterator range-for
                    vowels += (c == 'o');
                CHECK(vowels == 2);
            }

            // operator+ chains and rvalue buffer reuse
            {
                String a("foo");
                String b("bar");
                CHECK(a + b == "foobar");
                CHECK(a + "-" + b == "foo-bar");   // (a+"-") is an rvalue, reused for the +b append
                CHECK("<" + a == "<foo" && a + '!' == "foo!" && '@' + a == "@foo");
                String moved = std::move(a) + std::string_view("X");
                CHECK(moved == "fooX");
                CHECK(std::string_view("pre-") + std::move(b) == "pre-bar");   // move-rhs, rhs buffer reused
                CHECK('#' + String("tag") == "#tag");                          // char + move-rhs
            }

            // comparisons against String / string_view / const char*
            {
                String s("mid");
                CHECK(s == String("mid") && s == std::string_view("mid") && s == "mid");
                CHECK((s <=> String("mid")) == std::strong_ordering::equal);
                CHECK((s <=> std::string_view("mzz")) == std::strong_ordering::less);
                CHECK((s <=> "aaa") == std::strong_ordering::greater);
                CHECK("mid" == s && String("mid") == s);   // rewritten reversed candidates
            }

            // std::string conversions and formatting
            {
                const std::string source = "convert";
                String s(source);
                CHECK(std::string_view(s) == "convert");
                CHECK(static_cast<std::string>(s) == source);
                s = std::string("assigned");
                CHECK(s == "assigned");
                CHECK(std::format("[{}]", s) == "[assigned]");
                CHECK(std::format("{:>10}", s) == "  assigned");   // uses the string_view formatter
            }

            // self-aliasing mutations: value points into this->block
            {
                String s("HelloWorld");
                s.insert(0, s.substr(5));   // no-grow, tail shifts over the source region
                CHECK_MSG(s == "WorldHelloWorld", std::format("self-insert got '{}'", std::string_view(s)));

                String a("HelloWorld");
                a.replace(0, 2, a.substr(3));   // "loWorldlloWorld"
                CHECK_MSG(a == "loWorldlloWorld", std::format("self-replace got '{}'", std::string_view(a)));

                String b("HelloWorld");
                b = b.substr(2);   // self-assign from own substring
                CHECK_MSG(b == "lloWorld", std::format("self-assign got '{}'", std::string_view(b)));

                String c("0123456789ABCDEFGHIJ");   // long enough that self-append grows past capacity
                c += c;
                CHECK_MSG(c == "0123456789ABCDEFGHIJ0123456789ABCDEFGHIJ", std::format("self-append got '{}'", std::string_view(c)));
            }

#if !FDF_NO_COMMENTS
            // e->GetComment() = e->GetComment(); a no-op, not corruption
            {
                UniqueEntryPtr root = NewEntry();
                Entry* e = root? root->Emplace("k") : nullptr;
                if(CHECK(e != nullptr))
                {
                    e->GetComment() = "keep me";
                    e->GetComment() = e->GetComment();   // self-feed
                    CHECK_MSG(e->GetComment() == "keep me", std::format("self-SetComment got '{}'", std::string_view(e->GetComment())));
                }
            }
#endif
        }




        // writing and reparsing any finite double must reproduce its bits
        // covers the Dragon4 writer and AlgorithmM parser at compile time and runtime
        static void FloatRoundTripTest()
        {
            auto roundTrip = [](double x) -> bool
            {
                UniqueEntryPtr root = NewEntry();
                if(!root)
                    return false;
                if(Entry* e = root->Emplace("v"))
                    e->SetValue(x);
                String out = WriteBuffer<Style{ .bCommas = false }>(*root);

                UniqueEntryPtr re = ParseBuffer(out);
                Entry* e = re? re->GetChild("v") : nullptr;
                if(!e || e->GetType() != Type::Float)
                    return false;
                auto span = e->GetValue<double>();
                return span.size() == 1 && std::bit_cast<uint64_t>(span[0]) == std::bit_cast<uint64_t>(x);
            };

            // consteval coverage stays small enough for MSVC
            // runtime fuzz handles subnormal and edge cases
            static_assert(std::bit_cast<uint64_t>([]
            {
                String s;
                fdf::detail::AppendDouble(s, 3.141592653589793);
                bool bOk = false;
                return fdf::detail::ParseDouble(s.data(), s.data() + s.size(), &bOk);
            }()) == std::bit_cast<uint64_t>(3.141592653589793));

            // Curated hard cases
            const double edges[] =
            {
                0.0, -0.0, 1.0, -1.0, 0.1, 0.2, 0.3, 1.5, 100.0, 1e7, 1e16, 1e17, 1e21, 1e22, 1e23,
                1e-4, 1e-5, 1e-7, 3.141592653589793, 2.2250738585072014e-308,
                std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::max(),
                9007199254740993.0, 123456789.123456789
            };
            for(double x : edges)
                CHECK_MSG(roundTrip(x), std::format("rt {:.17g}", x));

            // Scientific / multi-dimensional input forms parse to the expected value
            auto parseFloat = [](std::string_view text, double& out) -> bool
            {
                UniqueEntryPtr root = ParseBuffer(std::format("v = {}\n", text));
                Entry* e = root? root->GetChild("v") : nullptr;
                if(!e || e->GetType() != Type::Float)
                    return false;
                auto s = e->GetValue<double>();
                if(s.size() != 1)
                    return false;
                out = s[0];
                return true;
            };
            struct { const char* t; double v; } forms[] =
            {
                {"1.5e10", 1.5e10}, {"1.5E10", 1.5e10}, {"2.5e-3", 2.5e-3},
                {"1.0e+7", 1.0e7}, {"5.0e-324", 5e-324}
            };
            for(auto& form : forms)
            {
                double y = 0.0;
                CHECK_MSG(parseFloat(form.t, y) && y == form.v, form.t);
            }

            // Malformed exponent must not parse as a float
            if(UniqueEntryPtr root = ParseBuffer("v = 1.0e\n"))
                CHECK(!root->GetChild("v") || root->GetChild("v")->GetType() != Type::Float);

            // Multi-dimensional float with an exponent component
            if(UniqueEntryPtr root = ParseBuffer("d = 1.5|2.0e3|0.001\n"))
            {
                Entry* e = root->GetChild("d");
                if(CHECK(e && e->GetType() == Type::Float) && e)
                {
                    auto s = e->GetValue<double>();
                    CHECK(s.size() == 3 && s[0] == 1.5 && s[1] == 2.0e3 && s[2] == 0.001);
                }
            }

            // Multi-dimensional float with negative components
            if(UniqueEntryPtr root = ParseBuffer("d = 0.5|-0.5|1.0\n"))
            {
                Entry* e = root->GetChild("d");
                if(CHECK(e && e->GetType() == Type::Float) && e)
                {
                    auto s = e->GetValue<double>();
                    CHECK(s.size() == 3 && s[0] == 0.5 && s[1] == -0.5 && s[2] == 1.0);
                }
            }

            // Negative component alongside an exponent
            if(UniqueEntryPtr root = ParseBuffer("d = 1.5e3|-2.5\n"))
            {
                Entry* e = root->GetChild("d");
                if(CHECK(e && e->GetType() == Type::Float) && e)
                {
                    auto s = e->GetValue<double>();
                    CHECK(s.size() == 2 && s[0] == 1.5e3 && s[1] == -2.5);
                }
            }

            // A dash not immediately after '|' is not a valid float component
            if(UniqueEntryPtr root = ParseBuffer("d = 1.0-2.0\n"))
                CHECK(!root->GetChild("d") || root->GetChild("d")->GetType() != Type::Float);

            // Randomized fuzz: normals + subnormals through the full write/parse pipeline
            const int randomCount = test::g_bStress? 300000 : 30000;
            const int subnormalCount = test::g_bStress? 100000 : 10000;
            std::mt19937_64 rng(0xF0F0CABA);
            int fuzzFails = 0;
            for(int i = 0; i < randomCount; i++)
            {
                double x = std::bit_cast<double>(rng());
                if(std::isnan(x) || std::isinf(x))
                    continue;
                if(!roundTrip(x) && ++fuzzFails <= 5)
                    CHECK_MSG(false, std::format("fuzz {:.17g}", x));
            }
            for(int i = 0; i < subnormalCount; i++)
            {
                uint64_t b = rng() & ((1ull << 52) - 1);
                if((rng() & 1) != 0)
                    b |= (1ull << 63);
                if((b & ~(1ull << 63)) == 0)
                    continue;
                double x = std::bit_cast<double>(b);
                if(!roundTrip(x) && ++fuzzFails <= 5)
                    CHECK_MSG(false, std::format("fuzz-subnormal {:.17g}", x));
            }
            CHECK_MSG(fuzzFails == 0, std::format("{} float round-trip fuzz failures", fuzzFails));
        }




        static void TimestampTest()
        {
            static_assert(sizeof(Timestamp) == 16 && alignof(Timestamp) == 4);
            static_assert(std::is_trivially_copyable_v<Timestamp>);
            static_assert(std::is_same_v<decltype(std::declval<Entry&>().GetValue<Timestamp>()), std::span<Timestamp>>);
            static_assert(std::is_same_v<decltype(std::declval<const Entry&>().GetValue<Timestamp>()), std::span<const Timestamp>>);

            // Field extraction from a fully-specified value
            {
                UniqueEntryPtr root = ParseBuffer("t = 2024-12-24T15:30:00.123Z\n");
                Entry* e = root? root->GetChild("t") : nullptr;
                if(CHECK(e && e->GetType() == Type::Timestamp) && e)
                {
                    const std::span<const Timestamp> values = std::as_const(*e).GetValue<Timestamp>();
                    REQUIRE(values.size() == 1);
                    const Timestamp ts = values[0];
                    CHECK(ts.IsValid() && ts.bHasDate && ts.bHasTime);
                    CHECK(ts.year == 2024 && ts.month == 12 && ts.day == 24);
                    CHECK(ts.hour == 15 && ts.minute == 30 && ts.second == 0);
                    CHECK(ts.nanosecond == 123'000'000 && ts.fracDigits == 3);
                    CHECK(ts.tzKind == Timestamp::TzKind::Utc);
                }
            }

            // Epoch extraction: the famous Unix 1e9 instant, plus nanos and millis
            {
                UniqueEntryPtr root = ParseBuffer("t = 2001-09-09T01:46:40Z\n");
                Entry* e = root? root->GetChild("t") : nullptr;
                REQUIRE_PTR(e);
                const std::span<const Timestamp> values = std::as_const(*e).GetValue<Timestamp>();
                REQUIRE(values.size() == 1);
                const Timestamp ts = values[0];
                CHECK(ts.ToUnixSeconds() == 1'000'000'000);
                CHECK(ts.ToUnixMillis() == 1'000'000'000'000);
                CHECK(ts.ToUnixNanos()  == 1'000'000'000'000'000'000);
            }

            // A timezone offset normalizes to the same UTC instant as its Z equivalent
            {
                UniqueEntryPtr offRoot = ParseBuffer("t = 2024-01-01T12:00:00+05:00\n");
                UniqueEntryPtr utcRoot = ParseBuffer("t = 2024-01-01T07:00:00Z\n");
                Entry* offEntry = offRoot? offRoot->GetChild("t") : nullptr;
                Entry* utcEntry = utcRoot? utcRoot->GetChild("t") : nullptr;
                REQUIRE(offEntry && utcEntry);
                const std::span<const Timestamp> offValues = std::as_const(*offEntry).GetValue<Timestamp>();
                const std::span<const Timestamp> utcValues = std::as_const(*utcEntry).GetValue<Timestamp>();
                REQUIRE(offValues.size() == 1 && utcValues.size() == 1);
                const Timestamp off = offValues[0];
                const Timestamp utc = utcValues[0];
                CHECK(off.ToUnixSeconds() == utc.ToUnixSeconds());
                CHECK(off.tzOffsetMin == 300);
            }

            // RFC 3339 §4.3 preserves -00:00 as UnknownOffset
            {
                UniqueEntryPtr root = ParseBuffer("a=2024-01-01T00:00:00-00:00\nb=2024-01-01T00:00:00+00:00\nc=2024-01-01T00:00:00Z\n");
                REQUIRE(static_cast<bool>(root));
                {
                    Entry* ea = root->GetChild("a");
                    Entry* eb = root->GetChild("b");
                    Entry* ec = root->GetChild("c");
                    REQUIRE(ea && eb && ec);
                    const std::span<const Timestamp> aValues = std::as_const(*ea).GetValue<Timestamp>();
                    const std::span<const Timestamp> bValues = std::as_const(*eb).GetValue<Timestamp>();
                    const std::span<const Timestamp> cValues = std::as_const(*ec).GetValue<Timestamp>();
                    REQUIRE(aValues.size() == 1 && bValues.size() == 1 && cValues.size() == 1);
                    const Timestamp unknown = aValues[0];
                    const Timestamp zero    = bValues[0];
                    const Timestamp utc     = cValues[0];
                    CHECK(unknown.tzKind == Timestamp::TzKind::UnknownOffset);
                    CHECK(zero.tzKind == Timestamp::TzKind::Offset);
                    CHECK(utc.tzKind == Timestamp::TzKind::Utc);
                    CHECK(unknown != zero);
                    CHECK(unknown.IsValid() && unknown.tzOffsetMin == 0);
                    // same instant with different offset provenance
                    CHECK(unknown.ToUnixSeconds() == zero.ToUnixSeconds());
                    CHECK(unknown.ToUnixSeconds() == utc.ToUnixSeconds());
                    CHECK(WriteBuffer<Style{ .bCommas = false }>(*root) ==
                          "a=2024-01-01T00:00:00-00:00\nb=2024-01-01T00:00:00+00:00\nc=2024-01-01T00:00:00Z\n");
                }
                // only -00:00 maps to UnknownOffset
                const Timestamp near = Timestamp::FromText("2024-01-01T00:00:00-00:01");
                CHECK(near.tzKind == Timestamp::TzKind::Offset && near.tzOffsetMin == -1);
            }

            {
                UniqueEntryPtr root = ParseBuffer("t = 15:30:00.5\n");
                Entry* e = root? root->GetChild("t") : nullptr;
                REQUIRE_PTR(e);
                const std::span<const Timestamp> values = std::as_const(*e).GetValue<Timestamp>();
                REQUIRE(values.size() == 1);
                const Timestamp ts = values[0];
                CHECK(ts.IsValid() && ts.bHasTime && !ts.bHasDate);
                CHECK(ts.hour == 15 && ts.minute == 30 && ts.nanosecond == 500'000'000);
                CHECK(ts.tzKind == Timestamp::TzKind::None);
            }

            // Fraction digit count is preserved exactly from zero through nine
            for(uint8_t digits = 0; digits <= 9; digits++)
            {
                const std::string fraction = digits == 0? std::string() : "." + std::string(digits, '7');
                const std::string raw = "15:30:00" + fraction;
                UniqueEntryPtr root = ParseBuffer(std::format("t={}\n", raw));
                Entry* e = root? root->GetChild("t") : nullptr;
                if(CHECK_MSG(e && e->GetType() == Type::Timestamp, raw) && e)
                {
                    const std::span<const Timestamp> span = std::as_const(*e).GetValue<Timestamp>();
                    CHECK_MSG(span.size() == 1 && span[0].fracDigits == digits, raw);
                    String out = WriteBuffer<Style{ .bCommas = false }>(*root);
                    CHECK_MSG(out == std::format("t={}\n", raw), out);
                }
            }

            // A leap second round-trips verbatim and maps to the following minute
            {
                UniqueEntryPtr root = ParseBuffer("t=2024-12-31T23:59:60.5Z\n");
                Entry* e = root? root->GetChild("t") : nullptr;
                REQUIRE_PTR(e);
                const std::span<const Timestamp> values = std::as_const(*e).GetValue<Timestamp>();
                REQUIRE(values.size() == 1);
                const Timestamp leap = values[0];
                const Timestamp next = Timestamp::FromText("2025-01-01T00:00:00Z");
                CHECK(leap.second == 60);
                CHECK(leap.ToUnixSeconds() == next.ToUnixSeconds());
                CHECK(leap.ToUnixMillis() == next.ToUnixMillis() + 500);
                CHECK(WriteBuffer<Style{ .bCommas = false }>(*root) == "t=2024-12-31T23:59:60.5Z\n");
            }

            {
                UniqueEntryPtr root = NewEntry();
                if(Entry* e = root->Emplace("t"); CHECK(e))
                {
                    const Timestamp values[] =
                    {
                        Timestamp::Date(2024, 12, 24),
                        Timestamp::Time(15, 30, 0)
                    };
                    e->SetValue(std::span<const Timestamp>(values));
                    CHECK(e->GetType() == Type::Timestamp);
                    const std::span<Timestamp> stored = e->GetValue<Timestamp>();
                    REQUIRE(stored.size() == 2);
                    CHECK(stored[0].day == 24 && stored[1].hour == 15);
                    stored[0].year = 2025;
                    stored[1].second = 45;
                    CHECK(WriteBuffer<Style{ .bCommas = false }>(*root) == "t=2025-12-24|15:30:45\n");
                }
            }

            // Invalid components write as null, re-parse yields a real Null
            {
                UniqueEntryPtr root = NewEntry();
                if(Entry* e = root->Emplace("t"); CHECK(e))
                {
                    e->SetValue(Timestamp::Date(2024, 12, 24));
                    e->Resize(3);
                    CHECK(e->GetValue<Timestamp>().size() == 3);
                    CHECK(WriteBuffer<Style{ .bCommas = false }>(*root) == "t=null\n");
                    constexpr Style nilStyle{ .bCommas = false, .bUseNilInsteadOfNull = true };
                    CHECK(WriteBuffer<nilStyle>(*root) == "t=nil\n");

                    e->Resize(1);
                    CHECK(WriteBuffer<Style{ .bCommas = false }>(*root) == "t=2024-12-24\n");
                    const std::span<Timestamp> shrunk = e->GetValue<Timestamp>();
                    REQUIRE(shrunk.size() == 1);
                    shrunk[0] = Timestamp{};
                    const String written = WriteBuffer<Style{ .bCommas = false }>(*root);
                    CHECK(written == "t=null\n");

                    UniqueEntryPtr reparsed = ParseBuffer(written);
                    Entry* n = reparsed? reparsed->GetChild("t") : nullptr;
                    CHECK(n && n->GetType() == Type::Null);
                }
            }

            {
                UniqueEntryPtr root = NewEntry();
                if(Entry* e = root->Emplace("t"); CHECK(e))
                {
                    e->SetValue(Timestamp::FromUnixSeconds(1'000'000'000));
                    CHECK(e->GetValue<Timestamp>()[0].ToUnixSeconds() == 1'000'000'000);
                    CHECK(WriteBuffer<Style{ .bCommas = false }>(*root) == "t=2001-09-09T01:46:40Z\n");
                }
            }

            // epoch conversion is lossless, including before 1970
            for(int64_t s : { int64_t(0), int64_t(1'000'000'000), int64_t(1'700'000'000), int64_t(-86400), int64_t(-1) })
                CHECK_MSG(Timestamp::FromUnixSeconds(s).ToUnixSeconds() == s, std::format("epoch {}", s));

            // Timestamp case is a writer style choice
            {
                UniqueEntryPtr root = ParseBuffer("t=2024-12-24t15:30:00z\n");
                String upper = WriteBuffer<Style{ .bCommas = false }>(*root);
                String lower = WriteBuffer<Style{ .bCommas = false, .bUppercaseTimestamp = false }>(*root);
                CHECK(upper == "t=2024-12-24T15:30:00Z\n");
                CHECK(lower == "t=2024-12-24t15:30:00z\n");
            }
        }

        static void DurationTest()
        {
            static_assert(sizeof(Duration) == 8 && alignof(Duration) == 8);
            static_assert(std::is_trivially_copyable_v<Duration>);
            static_assert(std::is_same_v<decltype(std::declval<Entry&>().GetValue<Duration>()), std::span<Duration>>);
            static_assert(std::is_same_v<decltype(std::declval<const Entry&>().GetValue<Duration>()), std::span<const Duration>>);

            struct Case
            {
                std::string_view source;
                std::string_view canonical;
            };
            constexpr Case cases[] =
            {
                { "90s", "1m30s" },
                { "90m", "1h30m" },
                { "1h30m", "1h30m" },
                { "1.5h", "1h30m" },
                { "1.25ms", "1ms250us" },
                { "-30m", "-30m" },
                { "2w", "14d" },
                { "10ms", "10ms" },
                { "1s500ms", "1s500ms" },
                { "0s", "0s" },
                { "604800000000000ns", "7d" },
                { "0.0000000005s0.5ns", "1ns" },
            };

            for(const Case& c : cases)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("d={}\n", c.source));
                Entry* entry = root? root->GetChild("d") : nullptr;
                if(CHECK_MSG(entry && entry->GetType() == Type::Duration, c.source) && entry)
                {
                    const std::span<const Duration> value = std::as_const(*entry).GetValue<Duration>();
                    CHECK_MSG(value.size() == 1, c.source);
                    const String written = WriteBuffer<Style{ .bCommas = false }>(*root);
                    CHECK_MSG(written == std::format("d={}\n", c.canonical), written);
                }
            }

            {
                UniqueEntryPtr root = ParseBuffer("d=1h|2h|30m\n");
                Entry* entry = root? root->GetChild("d") : nullptr;
                if(CHECK(entry && entry->GetType() == Type::Duration) && entry)
                {
                    const std::span<Duration> values = entry->GetValue<Duration>();
                    REQUIRE(values.size() == 3);
                    CHECK(values[0] == Duration::Hours(1));
                    CHECK(values[1] == Duration::Hours(2));
                    CHECK(values[2] == Duration::Minutes(30));
                    values[1] = Duration::Minutes(90);
                    CHECK(WriteBuffer<Style{ .bCommas = false }>(*root) == "d=1h|1h30m|30m\n");
                }
            }

            {
                UniqueEntryPtr root = NewEntry();
                Entry* entry = root->Emplace("d");
                if(CHECK(entry))
                {
                    entry->SetValue(Duration::Seconds(1));
                    CHECK(entry->GetType() == Type::Duration);
                    entry->Resize(3);
                    const std::span<Duration> values = entry->GetValue<Duration>();
                    REQUIRE(values.size() == 3);
                    CHECK(values[0] == Duration::Seconds(1));
                    CHECK(values[1] == Duration{} && values[2] == Duration{});
                    values[1] = Duration::Millis(500);
                    values[2] = Duration::Micros(250);
                    entry->Resize(2);
                    CHECK(entry->GetValue<Duration>().size() == 2);
                    CHECK(WriteBuffer<Style{ .bCommas = false }>(*root) == "d=1s|500ms\n");

                    const Duration replacement[] =
                    {
                        Duration::Days(1),
                        Duration::Hours(12)
                    };
                    entry->SetValue(std::span<const Duration>(replacement));
                    CHECK(entry->GetValue<Duration>().size() == 2);
                    CHECK(WriteBuffer<Style{ .bCommas = false }>(*root) == "d=1d|12h\n");
                }
            }

            {
                const Duration combined = Duration::Hours(1) + Duration::Minutes(30);
                CHECK(combined.TotalNanos() == 5'400'000'000'000LL);
                CHECK(combined.TotalMinutes() == 90);
                CHECK(combined.TotalHours() == 1);
                CHECK((combined - Duration::Minutes(30)) == Duration::Hours(1));
                CHECK((-Duration::Seconds(3)) == Duration::Seconds(-3));
                CHECK(Duration::Millis(250) * 4 == Duration::Seconds(1));
                CHECK(Duration::Nanos(-1'500).TotalMicros() == -1);
                CHECK(Duration::Weeks(2).TotalWeeks() == 2);
                CHECK(Duration::Days(13).TotalWeeks() == 1);
            }

            {
                const Timestamp base = Timestamp::FromUnixSeconds(100);
                const Timestamp later = base + Duration::Seconds(30);
                const Timestamp earlier = later - Duration::Minutes(1);
                CHECK(later.ToUnixNanos() == 130'000'000'000LL);
                CHECK(earlier.ToUnixNanos() == 70'000'000'000LL);
                CHECK(later - base == Duration::Seconds(30));

                const Timestamp midnight = Timestamp::Date(1970, 1, 2);
                CHECK((midnight + Duration::Hours(1)).ToUnixSeconds() == 90'000);
            }

            // IsValid checks edited fields as well as bValid
            {
                CHECK(!Timestamp::Date(2024, 13, 99).IsValid());
                CHECK(!Timestamp::Date(2023, 2, 29).IsValid());
                CHECK(Timestamp::Date(2024, 2, 29).IsValid());
                CHECK(!Timestamp::Time(25, 0, 0).IsValid());
                CHECK(!Timestamp{}.IsValid());

                Timestamp edited = Timestamp::Date(2024, 12, 24);
                edited.month = 13;
                CHECK(!edited.IsValid());
            }

            {
                UniqueEntryPtr root = NewEntry();
                if(Entry* e = root->Emplace("t"); CHECK(e))
                {
                    e->SetValue(Timestamp::Date(2024, 12, 24));
                    const std::span<Timestamp> stored = e->GetValue<Timestamp>();
                    REQUIRE(stored.size() == 1);
                    stored[0].day = 99;
                    CHECK(WriteBuffer<Style{ .bCommas = false }>(*root) == "t=null\n");
                }
            }

            // epoch conversion is limited to years 0-9999
            {
                CHECK(!Timestamp::FromUnixSeconds(std::numeric_limits<int64_t>::max()).IsValid());
                CHECK(!Timestamp::FromUnixSeconds(std::numeric_limits<int64_t>::min()).IsValid());
                CHECK(!Timestamp::FromUnixSeconds(Timestamp::MaxUnixSecond() + 1).IsValid());
                CHECK(Timestamp::FromUnixSeconds(Timestamp::MaxUnixSecond()).IsValid());
                CHECK(Timestamp::FromUnixSeconds(Timestamp::MinUnixSecond()).IsValid());
                // int64 nanoseconds span only ~1677-2262
                constexpr int64_t MIN_NS = std::numeric_limits<int64_t>::min();
                const Timestamp floor = Timestamp::FromUnixNanos(MIN_NS);
                CHECK(floor.IsValid() && floor.year == 1677);
                CHECK(floor.ToUnixNanos() == MIN_NS);
                CHECK(Timestamp::FromUnixNanos(std::numeric_limits<int64_t>::max()).ToUnixNanos()
                      == std::numeric_limits<int64_t>::max());
            }

            // constexpr boundary checks catch signed overflow on every compiler
            {
                constexpr int64_t MIN_NS = std::numeric_limits<int64_t>::min();
                constexpr int64_t MAX_NS = std::numeric_limits<int64_t>::max();
                static_assert(Timestamp::FromUnixNanos(MIN_NS).ToUnixNanos() == MIN_NS);
                static_assert(Timestamp::FromUnixNanos(MAX_NS).ToUnixNanos() == MAX_NS);
                static_assert(Timestamp::FromUnixNanos(0).ToUnixNanos() == 0);
                static_assert(Timestamp::FromUnixNanos(-1).ToUnixNanos() == -1);
                static_assert(Timestamp::SaturatingNanos(-9'223'372'037LL, 999'999'999) == -9'223'372'036'000'000'001LL);
                static_assert(Timestamp::SaturatingNanos(-9'223'372'037LL, 145'224'192) == MIN_NS);
                static_assert(Timestamp::SaturatingNanos(-9'223'372'038LL, 999'999'999) == MIN_NS);
                static_assert(Timestamp::SaturatingNanos(9'223'372'036LL, 854'775'807) == MAX_NS);
                static_assert(Timestamp::SaturatingNanos(9'223'372'036LL, 854'775'808) == MAX_NS);
                static_assert((Timestamp::Date(0, 1, 1) - Timestamp::Date(9999, 1, 1)) == Duration::Nanos(MIN_NS));
                static_assert((Timestamp::Date(9999, 1, 1) - Timestamp::Date(0, 1, 1)) == Duration::Nanos(MAX_NS));
            }

            // preserve representable values in the negative boundary second
            {
                Timestamp edge = Timestamp::FromUnixSeconds(-9223372037);
                REQUIRE(edge.IsValid());
                edge.nanosecond = 999'999'999;
                edge.fracDigits = 9;
                CHECK(edge.ToUnixNanos() == -9'223'372'036'000'000'001LL);

                edge.nanosecond = 145'224'192;   // exactly int64 min
                CHECK(edge.ToUnixNanos() == std::numeric_limits<int64_t>::min());
                edge.nanosecond = 145'224'191;   // one below, saturates
                CHECK(edge.ToUnixNanos() == std::numeric_limits<int64_t>::min());
            }

            {
                CHECK(Timestamp::Date(9999, 1, 1).ToUnixNanos() == std::numeric_limits<int64_t>::max());
                CHECK(Timestamp::Date(0, 1, 1).ToUnixNanos() == std::numeric_limits<int64_t>::min());
                CHECK(Timestamp::FromUnixSeconds(0).ToUnixNanos() == 0);
            }

            // timestamp arithmetic boundaries
            {
                CHECK(!(Timestamp{} + Duration::Seconds(1)).IsValid());
                CHECK((Timestamp{} - Timestamp{}) == Duration{});
                CHECK((Timestamp::Date(5000, 1, 1) + Duration::Nanos(1)).IsValid());
                CHECK(Timestamp::Date(5000, 1, 1) + Duration::Days(1) - Timestamp::Date(5000, 1, 2) == Duration{});
                CHECK(!(Timestamp::Date(9999, 12, 31) + Duration::Days(1)).IsValid());
                CHECK(!(Timestamp::Date(0, 1, 1) - Duration::Nanos(1)).IsValid());
                CHECK(Timestamp::Date(9999, 1, 1) - Timestamp::Date(0, 1, 1) == Duration::Nanos(std::numeric_limits<int64_t>::max()));
                CHECK(Timestamp::Date(0, 1, 1) - Timestamp::Date(9999, 1, 1) == Duration::Nanos(std::numeric_limits<int64_t>::min()));
            }

            // Existing scalar classification remains unchanged
            struct ClassificationCase { std::string_view atom; Type type; };
            constexpr ClassificationCase classifications[] =
            {
                { "123", Type::Int },
                { "1.5", Type::Float },
                { "1e5", Type::Float },
                { "1E5", Type::Float },
                { "1.2.3", Type::Version },
                { "0xFF", Type::Hex },
                { "2024-12-24", Type::Timestamp },
            };
            for(const ClassificationCase& c : classifications)
            {
                Type type = Type::Null;
                CHECK_MSG(detail::ClassifyAtom(c.atom, type) && type == c.type, c.atom);
            }
        }




        // ----- Round-trip comparison helpers -----

        template<typename T>
        static bool SpanEqual(std::span<const T> a, std::span<const T> b)
        {
            if(a.size() != b.size())
                return false;
            for(size_t i = 0; i < a.size(); i++)
                if(!(a[i] == b[i]))
                    return false;
            return true;
        }

        // containers go through TreeEqual
        static bool ValueEqual(const Entry& a, const Entry& b)
        {
            if(a.GetType() != b.GetType())
                return false;
            switch(a.GetType())
            {
                case Type::Null:                      return true;
                case Type::Bool:                      return SpanEqual(a.GetValue<bool>(),     b.GetValue<bool>());
                case Type::Int:                       return SpanEqual(a.GetValue<int64_t>(),  b.GetValue<int64_t>());
                case Type::Version:                    return SpanEqual(a.GetValue<Version>(),  b.GetValue<Version>());
                case Type::Float:                     return SpanEqual(a.GetValue<double>(),   b.GetValue<double>());
                case Type::Hex:                       return SpanEqual(a.GetValue<Hex>(),      b.GetValue<Hex>());
                case Type::Timestamp:                 return SpanEqual(a.GetValue<Timestamp>(), b.GetValue<Timestamp>());
                case Type::Duration:                  return SpanEqual(a.GetValue<Duration>(), b.GetValue<Duration>());
                case Type::String:                    return SpanEqual(a.GetValue<String>(),   b.GetValue<String>());
                default:                              return false;
            }
        }

        // Structural + value equality. Maps compare by identifier (the writer may reorder children by
        // type when grouping); arrays compare positionally. Comments are intentionally ignored
        static bool TreeEqual(const Entry& a, const Entry& b)
        {
            if(a.GetType() != b.GetType() || a.GetIdentifier() != b.GetIdentifier())
                return false;
            if(!a.IsContainer())
                return ValueEqual(a, b);
            if(a.GetChildCount() != b.GetChildCount())
                return false;

            if(a.GetType() == Type::Map)
            {
                for(uint32_t i = 0; i < a.GetChildCount(); i++)
                {
                    const Entry* ca = a.GetDirectChild(i);
                    const Entry* cb = b.GetDirectChild(ca->GetIdentifier());
                    if(!cb || !TreeEqual(*ca, *cb))
                        return false;
                }
                return true;
            }

            for(uint32_t i = 0; i < a.GetChildCount(); i++)
                if(!TreeEqual(*a.GetDirectChild(i), *b.GetDirectChild(i)))
                    return false;
            return true;
        }

        static void DigitSeparatorTest()
        {
            struct ValidCase
            {
                std::string_view separated;
                std::string_view plain;
            };
            constexpr ValidCase validCases[] =
            {
                { "1_000_000", "1000000" },
                { "1_0.5_5", "10.55" },
                { "0xFF_57_33", "0xFF5733" },
                { "-1_000", "-1000" },
                { "1_000e1_0", "1000e10" },
                { "1.5e-1_0", "1.5e-10" },
                { "0xA_BC", "0xABC" },
            };
            for(const ValidCase& c : validCases)
            {
                UniqueEntryPtr separated = ParseBuffer(std::format("value={}\n", c.separated));
                UniqueEntryPtr plain = ParseBuffer(std::format("value={}\n", c.plain));
                const Entry* separatedValue = separated? separated->GetChild("value") : nullptr;
                const Entry* plainValue = plain? plain->GetChild("value") : nullptr;
                CHECK_MSG(separatedValue && plainValue && ValueEqual(*separatedValue, *plainValue), c.separated);
            }

            constexpr std::string_view invalidCases[] =
            {
                "_100", "100_", "1__0", "1_.5", "1._5", "1_e5", "1e_5", "0x_FF", "0xFF_", "-_1", "0x_",
                "1_0.0.0", "1_0h",
            };
            for(const std::string_view invalid : invalidCases)
            {
                test::g_diagnostics = 0;
                test::g_lastDiagnostic = {};
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(std::format("bad={}\nok=7\n", invalid));
                CHECK_MSG(root && !root->GetChild("bad") && FirstIntEquals(root->GetChild("ok"), 7), invalid);
                CHECK_MSG(test::g_diagnostics == 1 && test::g_lastDiagnostic.type == DiagnosticType::InvalidNumber, invalid);
            }

            if(UniqueEntryPtr root = ParseBuffer("ints=1_000|2_000\nhex=0xF_F|0x8_0\n"); CHECK(static_cast<bool>(root)))
            {
                const Entry* ints = root->GetChild("ints");
                const Entry* hex = root->GetChild("hex");
                if(CHECK(ints && ints->GetType() == Type::Int) && ints)
                {
                    const std::span<const int64_t> values = ints->GetValue<int64_t>();
                    CHECK(values.size() == 2 && values[0] == 1000 && values[1] == 2000);
                }
                if(CHECK(hex && hex->GetType() == Type::Hex) && hex)
                {
                    const std::span<const Hex> values = hex->GetValue<Hex>();
                    CHECK(values.size() == 2 && HexEquals<uint8_t>(values[0], 0xFF) && HexEquals<uint8_t>(values[1], 0x80));
                }
            }

            if(UniqueEntryPtr root = ParseBuffer("my_key=1_000\n"); CHECK(static_cast<bool>(root)))
                CHECK(FirstIntEquals(root->GetChild("my_key"), 1000));

            if(UniqueEntryPtr root = ParseBuffer("h=0xA_BC\n"); CHECK(static_cast<bool>(root)))
                CHECK(WriteBuffer<Style{ .bCommas = false }>(*root) == "h=0x0ABC\n");

            constexpr std::string_view source =
                "i=1_000_000\n"
                "n=-1_000_000\n"
                "f=1_234.5678\n"
                "e=1.0e100\n"
                "h=0xFF_57_33\n"
                "v=123.456.789\n"
                "t=2024-12-24\n"
                "d=123ns\n";
            UniqueEntryPtr root = ParseBuffer(source);
            REQUIRE(static_cast<bool>(root));

            const String ungrouped = WriteBuffer<Style{ .bCommas = false }>(*root);
            CHECK_MSG(ungrouped ==
                "i=1000000\n"
                "n=-1000000\n"
                "f=1234.5678\n"
                "e=1.0e100\n"
                "h=0xFF5733\n"
                "v=123.456.789\n"
                "t=2024-12-24\n"
                "d=123ns\n", ungrouped);

            const String intGrouped = WriteBuffer<Style{ .bCommas = false, .intDigitGrouping = 3 }>(*root);
            CHECK_MSG(intGrouped ==
                "i=1_000_000\n"
                "n=-1_000_000\n"
                "f=1_234.5678\n"
                "e=1.0e100\n"
                "h=0xFF5733\n"
                "v=123.456.789\n"
                "t=2024-12-24\n"
                "d=123ns\n", intGrouped);
            UniqueEntryPtr intReparsed = ParseBuffer(intGrouped);
            CHECK(intReparsed && TreeEqual(*root, *intReparsed));

            const String fineGrouped = WriteBuffer<Style{ .bCommas = false, .intDigitGrouping = 2 }>(*root);
            CHECK_MSG(fineGrouped.contains("f=12_34.5678\n") && fineGrouped.contains("e=1.0e100\n"), fineGrouped);
            UniqueEntryPtr fineReparsed = ParseBuffer(fineGrouped);
            CHECK(fineReparsed && TreeEqual(*root, *fineReparsed));

            const String hexGrouped = WriteBuffer<Style{ .bCommas = false, .hexDigitGrouping = 4 }>(*root);
            CHECK_MSG(hexGrouped ==
                "i=1000000\n"
                "n=-1000000\n"
                "f=1234.5678\n"
                "e=1.0e100\n"
                "h=0xFF_5733\n"
                "v=123.456.789\n"
                "t=2024-12-24\n"
                "d=123ns\n", hexGrouped);
            UniqueEntryPtr hexReparsed = ParseBuffer(hexGrouped);
            CHECK(hexReparsed && TreeEqual(*root, *hexReparsed));
        }

        // parse, write and reparse without changing the tree
        // canonical output stays byte-stable on the second write
        template<Style STYLE>
        static void RoundTrip(const Entry& original, std::string_view label, bool bCheckStable)
        {
            String out = WriteBuffer<STYLE>(original);

            UniqueEntryPtr rt = ParseBuffer(out);
            if(!CHECK_MSG(rt && TreeEqual(original, *rt), std::format("[{}] tree mismatch:\n{}", label, out)))
                return;

            if(bCheckStable)
            {
                String out2 = WriteBuffer<STYLE>(*rt);
                CHECK_MSG(out == out2, std::format("[{}] writer not idempotent", label));
            }
        }

        // round trips every value shape through several writer styles
        static void RoundTripTest()
        {
            constexpr std::string_view source =
                "b1 = true\n"
                "bmd = true|false|true\n"
                "i1 = -42\n"
                "imd = -1|2|-3\n"
                "umax = 18446744073709551615\n"
                "f1 = 3.14\n"
                "fWhole = 100.0\n"
                "fNeg = -2.5\n"
                "fmd = 1.0|2.5|3.0\n"
                "s1 = \"hello\"\n"
                "sEmpty = \"\"\n"
                "sQuote = \"say \\\"hi\\\"\"\n"
                "sApos = \"it's\"\n"
                "sBack = \"a\\\\b\"\n"
                "sCtrl = \"tab\\there\"\n"
                "smd = \"a\"|\"b|c\"|\"\"\n"
                "hexv = 0xFF00AA\n"
                "hexOdd = 0xABC\n"
                "hexMixedCase = 0xfF00aA\n"
                "hmd = 0xFF|0xAA|0x01\n"
                "ver3 = 1.2.3\n"
                "ver4 = 1.2.3.4\n"
                "tsDate = 2024-12-24\n"
                "tsFull = 2024-12-24T15:30:00.123Z\n"
                "tsLeap = 2024-12-31T23:59:60Z\n"
                "tsLower = 2024-12-24t15:30:00z\n"
                "tsTime = 15:30:00\n"
                "tmd = 2024-12-24|15:30:00\n"
                "nn = null\n"
                "arrScalar [ 1, 2, 3 ]\n"
                "arrStr [ \"a\", \"b\", \"c\" ]\n"
                "arrArr [ [ 1, 2 ], [ 3, 4 ] ]\n"
                "arrMaps [ { id = 1 }, { id = 2 } ]\n"
                "arrMixed [ { m = 1 }, 2, \"three\", [ 4, 5 ] ]\n"   // leaves + containers interleaved
                "mapNest { a = 1, b = \"x\", c { d = true, e = 2.5 } }\n"
                "deep { l1 { l2 { l3 = 1, arr [ 1, 2 ] } } }\n"
                "emptyArr [ ]\n"
                "emptyMap { }\n"
                "mapWithEmpty { inner [ ], k = 1 }\n";

            UniqueEntryPtr original = ParseBuffer(source);
            if(!CHECK_MSG(static_cast<bool>(original), "round-trip source must parse"))
                return;

            RoundTrip<Style{}>                                                          (*original, "default", true);
            RoundTrip<Style{ .bGroupSimilarTypes = true }>                              (*original, "grouped", true);
            RoundTrip<Style{ .bSpaceBeforeAndAfterEqualSign = true }>                   (*original, "spaced-eq", true);
            RoundTrip<Style{ .bParenthesesOnNewLine = false, .singleLineContainerLimit = 200 }>(*original, "single-line", true);
            RoundTrip<Style{ .singleLineContainerLimit = 1 }>                           (*original, "multi-line", true);
            RoundTrip<Style{ .bCommas = false }>                                        (*original, "no-commas", true);
            RoundTrip<Style{ .bTopLevelCommas = true }>                                 (*original, "top-level-commas", true);
            RoundTrip<Style{ .bUseSpacesOverTabs = false }>                             (*original, "tabs", true);
            RoundTrip<Style{ .bUppercaseHex = false, .bUseNilInsteadOfNull = true,
                             .bAlwaysUseDoubleQuoteForStrings = true }>                 (*original, "lowhex-nil-dq", true);

            // Empty containers parse and round-trip ('=' before a container is illegal)
            for(std::string_view src : { "e [ ]\n", "e { }\n", "a [ ]\nb { }\nx = 1\n" })
            {
                if(UniqueEntryPtr o = ParseBuffer(src); CHECK_MSG(static_cast<bool>(o), std::format("empty parse: {}", src)))
                    RoundTrip<Style{}>(*o, std::format("empty: {}", src), false);
            }

            // the canonical example across several styles
            // Stability is skipped because comment whitespace is normalized on the first write
            if(!filesToTest.empty())
            {
                if(UniqueEntryPtr design = ParseFile(std::filesystem::path(filesToTest[0].inputFile)); CHECK(static_cast<bool>(design)))
                {
                    RoundTrip<Style{}>                            (*design, "design-default", false);
                    RoundTrip<Style{ .bGroupSimilarTypes = true }>(*design, "design-grouped", false);
                    RoundTrip<Style{ .singleLineContainerLimit = 1 }>(*design, "design-multi-line", false);
                }
            }
        }




    #if !FDF_NO_COMMENTS
        // Returns the column of "//" on each line that has one
        static std::vector<size_t> InlineCommentColumns(std::string_view text)
        {
            std::vector<size_t> columns;
            size_t lineStart = 0;
            for(size_t i = 0; i <= text.size(); i++)
            {
                if(i == text.size() || text[i] == '\n')
                {
                    std::string_view line = text.substr(lineStart, i - lineStart);
                    if(size_t c = line.find("//"); c != std::string_view::npos)
                        columns.push_back(c);
                    lineStart = i + 1;
                }
            }
            return columns;
        }
    #endif

        static void AllocatorTest()
        {
            using Alloc = fdf::detail::GlobalAllocator;

            auto checkBucket = [](size_t request, size_t expected)
            {
                Alloc::AllocationResult r = Alloc::Allocate(request);
                if(!CHECK_MSG(r.Ptr() != nullptr, std::format("request {}", request)))
                    return;
                const size_t expectedGranted = FDF_DISABLE_SLAB_ALLOCATOR? request : expected;
                CHECK_MSG(r.Size() == expectedGranted, std::format("request {} -> granted {} (want {})", request, r.Size(), expectedGranted));
                std::memset(r.Ptr(), 0xAB, r.Size());   // whole granted extent must be writable
                Alloc::Deallocate(r.Ptr(), r.Size());
            };

            // bucket boundaries on both sides of each power of two
            checkBucket(1, 8);
            checkBucket(8, 8);
            checkBucket(9, 16);
            checkBucket(16, 16);
            checkBucket(17, 32);
            checkBucket(32, 32);
            checkBucket(33, 64);
            checkBucket(64, 64);
            checkBucket(65, 128);
            checkBucket(128, 128);
            checkBucket(129, 256);
            checkBucket(200, 256);
            checkBucket(256, 256);

            // > 256 falls back to the heap and reports the exact request
            checkBucket(257, 257);
            checkBucket(1000, 1000);

            for(size_t size : { size_t(8), size_t(16), size_t(32), size_t(64), size_t(128), size_t(256), size_t(4096) })
            {
                const Alloc::AllocationResult allocation = Alloc::Allocate(size);
                if(CHECK_MSG(allocation.Ptr() != nullptr, std::format("Allocate({})", size)))
                {
                    std::memset(allocation.Ptr(), 0xCD, allocation.Size());
                    CHECK(Alloc::Deallocate(allocation.Ptr(), allocation.Size()));
                }
            }

            // Distinct live requests hand back distinct, independently writable blocks
            {
                const Alloc::AllocationResult a = Alloc::Allocate(65);
                const Alloc::AllocationResult b = Alloc::Allocate(65);
                const Alloc::AllocationResult c = Alloc::Allocate(200);
                CHECK(a.Ptr() != b.Ptr() && a.Ptr() != c.Ptr() && b.Ptr() != c.Ptr());
                if(a.Ptr() && b.Ptr() && c.Ptr())
                {
                    std::memset(a.Ptr(), 1, a.Size());
                    std::memset(b.Ptr(), 2, b.Size());
                    std::memset(c.Ptr(), 3, c.Size());
                    CHECK(*static_cast<unsigned char*>(a.Ptr()) == 1);
                    CHECK(*static_cast<unsigned char*>(b.Ptr()) == 2);
                    CHECK(*static_cast<unsigned char*>(c.Ptr()) == 3);
                }
                Alloc::Deallocate(a.Ptr(), a.Size());
                Alloc::Deallocate(b.Ptr(), b.Size());
                Alloc::Deallocate(c.Ptr(), c.Size());
            }

            {
                Vector<uint64_t> values;
                for(uint64_t i = 0; i < 40; i++)
                    values.emplace_back(i * 3);
                CHECK(values.size() == 40 && values[0] == 0 && values[39] == 117);
                values.erase(5);
                CHECK(values.size() == 39 && values[5] == 18 && values.back() == 117);
                values.pop_back();
                CHECK(values.size() == 38 && values.back() == 114);
            }

            // child storage grows through every bucket and into the heap fallback
            {
                UniqueEntryPtr root = NewEntry();
                if(CHECK(static_cast<bool>(root)))
                {
                    constexpr uint32_t count = 40;
                    for(uint32_t i = 0; i < count; i++)
                    {
                        if(Entry* e = root->Emplace(std::format("c{}", i)))
                            e->SetValue(static_cast<int64_t>(i));
                    }
                    CHECK(root->GetChildCount() == count);
                    bool bAllMatch = true;
                    for(uint32_t i = 0; i < count; i++)
                    {
                        Entry* e = root->GetChild(std::format("c{}", i));
                        auto v = e? e->GetValue<int64_t>() : std::span<int64_t>{};
                        bAllMatch = bAllMatch && v.size() == 1 && v[0] == static_cast<int64_t>(i);
                    }
                    CHECK_MSG(bAllMatch, "all children readable after regrowth into the heap");
                }
            }

#if !FDF_NO_COMMENTS
            // comments shrink, reuse slab slack and then grow onto the heap
            {
                UniqueEntryPtr root = NewEntry();
                Entry* e = root? root->Emplace("a") : nullptr;
                if(CHECK(e != nullptr))
                {
                    e->SetValue(static_cast<int64_t>(1));
                    e->GetComment() = "0123456789";
                    CHECK(e->GetComment() == "0123456789");
                    e->GetComment() = "ab";
                    CHECK(e->GetComment() == "ab");
                    e->GetComment() = "0123456789ABCDEF012";
                    CHECK(e->GetComment() == "0123456789ABCDEF012");
                    const String longComment(300, 'x');
                    e->GetComment() = longComment;
                    CHECK(e->GetComment() == longComment);
                }
            }
#endif
        }

    #if !FDF_NO_COMMENTS
        static void WriterCommentTest()
        {
            UniqueEntryPtr root = ParseBuffer(
                "a = 1 // first\n"
                "bb = 22 // second\n"
                "ccc = 333 // third\n");
            if(!CHECK(static_cast<bool>(root)))
                return;

            // Aligned: short comments are inline and their '//' share a column
            {
                String out = WriteBuffer<Style{ .bAlignCloseComments = true, .bCommas = false }>(*root);
                CHECK(out.find('\x01') == String::npos);  // no pad placeholder leaks
                CHECK(out.contains("// first") && out.contains("// second") && out.contains("// third"));
                auto cols = InlineCommentColumns(out);
                if(CHECK(cols.size() == 3))
                    CHECK(cols[0] > 0 && cols[0] == cols[1] && cols[1] == cols[2]);
            }

            // Unaligned: still inline, but each '//' just one space after its value (varying columns)
            {
                String out = WriteBuffer<Style{ .bAlignCloseComments = false, .bCommas = false }>(*root);
                auto cols = InlineCommentColumns(out);
                if(CHECK(cols.size() == 3))
                    CHECK(cols[0] != cols[2]);  // a=1 vs ccc=333 differ in width
            }

            // bEntryComment = false suppresses comments entirely
            {
                String out = WriteBuffer<Style{ .bEntryComment = false }>(*root);
                CHECK(!out.contains("//"));
            }

            // GetComment exposes raw mutable text and the writer normalizes it on output
            {
                UniqueEntryPtr r = NewEntry();
                Entry* e = r? r->Emplace("k") : nullptr;
                if(CHECK(e != nullptr))
                {
                    e->SetValue(static_cast<int64_t>(1));
                    e->GetComment() = "  lead\n\t\ttail";
                    CHECK(e->GetComment() == "  lead\n\t\ttail");   // raw, not normalized on set
                    e->GetComment().append("\nmore");               // in-place edit through the mutable ref
                    CHECK(e->GetComment() == "  lead\n\t\ttail\nmore");

                    String out = WriteBuffer<Style{ .bCommas = false }>(*r);
                    CHECK_MSG(out.contains("// lead tail more"), out);

                    UniqueEntryPtr re = ParseBuffer(out);
                    Entry* k = re? re->GetChild("k") : nullptr;
                    if(CHECK(k != nullptr))
                        CHECK(k->GetComment() == "lead tail more");   // reparse yields the emitted form
                }
            }

            // file comments normalize whitespace and contained close sequences on output
            {
                UniqueEntryPtr r = NewEntry();
                if(CHECK(static_cast<bool>(r)))
                {
                    r->Emplace("k")->SetValue(static_cast<int64_t>(1));
                    r->GetComment() = "   header line\n\n   evil */ inside";
                    CHECK(r->GetComment() == "   header line\n\n   evil */ inside");   // raw

                    String out1 = WriteBuffer<Style{ .bCommas = false }>(*r);
                    CHECK_MSG(out1.starts_with("/*#\n"), out1);
                    CHECK_MSG(out1.contains("    header line\n"), out1);   // leading whitespace stripped, newline kept
                    CHECK_MSG(out1.contains("evil * / inside"), out1);     // '*/' broken so the block can't close early

                    UniqueEntryPtr re = ParseBuffer(out1);
                    if(CHECK(static_cast<bool>(re)))
                    {
                        String out2 = WriteBuffer<Style{ .bCommas = false }>(*re);
                        CHECK_MSG(out1 == out2, "file comment emit is round-trip stable");
                    }
                }
            }
        }
    #endif
    };
}




// ----- Compile-time (consteval) coverage -----

template<typename T>
constexpr T ExtractValue(std::string_view buffer)
{
    T value = 0;

    fdf::UniqueEntryPtr eRoot = fdf::ParseBuffer(buffer);
    if(auto* eValue = eRoot->GetDirectChild("value"))
    {
        auto span = eValue->GetValue<T>();
        if(!span.empty())
            value = span[0];
    }
    return value;
}

consteval bool ResizeProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    fdf::Entry* e = root->Emplace("v");
    if(!e)
        return false;
    e->SetValue(static_cast<int64_t>(7));
    e->Resize(3);
    auto s = e->GetValue<int64_t>();
    if(s.size() != 3 || s[0] != 7 || s[1] != 0 || s[2] != 0)
        return false;
    e->Resize(0);
    if(!e->GetValue<int64_t>().empty())
        return false;
    e->Resize(2);
    s = e->GetValue<int64_t>();
    return s.size() == 2 && s[0] == 0 && s[1] == 0;
}
static_assert(ResizeProbe(), "consteval Resize preserves and zero-fills");

consteval bool ResizeStringProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    fdf::Entry* e = root->Emplace("v");
    if(!e)
        return false;
    const std::string_view names[2] = { "alpha", "beta" };
    e->SetValue(std::span<const std::string_view>(names));

    e->Resize(4);
    auto grown = e->GetValue<fdf::String>();
    if(grown.size() != 4 || grown[0] != "alpha" || grown[1] != "beta" || grown[2] != "" || grown[3] != "")
        return false;

    e->Resize(1);
    auto shrunk = e->GetValue<fdf::String>();
    if(shrunk.size() != 1 || shrunk[0] != "alpha")
        return false;

    e->Resize(3);
    auto regrown = e->GetValue<fdf::String>();
    if(regrown.size() != 3 || regrown[0] != "alpha" || regrown[1] != "" || regrown[2] != "")
        return false;
    e->Resize(0);
    if(!e->GetValue<fdf::String>().empty())
        return false;
    e->Resize(2);
    regrown = e->GetValue<fdf::String>();
    return regrown.size() == 2 && regrown[0] == "" && regrown[1] == "";
}
static_assert(ResizeStringProbe(), "consteval Resize handles String-backed arrays");

// SetType activates the new type's union member even without SetValue
consteval bool ResizeAfterSetTypeProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    fdf::Entry* e = root->Emplace("v");
    if(!e)
        return false;
    e->SetType(fdf::Type::Version);
    e->Resize(2);
    auto v = e->GetValue<fdf::Version>();
    return e->GetType() == fdf::Type::Version && v.size() == 2 && v[0].major == 0 && v[1].major == 0;
}
static_assert(ResizeAfterSetTypeProbe(), "consteval SetType then Resize keeps the union member active");

consteval bool ChildStorageProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    constexpr std::string_view names[] = { "a", "b", "c", "d", "e", "f" };
    for(uint32_t i = 0; i < static_cast<uint32_t>(std::size(names)); i++)
    {
        fdf::Entry* child = root->Emplace(names[i]);
        if(!child)
            return false;
        child->SetValue(static_cast<int64_t>(i));
    }

    if(root->GetChildCount() != static_cast<uint32_t>(std::size(names)) || root->GetDirectChild(5u)->GetIdentifier() != "f")
        return false;
    if(!root->RemoveChild("b") || root->GetDirectChild(1)->GetIdentifier() != "c")
        return false;

    fdf::UniqueEntryPtr orphan = root->OrphanChild("d");
    if(!orphan || orphan->GetParent() || root->GetChild("d"))
        return false;
    if(!root->ClearChildren() || root->GetChildCount() != 0)
        return false;

    fdf::Entry* regrown = root->Emplace("again");
    return regrown && root->GetDirectChild(0u) == regrown;
}
static_assert(ChildStorageProbe(), "consteval child array grows, shifts, clears, and reuses capacity");

consteval bool VectorProbe()
{
    fdf::detail::Vector<uint64_t> values;
    for(uint64_t i = 0; i < 20; i++)
        values.emplace_back(i + 1);
    if(values.size() != 20 || values[0] != 1 || values.back() != 20)
        return false;
    values.erase(3);
    if(values.size() != 19 || values[3] != 5)
        return false;
    values.pop_back();
    return values.size() == 18 && values.back() == 19;
}
static_assert(VectorProbe(), "consteval internal storage grows and preserves live elements");

// ----- RFC 3339-profile timestamp validation -----
static_assert(fdf::detail::IsValidTimestamp("2024-12-24"),                "date");
static_assert(fdf::detail::IsValidTimestamp("2024-02-29"),                "leap day");
static_assert(fdf::detail::IsValidTimestamp("15:30:00"),                  "time only");
static_assert(fdf::detail::IsValidTimestamp("15:30:00.123456789"),        "time fraction");
static_assert(fdf::detail::IsValidTimestamp("2024-12-24T15:30:00"),       "date + time");
static_assert(fdf::detail::IsValidTimestamp("2024-12-24t15:30:00z"),      "lowercase date + time");
static_assert(fdf::detail::IsValidTimestamp("2024-12-24T15:30:00Z"),      "utc");
static_assert(fdf::detail::IsValidTimestamp("2024-12-24T15:30:00.123Z"),  "fractional + utc");
static_assert(fdf::detail::IsValidTimestamp("2024-12-24T15:30:00+05:30"), "offset");
static_assert(fdf::detail::IsValidTimestamp("2024-12-31T23:59:60Z"),      "leap second");
static_assert(!fdf::detail::IsValidTimestamp(""),                         "empty");
static_assert(!fdf::detail::IsValidTimestamp("2024-13-01"),               "month 13");
static_assert(!fdf::detail::IsValidTimestamp("2024-00-10"),               "month 00");
static_assert(!fdf::detail::IsValidTimestamp("2024-12-32"),               "day 32");
static_assert(!fdf::detail::IsValidTimestamp("2023-02-29"),               "non-leap feb 29");
static_assert(!fdf::detail::IsValidTimestamp("2024-359"),                 "ordinal rejected");
static_assert(!fdf::detail::IsValidTimestamp("2024-W52-2"),               "week date rejected");
static_assert(!fdf::detail::IsValidTimestamp("25:00:00"),                 "hour 25");
static_assert(!fdf::detail::IsValidTimestamp("15:60:00"),                 "minute 60");
static_assert(!fdf::detail::IsValidTimestamp("15:30:99"),                 "second 99");
static_assert(!fdf::detail::IsValidTimestamp("15:30:00Z"),                "time-only zone rejected");
static_assert(!fdf::detail::IsValidTimestamp("15:30:00.1234567890"),      "ten fraction digits");
static_assert(!fdf::detail::IsValidTimestamp("2024-12-24T15:30:00+25:00"),"offset hour 25");
static_assert(sizeof(fdf::Timestamp) == 16,                               "timestamp layout");
static_assert(alignof(fdf::Timestamp) == 4,                               "timestamp alignment");

// ----- Strict UTF-8 validation (all escaped so the test file's own encoding can't skew it) -----
static_assert( fdf::detail::IsValidUtf8(""),                     "empty");
static_assert( fdf::detail::IsValidUtf8("plain ascii"),         "ascii");
static_assert( fdf::detail::IsValidUtf8("caf\xC3\xA9"),         "2-byte");
static_assert( fdf::detail::IsValidUtf8("\xE2\x82\xAC"),        "3-byte euro");
static_assert( fdf::detail::IsValidUtf8("\xF0\x9F\x98\x80"),    "4-byte emoji");
static_assert(!fdf::detail::IsValidUtf8("\x80"),                "stray continuation");
static_assert(!fdf::detail::IsValidUtf8("\xC0\xAF"),            "overlong 2-byte");
static_assert(!fdf::detail::IsValidUtf8("\xE0\x80\xAF"),        "overlong 3-byte");
static_assert(!fdf::detail::IsValidUtf8("\xF0\x80\x80\xAF"),    "overlong 4-byte");
static_assert(!fdf::detail::IsValidUtf8("\xED\xA0\x80"),        "surrogate U+D800");
static_assert(!fdf::detail::IsValidUtf8("\xF4\x90\x80\x80"),    "above U+10FFFF");
static_assert(!fdf::detail::IsValidUtf8("\xF5"),                "invalid lead F5");
static_assert(!fdf::detail::IsValidUtf8("\xC2"),                "truncated 2-byte");
static_assert(!fdf::detail::IsValidUtf8("\xE2\x82"),            "truncated 3-byte");
static_assert(!fdf::detail::IsValidUtf8("\xC2\x20"),            "bad continuation");
static_assert(fdf::detail::Utf8FirstInvalidByte("ok\xFF") == 2, "reports first bad offset");

// ----- Timestamp struct: decode and epoch conversion (all consteval) -----
static_assert(fdf::Timestamp::FromText("1970-01-01T00:00:00Z").ToUnixSeconds() == 0,             "epoch zero");
static_assert(fdf::Timestamp::FromText("2001-09-09T01:46:40Z").ToUnixSeconds() == 1'000'000'000, "famous epoch");
static_assert(fdf::Timestamp::FromUnixSeconds(1'700'000'000).ToUnixSeconds() == 1'700'000'000,   "epoch round-trip");
static_assert(fdf::Timestamp::FromUnixSeconds(-86400).ToUnixSeconds() == -86400,                  "pre-1970 round-trip");
static_assert(fdf::Timestamp::FromText("15:30:00.5").nanosecond == 500'000'000,                   "fractional scaled to nanos");
static_assert(!fdf::Timestamp::FromText("2024-13-01").IsValid(),                                  "invalid month -> not valid");
static_assert(fdf::Timestamp::FromText("2024-01-01T12:00:00+05:00").ToUnixSeconds()
            == fdf::Timestamp::FromText("2024-01-01T07:00:00Z").ToUnixSeconds(),                   "offset normalizes to UTC");
static_assert(fdf::Timestamp::FromText("2024-12-31T23:59:60Z").ToUnixSeconds()
            == fdf::Timestamp::FromText("2025-01-01T00:00:00Z").ToUnixSeconds(),                   "leap second advances");

// Inject a Timestamp into an entry and read it back at compile time
consteval bool TimestampInjectProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    fdf::Entry* e = root->Emplace("t");
    if(!e)
        return false;
    e->SetValue(fdf::Timestamp::FromUnixSeconds(1'000'000'000));
    if(e->GetType() != fdf::Type::Timestamp)
        return false;
    const std::span<const fdf::Timestamp> got = std::as_const(*e).GetValue<fdf::Timestamp>();
    const fdf::String out = fdf::WriteBuffer<fdf::Style{ .bCommas = false }>(*root);
    return got.size() == 1 && got[0].IsValid() && got[0].year == 2001 && got[0].month == 9
        && got[0].day == 9 && got[0].ToUnixSeconds() == 1'000'000'000
        && out == "t=2001-09-09T01:46:40Z\n";
}
static_assert(TimestampInjectProbe(), "consteval timestamp inject + read back");

consteval bool DurationTextProbe()
{
    bool bValid = false;
    const fdf::Duration duration = fdf::Duration::FromText("-1.5h", bValid);
    if(!bValid || duration != fdf::Duration::Minutes(-90))
        return false;
    fdf::String output;
    duration.AppendTo(output);
    return output == "-1h30m";
}
static_assert(DurationTextProbe(), "consteval duration FromText + AppendTo");

consteval bool DurationStorageProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("d=1h30m|500ms\n");
    if(!root)
        return false;
    fdf::Entry* entry = root->GetDirectChild("d");
    if(!entry || entry->GetType() != fdf::Type::Duration || !entry->GetValue<fdf::String>().empty())
        return false;

    std::span<fdf::Duration> values = entry->GetValue<fdf::Duration>();
    if(values.size() != 2 || values[0] != fdf::Duration::Minutes(90)
        || values[1] != fdf::Duration::Millis(500))
        return false;
    values[1] = fdf::Duration::Seconds(2);

    const fdf::Duration injected[] =
    {
        fdf::Duration::Days(1),
        fdf::Duration::Hours(12)
    };
    fdf::Entry* second = root->Emplace("e");
    if(!second)
        return false;
    second->SetValue(std::span<const fdf::Duration>(injected));

    return fdf::WriteBuffer<fdf::Style{ .bCommas = false }>(*root)
        == "d=1h30m|2s\ne=1d|12h\n";
}
static_assert(DurationStorageProbe(), "consteval duration parse + typed storage + write");

consteval bool DurationArithmeticProbe()
{
    const fdf::Duration duration = (fdf::Duration::Hours(2) - fdf::Duration::Minutes(30)) * 2;
    if(duration != fdf::Duration::Hours(3) || duration.TotalMinutes() != 180)
        return false;

    const fdf::Timestamp base = fdf::Timestamp::FromUnixSeconds(1'000);
    const fdf::Timestamp later = base + fdf::Duration::Seconds(25);
    const fdf::Timestamp earlier = later - fdf::Duration::Minutes(1);
    return later.ToUnixSeconds() == 1'025
        && earlier.ToUnixSeconds() == 965
        && later - base == fdf::Duration::Seconds(25);
}
static_assert(DurationArithmeticProbe(), "consteval duration and timestamp arithmetic");

static_assert(ExtractValue<int64_t>("value = 25|50") == 25, "consteval multidim int parse");
static_assert(ExtractValue<bool>("value = true|false|true") == true, "consteval multidim bool parse");
static_assert(ExtractValue<double>("value = 3.5") > 3.4 && ExtractValue<double>("value = 3.5") < 3.6, "consteval float parse");

consteval bool MultiDimNegFloatProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 0.5|-0.5|1.0\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    if(!e || e->GetType() != fdf::Type::Float)
        return false;
    auto s = e->GetValue<double>();
    return s.size() == 3 && s[0] == 0.5 && s[1] == -0.5 && s[2] == 1.0;
}
static_assert(MultiDimNegFloatProbe(), "consteval multidim negative float parse");

consteval bool MultiDimWidenProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 1|2.0|3\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    if(!e || e->GetType() != fdf::Type::Float)
        return false;
    auto s = e->GetValue<double>();
    return s.size() == 3 && s[0] == 1.0 && s[1] == 2.0 && s[2] == 3.0;
}
static_assert(MultiDimWidenProbe(), "consteval multidim int->float widening");

consteval bool StringPackProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = \"a\"|'b\\tc'|\"dd\"\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    if(!e || e->GetType() != fdf::Type::String)
        return false;
    const std::span<const fdf::String> parts = e->GetValue<fdf::String>();
    return parts.size() == 3 && parts[0] == "a" && parts[1] == "b\tc" && parts[2] == "dd";
}
static_assert(StringPackProbe(), "consteval string pack parse");

consteval bool StringPackInjectProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    fdf::Entry* e = root->Emplace("v");
    if(!e)
        return false;
    const std::string_view parts[] = { "x", "yy" };
    e->SetValue(std::span<const std::string_view>(parts));
    std::span<fdf::String> comps = e->GetValue<fdf::String>();
    if(e->GetType() != fdf::Type::String || comps.size() != 2 || comps[0] != "x" || comps[1] != "yy")
        return false;
    comps[1] = "grown at compile time";   // mutate a component through the span at consteval
    return e->GetValue<fdf::String>()[1] == "grown at compile time" && e->GetValue<fdf::String>()[0] == "x";
}
static_assert(StringPackInjectProbe(), "consteval string pack inject + mutate + read back");

consteval bool HexTimestampPackProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("h = 0xFF|0xAA\nt = 2024-12-24|15:30:00\n");
    const fdf::Entry* h = root? root->GetDirectChild("h") : nullptr;
    const fdf::Entry* t = root? root->GetDirectChild("t") : nullptr;
    if(!h || h->GetType() != fdf::Type::Hex || !t || t->GetType() != fdf::Type::Timestamp)
        return false;
    const std::span<const fdf::Hex> hp = h->GetValue<fdf::Hex>();
    const std::span<const fdf::Timestamp> tp = t->GetValue<fdf::Timestamp>();
    return hp.size() == 2 && HexEquals<uint8_t>(hp[0], 0xFF) && HexEquals<uint8_t>(hp[1], 0xAA)
        && tp.size() == 2
        && tp[0].bHasDate && !tp[0].bHasTime && tp[0].year == 2024 && tp[0].month == 12 && tp[0].day == 24
        && !tp[1].bHasDate && tp[1].bHasTime && tp[1].hour == 15 && tp[1].minute == 30;
}
static_assert(HexTimestampPackProbe(), "consteval hex/timestamp pack parse");

consteval bool StringInjectProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    fdf::Entry* e = root->Emplace("v");
    if(!e)
        return false;
    e->SetValue(std::string_view("x"));
    std::span<fdf::String> comps = e->GetValue<fdf::String>();
    if(e->GetType() != fdf::Type::String || comps.size() != 1 || comps[0] != "x")
        return false;
    comps[0] = "grown at compile time";
    return e->GetValue<fdf::String>()[0] == "grown at compile time"
        && FirstString(*e) == "grown at compile time";
}
static_assert(StringInjectProbe(), "consteval string inject + mutate + read back");

consteval bool StringParseMutateProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 'a\\tb'\n");
    fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    if(!e || e->GetType() != fdf::Type::String)
        return false;
    std::span<fdf::String> comps = e->GetValue<fdf::String>();
    if(comps.size() != 1 || comps[0] != "a\tb")
        return false;
    comps[0] = "rewritten";
    return FirstString(*e) == "rewritten";
}
static_assert(StringParseMutateProbe(), "consteval string parse + mutate through the span");

// the string edit and read API preserves its terminator at constant evaluation
consteval bool StringApiProbe()
{
    fdf::String empty;
    if(empty.begin() != empty.end() || std::as_const(empty).begin() != std::as_const(empty).end())
        return false;

    fdf::String s("abc");
    s.append("def");         // "abcdef"
    s.insert(0, "[");        // "[abcdef"
    s.erase(4, 1);           // drop 'd' -> "[abcef"
    if(s != "[abcef")
        return false;
    s.replace(0, 1, "<<");   // grow the head -> "<<abcef"
    if(s != "<<abcef")
        return false;
    s.resize(4);             // "<<ab"
    if(s != "<<ab")
        return false;
    if(s.c_str()[s.size()] != '\0')   // terminator invariant at consteval
        return false;
    if(s.find("ab") != 2 || s.substr(2) != "ab" || !s.starts_with("<<"))
        return false;
    fdf::String joined = fdf::String("x") + s + std::string_view("y");
    return joined == "x<<aby";
}
static_assert(StringApiProbe(), "consteval String edit/read/operator+ surface");

// constant evaluation catches freed reads in self-aliasing edits
consteval bool StringSelfAliasProbe()
{
    fdf::String s("0123456789ABCDEFGHIJ");
    s += s;   // grows past capacity, source block freed mid-copy on the bug
    if(s != "0123456789ABCDEFGHIJ0123456789ABCDEFGHIJ")
        return false;

    fdf::String i("HelloWorld");
    i.insert(0, i.substr(5));   // no-grow tail-shift overlap
    if(i != "WorldHelloWorld")
        return false;

    fdf::String r("HelloWorld");
    r.replace(0, 2, r.substr(3));
    if(r != "loWorldlloWorld")
        return false;

    fdf::String a("HelloWorld");
    a = a.substr(2);   // self-assign from own substring
    return a == "lloWorld";
}
static_assert(StringSelfAliasProbe(), "consteval String self-aliasing edits");

consteval bool TimestampStorageProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("t = 2024-12-24t15:30:00.123z\n");
    if(!root)
        return false;
    const fdf::Entry* t = root->GetDirectChild("t");
    if(!t || t->GetType() != fdf::Type::Timestamp || !t->GetValue<fdf::String>().empty())
        return false;
    const std::span<const fdf::Timestamp> values = t->GetValue<fdf::Timestamp>();
    if(values.size() != 1 || values[0].fracDigits != 3 || values[0].nanosecond != 123'000'000)
        return false;
    return fdf::WriteBuffer<fdf::Style{ .bCommas = false }>(*root)
        == "t=2024-12-24T15:30:00.123Z\n";
}
static_assert(TimestampStorageProbe(), "consteval timestamp parse + typed storage + write");

consteval bool HexStorageProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("h = 0xfF5733\nodd = 0xABC\nempty = 0x\n");
    if(!root)
        return false;
    const fdf::Entry* h = root->GetDirectChild("h");
    if(!h || h->GetType() != fdf::Type::Hex || !h->GetValue<fdf::String>().empty())
        return false;
    const std::span<const fdf::Hex> hs = h->GetValue<fdf::Hex>();
    if(hs.size() != 1 || hs[0].Size() != 3
        || !HexEquals<uint32_t>(hs[0], 0x00FF5733u) || !HexEquals<uint8_t>(hs[0], 0x57, 1))
        return false;

    const fdf::Entry* odd = root->GetDirectChild("odd");
    if(!odd || odd->GetType() != fdf::Type::Hex)
        return false;
    const std::span<const fdf::Hex> os = odd->GetValue<fdf::Hex>();
    if(os.size() != 1 || os[0].Size() != 2
        || os[0].DigitCount() != 4 || !HexEquals<uint16_t>(os[0], 0x0ABC))
        return false;

    const fdf::Entry* empty = root->GetDirectChild("empty");
    if(!empty || empty->GetType() != fdf::Type::Hex || empty->GetValue<fdf::Hex>().size() != 1
        || !empty->GetValue<fdf::Hex>()[0].IsEmpty())
        return false;

    fdf::Entry* injected = root->Emplace("injected");
    if(!injected)
        return false;
    const std::byte bytes[3] = { std::byte{0xFF}, std::byte{0x57}, std::byte{0x33} };
    injected->SetValue(fdf::Hex(std::span<const std::byte>(bytes, 3)));
    if(injected->GetType() != fdf::Type::Hex)
        return false;
    std::span<fdf::Hex> is = injected->GetValue<fdf::Hex>();
    if(is.size() != 1 || !HexEquals<uint32_t>(is[0], 0x00FF5733u))
        return false;
    if(!is[0].Write<uint8_t>(0x99, 1))
        return false;
    if(!HexEquals<uint32_t>(is[0], 0x00FF9933u))
        return false;

    fdf::Hex grown;
    if(grown.Write<uint16_t>(0xABCD, 1))
        return false;
    if(!grown.Write<uint8_t>(0x00) || !grown.Write<uint16_t>(0xABCD, 1) || !grown.Write<uint8_t>(0xEF)
        || grown.Size() != 4 || !HexEquals<uint32_t>(grown, 0x00ABCDEFu))
        return false;

    fdf::Hex decoded;
    if(!decoded.Decode("0xFF57") || !decoded.Decode("3")
        || decoded.Size() != 3 || !HexEquals<uint32_t>(decoded, 0x00FF5703u))
        return false;
    if(!decoded.Decode("99", 1) || decoded.Size() != 3
        || !HexEquals<uint32_t>(decoded, 0x00FF9903u))
        return false;
    if(!decoded.Assign("0xABC") || decoded.Size() != 2)
        return false;
    const fdf::Hex beforeBadDigits = decoded;
    if(decoded.Decode("0xZZ") || decoded != beforeBadDigits)
        return false;

    fdf::Hex hooked;
    Rgb rgb{};
    return hooked.Write(Rgb{ 0xFF, 0x57, 0x33 }) && HexEquals<uint32_t>(hooked, 0x00FF5733u)
        && hooked.Read(rgb) && rgb.r == 0xFF && rgb.g == 0x57 && rgb.b == 0x33;
}
static_assert(HexStorageProbe(), "consteval hex parse + typed access + inject");

consteval bool HexCursorProbe()
{
    fdf::Hex chained;
    if(!chained.Write(Rgb{ 1, 2, 3 }) || !chained.Write(uint8_t{4})
        || !HexEquals<uint32_t>(chained, 0x01020304u))
        return false;

    fdf::Hex nestedHex;
    if(!nestedHex.Write(NestedHook{ { 5, 6, 7 }, 8 })
        || !HexEquals<uint32_t>(nestedHex, 0x05060708u))
        return false;

    fdf::Hex empty;
    if(!empty.Write(EmptyHook{}) || empty.Size() != 0)
        return false;
    if(empty.Write(EmptyHook{}, 2) || empty.Size() != 0)
        return false;

    const fdf::Hex original = nestedHex;
    if(nestedHex.Write(HookFailure{ 0xEE }, 1) || nestedHex != original)
        return false;

    ReadNested nested{};
    if(!nestedHex.Read(nested) || nested.color.r != 5 || nested.color.g != 6
        || nested.color.b != 7 || nested.tail != 8)
        return false;

    fdf::Hex composed;
    if(!composed.Write(HelperComposed{ 1, 2, 3, 4 }) || !HexEquals<uint32_t>(composed, 0x01020304u))
        return false;

    if(!composed.Write(Rgb{ 9, 8, 7 }, 0) || composed.Size() != 4
        || !HexEquals<uint32_t>(composed, 0x09080704u))
        return false;

    const std::byte one[] = { std::byte{0x7F} };
    const fdf::Hex shortInput(std::span<const std::byte>{one});
    ReaderPoison poisoned{};
    return !shortInput.Read(poisoned) && poisoned.wide == 0xCAFE && poisoned.tail == 0xA5;
}
static_assert(HexCursorProbe(), "consteval hex cursor transfers and recovery");

consteval bool HexWriteProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("h = 0xff5733\nodd = 0xabc\np = 0xFF|0xAA\n");
    if(!root)
        return false;
    fdf::String upper = fdf::WriteBuffer<fdf::Style{}>(*root);
    if(!upper.contains("h=0xFF5733") || !upper.contains("odd=0x0ABC") || !upper.contains("p=0xFF|0xAA"))
        return false;
    fdf::String lower = fdf::WriteBuffer<fdf::Style{ .bUppercaseHex = false }>(*root);
    return lower.contains("h=0xff5733") && lower.contains("odd=0x0abc") && lower.contains("p=0xff|0xaa");
}
static_assert(HexWriteProbe(), "consteval hex write");

// one consteval parse probe per scalar type
consteval bool IntProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = -42\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    return e && e->GetType() == fdf::Type::Int && e->GetValue<int64_t>().size() == 1 && e->GetValue<int64_t>()[0] == -42;
}
consteval bool UIntProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 18446744073709551615\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    return e && e->GetType() == fdf::Type::Int && e->GetValue<uint64_t>()[0] == 18446744073709551615ull;
}
consteval bool MinIntProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = -9223372036854775808|0|9223372036854775807\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    if(!e || e->GetType() != fdf::Type::Int)
        return false;
    const std::span<const int64_t> values = e->GetValue<int64_t>();
    return values.size() == 3 && values[0] == std::numeric_limits<int64_t>::min()
        && values[1] == 0 && values[2] == std::numeric_limits<int64_t>::max();
}
consteval bool UIntPackProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 1|2|18446744073709551615\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    if(!e || e->GetType() != fdf::Type::Int)
        return false;
    const fdf::ConstUIntSpan values = e->GetValue<uint64_t>();
    return values.size() == 3 && values[0] == 1 && values[1] == 2
        && values[2] == 18446744073709551615ull;
}
consteval bool FloatProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 3.5\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    return e && e->GetType() == fdf::Type::Float && e->GetValue<double>()[0] > 3.4 && e->GetValue<double>()[0] < 3.6;
}
consteval bool DigitSeparatorProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 1_000_000\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    return e && e->GetType() == fdf::Type::Int && e->GetValue<int64_t>()[0] == 1000000;
}
consteval bool StringProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = \"hi\"\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    return e && e->GetType() == fdf::Type::String && FirstString(*e) == "hi";
}
consteval bool BoolProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = true\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    return e && e->GetType() == fdf::Type::Bool && e->GetValue<bool>()[0] == true;
}
consteval bool NullProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = null\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    return e && e->GetType() == fdf::Type::Null;
}
consteval bool MultiDimProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 1920|1080\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    if(!e || e->GetType() != fdf::Type::Int)
        return false;
    auto v = e->GetValue<int64_t>();
    return v.size() == 2 && v[0] == 1920 && v[1] == 1080;
}
consteval bool VersionProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 1.2.3\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    if(!e || e->GetType() != fdf::Type::Version)
        return false;
    auto v = e->GetValue<fdf::Version>();
    return v.size() == 1 && !v[0].bHasRevision && v[0].major == 1 && v[0].minor == 2 && v[0].patch == 3 && v[0].revision == 0;
}
consteval bool TimestampProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 2024-12-24T15:30:00Z\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    if(!e || e->GetType() != fdf::Type::Timestamp)
        return false;
    const std::span<const fdf::Timestamp> value = e->GetValue<fdf::Timestamp>();
    return value.size() == 1 && value[0].year == 2024 && value[0].month == 12
        && value[0].day == 24 && value[0].hour == 15 && value[0].minute == 30
        && value[0].tzKind == fdf::Timestamp::TzKind::Utc;
}

static_assert(IntProbe(),       "consteval int parse");
static_assert(MinIntProbe(),    "consteval minimum int64 parse");
static_assert(UIntProbe(),      "consteval uint (max u64) parse");
static_assert(UIntPackProbe(),  "consteval unsigned view over an int pack with a huge component");
static_assert(FloatProbe(),     "consteval float parse");
static_assert(DigitSeparatorProbe(), "consteval separated int parse");
static_assert(StringProbe(),    "consteval string parse");
static_assert(BoolProbe(),      "consteval bool parse");
static_assert(NullProbe(),      "consteval null parse");
static_assert(MultiDimProbe(),  "consteval multidim int parse");
static_assert(VersionProbe(),   "consteval version parse");
static_assert(TimestampProbe(), "consteval timestamp parse");

// Maps and arrays, including nested child access, at compile time
consteval bool ParseContainerProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer(
        "m { a = 1, b = 2 }\n"
        "arr [ 10, 20, 30 ]\n");
    if(!root)
        return false;

    const fdf::Entry* m = root->GetDirectChild("m");
    if(!m || m->GetType() != fdf::Type::Map || m->GetChildCount() != 2)
        return false;
    if(const fdf::Entry* a = m->GetDirectChild("a"); !a || a->GetValue<int64_t>()[0] != 1)
        return false;

    const fdf::Entry* arr = root->GetDirectChild("arr");
    if(!arr || arr->GetType() != fdf::Type::Array || arr->GetChildCount() != 3)
        return false;
    const fdf::Entry* third = arr->GetDirectChild(2u);
    return third && third->GetValue<int64_t>()[0] == 30 && third->GetFullIdentifier() == "arr.2";
}
static_assert(ParseContainerProbe(), "consteval map + array parse");

static_assert(std::is_same_v<decltype(std::declval<const fdf::Entry&>().GetFullIdentifier()), fdf::String>,
    "full identifiers use fdf::String storage");

consteval bool FullIdentifierProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    fdf::Entry* items = root->Emplace("items");
    if(!items)
        return false;
    items->SetValue(fdf::ArrayType{});
    for(int64_t i = 0; i < 13; i++)
        items->Emplace("")->SetValue(i);

    fdf::Entry* map = items->GetDirectChild(5u);
    map->SetValue(fdf::MapType{});
    fdf::Entry* nested = map->Emplace("nested");
    nested->SetValue(fdf::ArrayType{});
    nested->Emplace("")->SetValue(1);
    nested->Emplace("")->SetValue(2);

    if(!root->GetFullIdentifier().empty() || items->GetFullIdentifier() != "items"
        || items->GetDirectChild(0u)->GetFullIdentifier() != "items.0"
        || items->GetDirectChild(12u)->GetFullIdentifier() != "items.12"
        || nested->GetDirectChild(1u)->GetFullIdentifier() != "items.5.nested.1")
        return false;

    fdf::UniqueEntryPtr detached = items->OrphanChild(12u);
    return detached && detached->GetParent() == nullptr && detached->GetFullIdentifier().empty();
}
static_assert(FullIdentifierProbe(), "consteval full identifiers cover direct, nested, and multi-digit array indices");

consteval bool CombineProbe()
{
    fdf::UniqueEntryPtr base = fdf::ParseBuffer("keep=1\nsection { old=2 }\n");
    fdf::UniqueEntryPtr incoming = fdf::ParseBuffer("add=3\nsection { fresh=4 }\n");
    if(!base || !incoming || !base->Combine(incoming) || incoming)
        return false;
    const fdf::Entry* section = base->GetChild("section");
    const fdf::Entry* old = base->GetChild("section.old");
    const fdf::Entry* fresh = base->GetChild("section.fresh");
    if(!base->GetChild("keep") || !base->GetChild("add") || !section || !fresh
        || fresh->GetParent() != section || !FirstIntEquals(old, 2))
        return false;

    fdf::UniqueEntryPtr left = fdf::NewEntry();
    fdf::UniqueEntryPtr right = fdf::NewEntry();
    left->SetValue(fdf::ArrayType{});
    right->SetValue(fdf::ArrayType{});
    left->Emplace("")->SetValue(1);
    right->Emplace("")->SetValue(2);
    if(!left->Combine(right) || right || left->GetChildCount() != 2)
        return false;
    return left->GetDirectChild(0u)->GetValue<int64_t>()[0] == 1
        && left->GetDirectChild(1u)->GetValue<int64_t>()[0] == 2
        && left->GetDirectChild(1u)->GetParent() == left.get();
}
static_assert(CombineProbe(), "consteval combine merges roots and repairs parent links");

// String escape decoding (\t -> tab, \\ -> backslash, \" -> quote) at compile time
consteval bool EscapeProbe()
{
    constexpr std::string_view source =
        R"(one="a\"b")" "\n"
        R"(two="a\\")" "\n"
        R"(three="a\\\"b")" "\n"
        R"(four="a\\\\")" "\n"
        R"(five="a\\\\\"b")" "\n"
        "after=1\n";
    fdf::UniqueEntryPtr root = fdf::ParseBuffer(source);
    if(!root)
        return false;
    return FirstString(*root->GetDirectChild("one")) == "a\"b"
        && FirstString(*root->GetDirectChild("two")) == "a\\"
        && FirstString(*root->GetDirectChild("three")) == "a\\\"b"
        && FirstString(*root->GetDirectChild("four")) == "a\\\\"
        && FirstString(*root->GetDirectChild("five")) == "a\\\\\"b"
        && root->GetDirectChild("after")->GetValue<int64_t>()[0] == 1;
}
static_assert(EscapeProbe(), "consteval string escape decoding");

// Compile-time negative coverage: a fatal lexer error yields a null tree
static_assert(fdf::ParseBuffer("s = \"unterminated\n") == nullptr, "consteval fatal lexer error -> null");

// Recoverable errors: the bad entry is skipped, valid entries on either side survive
consteval bool RecoveryProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer(
        "good = 1\n"
        "bad = = 5\n"      // grammar error
        "ts = 2024-13-45\n"  // out-of-range timestamp
        "also = 2\n");
    return root && root->GetChildCount() == 2
        && root->GetDirectChild("good") && root->GetDirectChild("also")
        && !root->GetDirectChild("bad") && !root->GetDirectChild("ts");
}
static_assert(RecoveryProbe(), "consteval recovery skips malformed entries");

// ----- Compile-time writing (WriteBuffer at consteval) -----
consteval bool ContainsAt(std::string_view hay, std::string_view needle)
{
    return hay.find(needle) != std::string_view::npos;
}

// Every scalar type written at compile time, with the expected text checked
consteval bool WriteScalarsProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    root->Emplace("i")->SetValue(static_cast<int64_t>(-42));
    root->Emplace("u")->SetValue(18446744073709551615ull);
    root->Emplace("f")->SetValue(2.5);                       // exact dyadic -> deterministic text
    root->Emplace("md")->SetValue(1920);                     // single int
    root->Emplace("b")->SetValue(true);
    root->Emplace("s")->SetValue("hi");
    root->Emplace("n")->SetValue(fdf::NullType{});

    fdf::String out = fdf::WriteBuffer<fdf::Style{}>(*root);

    return ContainsAt(out, "i=-42")
        && ContainsAt(out, "u=-1")   // above INT64_MAX serializes in signed form, bits round-trip
        && ContainsAt(out, "f=2.5")
        && ContainsAt(out, "b=true")
        && ContainsAt(out, "s=\"hi\"")
        && ContainsAt(out, "n=null");
}
static_assert(WriteScalarsProbe(), "consteval write scalars");

// Multi-dimensional and hex/version written at compile time
consteval bool WriteCompositeProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    int64_t dims[2] = { 1920, 1080 };
    root->Emplace("res")->SetValue(std::span(dims, 2));
    double xyz[3] = { 1.0, 2.5, 3.0 };
    root->Emplace("pos")->SetValue(std::span(xyz, 3));
    const std::string_view names[2] = { "ann", "bo" };
    root->Emplace("who")->SetValue(std::span<const std::string_view>(names));
    const fdf::Version versions[2] =
    {
        { .bHasRevision = false, .major = 1, .minor = 2, .patch = 3, .revision = 0 },
        { .bHasRevision = true, .major = 4, .minor = 5, .patch = 6, .revision = 0 }
    };
    root->Emplace("ver")->SetValue(std::span<const fdf::Version>(versions));

    fdf::String out = fdf::WriteBuffer<fdf::Style{}>(*root);
    return ContainsAt(out, "res=1920|1080") && ContainsAt(out, "pos=1.0|2.5|3.0")
        && ContainsAt(out, "who=\"ann\"|\"bo\"") && ContainsAt(out, "ver=1.2.3|4.5.6.0");
}
static_assert(WriteCompositeProbe(), "consteval write numeric/string/version packs");

#if !FDF_NO_COMMENTS
consteval bool WriteCommentAlignmentProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer(
        "a=1 // first\n"
        "longer=2 // second\n");
    if(!root)
        return false;

    fdf::String aligned = fdf::WriteBuffer<fdf::Style{ .bAlignCloseComments = true, .bCommas = false }>(*root);
    const size_t first = aligned.find("// first");
    const size_t second = aligned.find("// second");
    if(first == fdf::String::npos || second == fdf::String::npos || aligned.find('\x01') != fdf::String::npos)
        return false;
    const size_t firstLine = aligned.rfind('\n', first);
    const size_t secondLine = aligned.rfind('\n', second);
    const size_t firstColumn = firstLine == fdf::String::npos? first : first - firstLine - 1;
    const size_t secondColumn = secondLine == fdf::String::npos? second : second - secondLine - 1;
    if(firstColumn != secondColumn)
        return false;

    fdf::String unaligned = fdf::WriteBuffer<fdf::Style{ .bAlignCloseComments = false, .bCommas = false }>(*root);
    if(unaligned.find('\x01') != fdf::String::npos || !unaligned.contains("1 // first")
       || !unaligned.contains("2 // second"))
        return false;

    fdf::UniqueEntryPtr controls = fdf::NewEntry();
    fdf::Entry* control = controls->Emplace("control");
    constexpr std::string_view CONTROL_VALUE{"a\x01// b", 6};
    constexpr std::string_view CONTROL_COMMENT{"note\x01// keep", 12};
    control->SetValue(CONTROL_VALUE);
    control->GetComment() = CONTROL_COMMENT;

    fdf::String controlOut = fdf::WriteBuffer<fdf::Style{ .bAlignCloseComments = true, .bCommas = false }>(*controls);
    size_t controlByteCount = 0;
    for(char c : controlOut)
        controlByteCount += c == '\x01';
    return controlByteCount == 2 && controlOut.contains(CONTROL_VALUE) && controlOut.contains(CONTROL_COMMENT);
}
static_assert(WriteCommentAlignmentProbe(), "consteval inline-comment padding avoids staging allocations");
#endif

// consteval SetValue/GetValue coverage for the compile-time storage path
consteval bool ValueRoundTripProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();

    fdf::Entry* i = root->Emplace("i");
    i->SetValue(static_cast<int64_t>(-7));
    if(i->GetType() != fdf::Type::Int || i->GetValue<int64_t>()[0] != -7)
        return false;

    fdf::Entry* u = root->Emplace("u");
    u->SetValue(static_cast<uint64_t>(42));
    if(u->GetType() != fdf::Type::Int || u->GetValue<uint64_t>()[0] != 42u)
        return false;

    // overwrite through the buffer-reuse path (same storage, capacity suffices)
    u->SetValue(static_cast<int64_t>(-3));
    if(u->GetType() != fdf::Type::Int || u->GetValue<int64_t>()[0] != -3)
        return false;

    fdf::Entry* f = root->Emplace("f");
    f->SetValue(2.5);
    if(f->GetType() != fdf::Type::Float || f->GetValue<double>()[0] != 2.5)
        return false;

    fdf::Entry* b = root->Emplace("b");
    b->SetValue(true);
    if(b->GetType() != fdf::Type::Bool || b->GetValue<bool>()[0] != true)
        return false;

    fdf::Entry* v = root->Emplace("v");
    const fdf::Version version{ .bHasRevision = true, .major = 1, .minor = 2, .patch = 3, .revision = 0 };
    v->SetValue(version);
    const std::span<const fdf::Version> versionValue = v->GetValue<fdf::Version>();
    if(v->GetType() != fdf::Type::Version || versionValue.size() != 1 || versionValue[0] != version)
        return false;

    fdf::Entry* s = root->Emplace("s");
    s->SetValue("hi");
    if(s->GetType() != fdf::Type::String || FirstString(*s) != "hi")
        return false;

    int64_t ints[3] = { 10, -20, 30 };
    fdf::Entry* iv = root->Emplace("iv");
    iv->SetValue(std::span(ints, 3));
    auto ri = iv->GetValue<int64_t>();
    if(ri.size() != 3 || ri[0] != 10 || ri[1] != -20 || ri[2] != 30)
        return false;

    bool bools[3] = { true, false, true };
    fdf::Entry* bv = root->Emplace("bv");
    bv->SetValue(std::span(bools, 3));
    auto rb = bv->GetValue<bool>();
    if(rb.size() != 3 || rb[0] != true || rb[1] != false || rb[2] != true)
        return false;

    double dbls[2] = { 1.0, 2.5 };
    fdf::Entry* dv = root->Emplace("dv");
    dv->SetValue(std::span(dbls, 2));
    auto rd = dv->GetValue<double>();
    if(rd.size() != 2 || rd[0] != 1.0 || rd[1] != 2.5)
        return false;

    return true;
}
static_assert(ValueRoundTripProbe(), "consteval SetValue/GetValue round-trips");

// Containers (array + nested map), single-line collapse exercised at compile time
consteval bool WriteContainerProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    fdf::Entry* arr = root->Emplace("arr");
    arr->SetValue(fdf::ArrayType{});
    arr->Emplace("")->SetValue(static_cast<int64_t>(10));
    arr->Emplace("")->SetValue(static_cast<int64_t>(20));
    fdf::Entry* m = root->Emplace("m");
    m->SetValue(fdf::MapType{});
    m->Emplace("k")->SetValue(true);

    fdf::String out = fdf::WriteBuffer<fdf::Style{}>(*root);
    return ContainsAt(out, "arr") && ContainsAt(out, "10") && ContainsAt(out, "20")
        && ContainsAt(out, "m") && ContainsAt(out, "k=true");
}
static_assert(WriteContainerProbe(), "consteval write containers");

// full build, write and parse round trip at compile time
consteval bool WriteRoundTripProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    root->Emplace("x")->SetValue(static_cast<int64_t>(123));
    root->Emplace("y")->SetValue("hello");
    fdf::Entry* arr = root->Emplace("arr");
    arr->SetValue(fdf::ArrayType{});
    arr->Emplace("")->SetValue(static_cast<int64_t>(7));
    arr->Emplace("")->SetValue(static_cast<int64_t>(8));

    fdf::String out = fdf::WriteBuffer<fdf::Style{}>(*root);

    fdf::UniqueEntryPtr re = fdf::ParseBuffer(out);
    if(!re)
        return false;
    const fdf::Entry* x = re->GetDirectChild("x");
    const fdf::Entry* y = re->GetDirectChild("y");
    const fdf::Entry* a = re->GetDirectChild("arr");
    return x && x->GetValue<int64_t>()[0] == 123
        && y && FirstString(*y) == "hello"
        && a && a->GetChildCount() == 2 && a->GetDirectChild(1u)->GetValue<int64_t>()[0] == 8;
}
static_assert(WriteRoundTripProbe(), "consteval write -> parse round-trip");

#if !FDF_NO_COMMENTS
consteval size_t ExtractCommentSize(auto buffer)
{
    fdf::UniqueEntryPtr eRoot = fdf::ParseBuffer(buffer);
    if(!eRoot)
        return 0;
    if(auto* eValue = eRoot->GetDirectChild("value"))
        return eValue->GetComment().size();
    return 0;
}

template<size_t SIZE>
consteval auto ExtractCommentArray(auto buffer)
{
    std::array<char, SIZE + 1> result = {};
    fdf::UniqueEntryPtr eRoot = fdf::ParseBuffer(buffer);
    if(!eRoot)
        return result;
    if(auto* eValue = eRoot->GetDirectChild("value"))
        fdf::detail::constexpr_memcpy(result.data(), eValue->GetComment().data(), SIZE);
    return result;
}

template<const char* buffer>
constexpr auto ExtractComment()
{
    constexpr size_t size = ExtractCommentSize(buffer);
    static constexpr auto commentArray = ExtractCommentArray<size>(buffer);
    return std::string_view(commentArray.data(), size);
}

constexpr char COMMENT_SAMPLE[] = "//TestComment\nvalue = 0";
static_assert(ExtractComment<COMMENT_SAMPLE>() == "TestComment", "consteval comment parse");
constexpr char COMPACT_BLOCK_COMMENT_SAMPLE[] = "/*abc*/\nvalue = 0";
static_assert(ExtractComment<COMPACT_BLOCK_COMMENT_SAMPLE>() == "abc", "consteval compact block comment parse");
constexpr char EMPTY_BLOCK_COMMENT_SAMPLE[] = "/**/\nvalue = 0";
static_assert(ExtractComment<EMPTY_BLOCK_COMMENT_SAMPLE>().empty(), "consteval empty block comment parse");
constexpr char SLASH_BLOCK_COMMENT_SAMPLE[] = "/*a/b*/\nvalue = 0";
static_assert(ExtractComment<SLASH_BLOCK_COMMENT_SAMPLE>() == "a/b", "consteval slash in block comment");
constexpr char SPACED_BLOCK_COMMENT_SAMPLE[] = "/*trim */\nvalue = 0";
static_assert(ExtractComment<SPACED_BLOCK_COMMENT_SAMPLE>() == "trim", "consteval block close spacing");
constexpr char MULTILINE_BLOCK_COMMENT_SAMPLE[] = "/*line\nnext\n*/\nvalue = 0";
static_assert(ExtractComment<MULTILINE_BLOCK_COMMENT_SAMPLE>() == "line\nnext", "consteval multiline block comment");
#endif




int main(int argc, char** argv)
{
    using namespace fdf::detail;

    for(int i = 1; i < argc; i++)
    {
        if(std::string_view(argv[i]) == "--stress")
            fdf::test::g_bStress = true;
    }

    std::filesystem::path currentDesignFile = FDF_ROOT_DIRECTORY "/examples/example.fdf";
    std::filesystem::path examplesDir = FDF_ROOT_DIRECTORY "/examples";   // genuine references
    std::filesystem::path casesDir    = FDF_TEST_DIRECTORY "/cases";      // stress / edge inputs
    std::filesystem::path outputDir   = FDF_OUTPUT_DIRECTORY;

    std::filesystem::create_directories(outputDir);

    // example.fdf goes first so RoundTripTest can round-trip filesToTest[0]
    if(std::filesystem::exists(currentDesignFile))
        filesToTest.emplace_back(currentDesignFile);

    auto collect = [&](const std::filesystem::path& dir, auto skip)
    {
        if(!std::filesystem::exists(dir))
            return;

        for(const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if(!entry.is_regular_file() || skip(entry.path()))
                continue;
            if(entry.path().extension() != ".fdf")
                continue;

            size_t length = filesToTest.emplace_back(entry.path()).inputFile.size();
            if(length > longestFilename)
                longestFilename = length;
        }
    };

    collect(examplesDir, [](const std::filesystem::path& p){ return p.filename() == "example.fdf"; });
    collect(casesDir,    [](const std::filesystem::path&){ return false; });

    if(filesToTest.empty())
    {
        std::println("[ERROR] No test files found");
        return -1;
    }


    constexpr std::string_view separator = "--------------------------------------------------\n";

    using fdf::test::RunCase;
    std::print("Running suite{} -- Found {} files\n{}", fdf::test::g_bStress? " (stress)" : "", filesToTest.size(), separator);

    RunCase("ParseTest",           Test::ParseTest);
    RunCase("ReadTest",            Test::ReadTest);
    RunCase("WriteTest",           Test::WriteTest);
    RunCase("ValueTest",           Test::ValueTest);
    RunCase("MutateTest",          Test::MutateTest);
    RunCase("RegressionTest",      Test::RegressionTest);
    RunCase("RecoveryTest",        Test::RecoveryTest);
    RunCase("DuplicateKeyPolicyTest", Test::DuplicateKeyPolicyTest);
    RunCase("NestingDepthTest",    Test::NestingDepthTest);
    RunCase("NegativeTest",        Test::NegativeTest);
    RunCase("PackTest",            Test::PackTest);
    RunCase("DigitSeparatorTest",  Test::DigitSeparatorTest);
    RunCase("StringRoundTripTest", Test::StringRoundTripTest);
    RunCase("StringStorageTest",   Test::StringStorageTest);
    RunCase("HexStorageTest",      Test::HexStorageTest);
    RunCase("StringApiTest",       Test::StringApiTest);
    RunCase("FloatRoundTripTest",  Test::FloatRoundTripTest);
    RunCase("TimestampTest",       Test::TimestampTest);
    RunCase("DurationTest",        Test::DurationTest);
    RunCase("AllocatorTest",       Test::AllocatorTest);
    RunCase("RoundTripTest",       Test::RoundTripTest);
#if !FDF_NO_COMMENTS
    RunCase("WriterCommentTest",   Test::WriterCommentTest);
#endif

    std::print("{}", separator);
    if(fdf::test::g_failedCases.empty())
    {
        std::println("PASSED -- {} checks across all cases", fdf::test::g_checks);
        return 0;
    }

    std::println("FAILED -- {} of {} checks failed in {} case(s):",
                 fdf::test::g_failed, fdf::test::g_checks, fdf::test::g_failedCases.size());
    for(const std::string& name : fdf::test::g_failedCases)
        std::println("  - {}", name);
    return 1;
}
