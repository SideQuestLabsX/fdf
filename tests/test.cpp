
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
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
    // Order must match TokenType in fdf.h
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

    // Order must match Type in fdf.h
    constexpr std::string_view ENTRY_TYPE_TO_STRING[] =
    {
        "Map      ",
        "Array    ",
        "Null     ",
        "Bool     ",
        "Int      ",
        "UInt     ",
        "Float    ",
        "String   ",
        "Hex      ",
        "Version  ",
        "Timestamp"
    };
}




namespace fdf::test
{
    inline int g_checks = 0;
    inline int g_failed = 0;
    inline int g_caseChecks = 0;
    inline int g_caseFailed = 0;
    inline std::vector<std::string> g_failedCases;

    // On a tty the RUN line is rewritten in place; captured logs get the completion line only
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

// Component 0 of a string-ish entry (String/Hex/Timestamp) as a view, via the read-only span
[[nodiscard]] constexpr std::string_view FirstString(const fdf::Entry& e) noexcept
{
    const std::span<const fdf::String> parts = e.GetValue<fdf::String>();
    return parts.empty()? std::string_view{} : std::string_view(parts[0]);
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




namespace fdf::detail
{
    struct TestDirectories
    {
        TestDirectories(const std::filesystem::path& file)
        {
            inputFile = file.generic_string();
            outputFile = FDF_OUTPUT_DIRECTORY "/" + file.stem().generic_string();

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

            std::string content((std::istreambuf_iterator<char>(iFile)), std::istreambuf_iterator<char>());
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

            std::string buffer;
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

            oFile << buffer;
            return static_cast<bool>(oFile);
        }

        static bool PrintAllEntries(const Entry* e, std::string_view outFile)
        {
            std::ofstream file(outFile.data());
            if(!file)
                return false;

            std::string buffer;
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

            std::string temp;
            e->ForEach<ForEachFlags::Recursive | ForEachFlags::Group>([&](const Entry& entry)
            {
                addToBuffer(std::format("{:<{}}Type={}--Size={:03}--Name={:<20}--Value={:<50}--Comment={}", "", 4 * entry.CalculateDepth(), ENTRY_TYPE_TO_STRING[static_cast<size_t>(entry.GetType())], entry.GetChildCount(), entry.GetFullIdentifier(), entry.DataToView(temp), std::string_view(entry.GetComment())));
                buffer.push_back('\n');
            });

            file << buffer;
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

                std::string durationString = std::format("{:.6f}ms", static_cast<double>(duration.count()) / 1'000'000.0);
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




        // Structure + value assertions against an embedded document. The literal and its expected
        // counts live side by side, so editing one forces editing the other (no coupling to a far
        // external file). Exercises the same breadth as examples/example.fdf: every scalar type,
        // packs, nested maps/arrays, and string escapes
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

            // Derived from READ_DOC: 13 top-level entries, recursive adds gameSettings1's 3 children
            // + 3 tag elements, and players' 2 maps + their 4 leaves = 25
            CHECK_MSG(e->GetChildCountRecursive() == 25, std::format("recursive count = {}", e->GetChildCountRecursive()));
            CHECK_MSG(e->GetChildCount() == 13, std::format("top level count = {}", e->GetChildCount()));

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
        }




        // SetValue/GetValue round-trips for every scalar and span overload, plus type tagging
        static void ValueTest()
        {
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
                CHECK(e->GetType() == Type::UInt && v.size() == 1 && v[0] == 42u);
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
                CHECK(e->GetType() == Type::UInt && v.size() == 2 && v[0] == 1u && v[1] == 2u);
            }

            if(Entry* e = root->Emplace("dbls"); CHECK(e))
            {
                double dbls[3] = { 1.0, 2.5, 3.0 };
                e->SetValue(std::span(dbls, 3));
                auto v = e->GetValue<double>();
                CHECK(e->GetType() == Type::Float && v.size() == 3 && v[0] == 1.0 && v[1] == 2.5 && v[2] == 3.0);
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
                auto v = e->GetValue<Version>();
                CHECK(e->GetType() == Type::Version && v.size() == 2);
                CHECK(!v[0].bHasRevision && v[0].major == 1 && v[0].minor == 2 && v[0].patch == 3 && v[0].revision == 0);
                CHECK(v[1].bHasRevision && v[1].major == 4 && v[1].minor == 5 && v[1].patch == 6 && v[1].revision == 0);
                v[0].patch = 9;
                CHECK(e->GetValue<Version>()[0].patch == 9);
            }

            // Set from a Timestamp reference, decode it back
            if(Entry* e = root->Emplace("t"); CHECK(e))
            {
                const Timestamp ts = Timestamp::DateTime(2024, 12, 24, 15, 30, 0);
                e->SetValue(ts);
                CHECK(e->GetType() == Type::Timestamp);
                Timestamp got = e->GetValue<Timestamp>();
                CHECK(got.year == 2024 && got.month == 12 && got.day == 24 && got.hour == 15 && got.minute == 30);
            }

            // Re-set overwrites type and value cleanly
            if(Entry* e = root->Emplace("re"); CHECK(e))
            {
                e->SetValue(123);
                e->SetValue("now a string");
                CHECK(e->GetType() == Type::String && FirstString(*e) == "now a string");
            }
        }

        // Exercises container growth, RemoveChild/OrphanChild index shifting, GetDirectChild(index)
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

            // Orphan keeps the node alive and detaches it from the parent
            if(UniqueEntryPtr orphan = root->OrphanChild("k10"); CHECK(static_cast<bool>(orphan)))
            {
                CHECK(orphan->GetParent() == nullptr);
                CHECK(root->GetChild("k10") == nullptr);
                CHECK(root->GetChildCount() == count - 2);
            }

            // Resize packable scalar arrays: existing elements preserved, new ones zero-filled
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
                auto grown = e->GetValue<Version>();
                CHECK(grown.size() == 3 && grown[0].major == 1 && grown[1].major == 4
                    && grown[2].major == 0 && !grown[2].bHasRevision);
                e->Resize(1);
                CHECK(e->GetValue<Version>().size() == 1 && e->GetValue<Version>()[0].major == 1);

                // revision without bHasRevision is normalized to 0 on ingest
                e->SetValue(Version{ .bHasRevision = false, .major = 9, .minor = 0, .patch = 0, .revision = 42 });
                CHECK(e->GetValue<Version>()[0].revision == 0);
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
            }

            // SetType frees the payload and empties the entry; Resize must grow cleanly from no slab
            if(Entry* e = root->Emplace("retyped"); CHECK(e))
            {
                int64_t nums[5] = { 1, 2, 3, 4, 5 };
                e->SetValue(std::span(nums, 5));
                e->SetType(Type::Bool);
                e->Resize(3);
                auto v = e->GetValue<bool>();
                CHECK(e->GetType() == Type::Bool && v.size() == 3 && v[0] == false && v[1] == false && v[2] == false);
            }

            // Hex/Timestamp reject Resize: an empty component isn't valid hex or timestamp text
            if(UniqueEntryPtr doc = ParseBuffer(std::string("h = 0xFF|0x80\nt = 2024-01-02|2024-03-04\n")))
            {
                if(Entry* h = doc->GetChild("h"); CHECK(h && h->GetType() == Type::Hex))
                {
                    h->Resize(4);
                    CHECK(std::as_const(*h).GetValue<String>().size() == 2);   // unchanged, Resize was a no-op
                }
                if(Entry* t = doc->GetChild("t"); CHECK(t && t->GetType() == Type::Timestamp))
                {
                    t->Resize(4);
                    CHECK(std::as_const(*t).GetValue<String>().size() == 2);
                }
            }
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

            // Recovery inside a map must work, and identically whether single-line or multi-line
            auto checkMap = [](std::string_view src)
            {
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>(src);
                if(!CHECK_MSG(static_cast<bool>(root), src))
                    return;
                Entry* m = root->GetChild("m");
                if(CHECK(m && m->GetType() == Type::Map))
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
                if(CHECK(arr && arr->GetType() == Type::Array))
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
        #endif

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

            // An unterminated string is a fatal lexer error reported as UnterminatedString
            {
                UniqueEntryPtr root = ParseBuffer<&CountDiagnostics>("s = \"abc\n");
                CHECK(root == nullptr);
                CHECK(test::g_lastDiagnostic.type == DiagnosticType::UnterminatedString);
                CHECK(test::g_lastDiagnostic.severity == DiagnosticSeverity::Fatal);
            }
        }




        // Malformed inputs each surface the right DiagnosticType. Fatal lexer errors null the tree;
        // recoverable parse errors skip the bad entry and keep the valid ones
        static void NegativeTest()
        {
            // type, source, expectNull (fatal), and the surviving sibling for recoverable cases
            struct Case { DiagnosticType type; std::string_view src; bool bFatal; };
            constexpr Case cases[] =
            {
                { DiagnosticType::InvalidTimestamp,    "ts = 2024-13-45\nok = 1\n",                          false },
                { DiagnosticType::InvalidTimestamp,    "ts = 25:99:00\nok = 1\n",                            false },
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

            // A valid timestamp of every accepted shape parses and keeps its raw text
            constexpr std::string_view goodTs[] =
            {
                "2024-12-24", "2024-359", "2024-W52-2", "15:30:00",
                "2024-12-24T15:30:00", "2024-12-24T15:30:00.123Z", "15:30:00-05:00",
            };
            for(std::string_view ts : goodTs)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("t = {}\n", ts));
                if(Entry* e = root? root->GetChild("t") : nullptr; CHECK_MSG(e && e->GetType() == Type::Timestamp, ts))
                    CHECK(FirstString(*e) == ts);
            }

            // A buffer at/above the 32-bit offset limit is refused before parsing. The guard reads only
            // size(), so a view that lies about its length over a tiny buffer exercises it without a 4GB alloc
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
                if(!CHECK_MSG(e && e->GetType() == Type::Int, src))
                    return;
                auto s = e->GetValue<int64_t>();
                if(!CHECK_MSG(s.size() == exp.size(), src))
                    return;
                size_t i = 0;
                for(int64_t x : exp)
                    CHECK_MSG(s[i++] == x, src);
            };
            auto checkFloat = [](std::string_view src, std::initializer_list<double> exp)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("v = {}\n", src));
                Entry* e = root? root->GetChild("v") : nullptr;
                if(!CHECK_MSG(e && e->GetType() == Type::Float, src))
                    return;
                auto s = e->GetValue<double>();
                if(!CHECK_MSG(s.size() == exp.size(), src))
                    return;
                size_t i = 0;
                for(double x : exp)
                    CHECK_MSG(s[i++] == x, src);
            };
            auto checkBool = [](std::string_view src, std::initializer_list<bool> exp)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("v = {}\n", src));
                Entry* e = root? root->GetChild("v") : nullptr;
                if(!CHECK_MSG(e && e->GetType() == Type::Bool, src))
                    return;
                auto s = e->GetValue<bool>();
                if(!CHECK_MSG(s.size() == exp.size(), src))
                    return;
                size_t i = 0;
                for(bool x : exp)
                    CHECK_MSG(s[i++] == x, src);
            };
            auto checkStrings = [](std::string_view src, std::initializer_list<std::string_view> exp)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("v = {}\n", src));
                Entry* e = root? root->GetChild("v") : nullptr;
                if(!CHECK_MSG(e && e->GetType() == Type::String, src))
                    return;
                const std::span<const String> parts = e->GetValue<String>();
                if(!CHECK_MSG(parts.size() == exp.size(), src))
                    return;
                size_t i = 0;
                for(std::string_view x : exp)
                    CHECK_MSG(parts[i++] == x, src);
            };
            auto checkVersions = [](std::string_view src, std::initializer_list<Version> exp)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("v = {}\n", src));
                Entry* e = root? root->GetChild("v") : nullptr;
                if(!CHECK_MSG(e && e->GetType() == Type::Version, src))
                    return;
                const std::span<const Version> versions = e->GetValue<Version>();
                if(!CHECK_MSG(versions.size() == exp.size(), src))
                    return;
                size_t i = 0;
                for(const Version& version : exp)
                    CHECK_MSG(versions[i++] == version, src);
            };

            // Int: dimensions 2..5, negatives in leading / middle / trailing / all positions
            checkInt("1|2",         { 1, 2 });
            checkInt("1|2|3",       { 1, 2, 3 });
            checkInt("1|2|3|4",     { 1, 2, 3, 4 });
            checkInt("1|2|3|4|5",   { 1, 2, 3, 4, 5 });
            checkInt("-1|2",        { -1, 2 });
            checkInt("1|-2",        { 1, -2 });
            checkInt("1|2|-3",      { 1, 2, -3 });
            checkInt("-1|-2|-3",    { -1, -2, -3 });

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

            // UInt component beyond INT64_MAX keeps the whole pack unsigned
            if(UniqueEntryPtr root = ParseBuffer(std::string("v = 18446744073709551615|1\n")))
            {
                Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::UInt))
                {
                    auto s = e->GetValue<uint64_t>();
                    CHECK(s.size() == 2 && s[0] == 18446744073709551615ull && s[1] == 1);
                }
            }

            // string packs: components decode escapes like plain strings, a '|' inside quotes is
            // literal (the lexer only splits unquoted pipes)
            checkStrings("\"alice\"|\"bob\"|\"carol\"",  { "alice", "bob", "carol" });
            checkStrings("\"with|pipe\"|\"b\"",          { "with|pipe", "b" });
            checkStrings("\"a\\\"q\"|'b\\tc'",           { "a\"q", "b\tc" });
            checkStrings("\"\"|\"x\"",                   { "", "x" });

            // hex and timestamp packs keep each component's raw text, read through the const span
            auto checkText = [](std::string_view src, Type type, std::initializer_list<std::string_view> exp)
            {
                UniqueEntryPtr root = ParseBuffer(std::format("v = {}\n", src));
                const Entry* e = root? root->GetChild("v") : nullptr;
                if(!CHECK_MSG(e && e->GetType() == type, src))
                    return;
                const std::span<const String> parts = e->GetValue<String>();
                if(!CHECK_MSG(parts.size() == exp.size(), src))
                    return;
                size_t i = 0;
                for(std::string_view x : exp)
                    CHECK_MSG(parts[i++] == x, src);
            };
            checkText("0xFF|0xAA",                Type::Hex,       { "0xFF", "0xAA" });
            checkText("0x1|0x2|0x3",              Type::Hex,       { "0x1", "0x2", "0x3" });
            checkText("2024-12-24|15:30:00",      Type::Timestamp, { "2024-12-24", "15:30:00" });
            checkText("2024-12-24T15:30:00Z|2024-359", Type::Timestamp, { "2024-12-24T15:30:00Z", "2024-359" });

            // GetValue<Timestamp> on a timestamp pack decodes component 0
            if(UniqueEntryPtr root = ParseBuffer(std::string("v = 2024-12-24|15:30:00\n")))
            {
                const Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::Timestamp))
                {
                    const Timestamp ts = e->GetValue<Timestamp>();
                    CHECK(ts.year == 2024 && ts.month == 12 && ts.day == 24);
                }
            }

            // hex/timestamp pack write -> reparse round trip, unquoted atoms joined with '|'
            for(std::string_view src : { "v = 0xFF|0xAA\n", "v = 2024-12-24|15:30:00\n" })
            {
                UniqueEntryPtr root = ParseBuffer(src);
                if(!CHECK_MSG(static_cast<bool>(root), src))
                    continue;
                std::string out;
                WriteBuffer<Style{ .bCommas = false }>(*root, out);
                CHECK_MSG(!out.contains('"') && !out.contains('\''), out);

                UniqueEntryPtr reparsed = ParseBuffer(out);
                const Entry* a = root->GetChild("v");
                const Entry* b = reparsed? reparsed->GetChild("v") : nullptr;
                if(CHECK_MSG(a && b && a->GetType() == b->GetType(), out))
                {
                    const std::span<const String> pa = a->GetValue<String>();
                    const std::span<const String> pb = b->GetValue<String>();
                    if(CHECK_MSG(pa.size() == pb.size(), out))
                    {
                        for(size_t i = 0; i < pa.size(); i++)
                            CHECK_MSG(pa[i] == pb[i], out);
                    }
                }
            }

            for(std::string_view src : { "v = 1.2.3|4.5.6.0\n", "v = 2147483647.4294967295.4294967295.4294967295\n" })
            {
                UniqueEntryPtr root = ParseBuffer(src);
                std::string out;
                WriteBuffer<Style{ .bCommas = false }>(*root, out);
                UniqueEntryPtr reparsed = ParseBuffer(out);
                const Entry* a = root->GetChild("v");
                const Entry* b = reparsed? reparsed->GetChild("v") : nullptr;
                CHECK_MSG(a && b && SpanEqual(a->GetValue<Version>(), b->GetValue<Version>()), out);
            }

            // scalar accessors on a string pack fall back to the first component
            if(UniqueEntryPtr root = ParseBuffer(std::string("v = \"first\"|\"second\"\n")))
            {
                Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::String))
                {
                    CHECK(FirstString(*e) == "first");

                    const std::span<const String> parts = e->GetValue<String>();
                    std::string joined;
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

            // mutation through the span: an edit reallocs that component's own chunk in place, the
            // span itself never moves (valid until SetValue/ReleaseData), and a component can grow
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

            // empty span becomes a single empty component (a string value is never zero components)
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

                    std::string out;
                    WriteBuffer<Style{ .bCommas = false }>(*root, out);

                    UniqueEntryPtr reparsed = ParseBuffer(out);
                    Entry* r = reparsed? reparsed->GetChild("v") : nullptr;
                    if(CHECK_MSG(r && r->GetType() == Type::String, out))
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

            // malformed packs are rejected but recoverable: bad entry skipped, sibling survives.
            // also the heap-corruption regression, a mid-parse failure must not leave a Map-typed
            // entry pointing at a scalar buffer
            constexpr std::string_view recover[] =
            {
                "bad = 1|99999999999999999999999\nok = 7\n",   // component overflows u64
                "bad = -1|99999999999999999999999\nok = 7\n",  // negative then unsigned-overflow
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
                    if(CHECK_MSG(ok && ok->GetType() == Type::Int, src))
                        CHECK_MSG(ok->GetValue<int64_t>()[0] == 7, src);
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




        // Strings with embedded quotes survive write -> reparse. The writer picks a quote style that
        // needs no escaping where it can, and escapes embedded double quotes otherwise
        static void StringRoundTripTest()
        {
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

                std::string out;
                WriteBuffer<Style{ .bCommas = false }>(*root, out);

                UniqueEntryPtr reparsed = ParseBuffer(out);
                if(Entry* e = reparsed? reparsed->GetChild("s") : nullptr; CHECK_MSG(e && e->GetType() == Type::String, out))
                    CHECK_MSG(FirstString(*e) == want, out);
            }
        }




        // fdf::String storage: scalar is a String[1], mutable via span, span stable until SetValue/ReleaseData, empty holds no allocation
        static void StringStorageTest()
        {
            CHECK(sizeof(fdf::String) == 8);

            if(UniqueEntryPtr root = ParseBuffer(std::string("v = \"hello\"\n")))
            {
                Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::String))
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

                    std::string out;
                    WriteBuffer<Style{ .bCommas = false }>(*root, out);
                    UniqueEntryPtr reparsed = ParseBuffer(out);
                    Entry* r = reparsed? reparsed->GetChild("v") : nullptr;
                    if(CHECK_MSG(r && r->GetType() == Type::String, out))
                        CHECK_MSG(FirstString(*r) == "second value", out);
                }
            }

            // Hex/Timestamp are String[1] too: readable through the const span, but the mutable
            // span is Type::String-only (writable text could break their format invariants)
            if(UniqueEntryPtr root = ParseBuffer(std::string("h = 0xFF5733\nt = 2024-12-24T15:30:00\n")))
            {
                Entry* h = root->GetChild("h");
                Entry* t = root->GetChild("t");
                CHECK(h && h->GetType() == Type::Hex && FirstString(*h) == "0xFF5733");
                CHECK(t && t->GetType() == Type::Timestamp && FirstString(*t) == "2024-12-24T15:30:00");
                if(h && t)
                {
                    CHECK(std::as_const(*h).GetValue<String>().size() == 1);   // const gate: readable
                    CHECK(std::as_const(*t).GetValue<String>().size() == 1);
                    CHECK(h->GetValue<String>().empty());                      // mutable gate: String-only
                    CHECK(t->GetValue<String>().empty());
                }
            }
        }




        // fdf::String std API: terminator invariant, edit ops (growth preserves content, within-cap no
        // realloc), read one-liners, operator+ chains incl. move-lhs reuse, comparisons, std::string + formatter
        static void StringApiTest()
        {
            using fdf::String;

            // c_str terminator invariant: strlen tracks size across every mutation
            {
                String s;
                CHECK(s.c_str()[0] == '\0' && std::string_view(s.c_str()).empty());   // null state -> ""
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

            // read one-liners spot check
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

            // operator+ chains, incl. move-lhs reuse
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

            // explicit std::string conversion + std::formatter
            {
                String s("convert");
                CHECK(static_cast<std::string>(s) == "convert");
                CHECK(std::format("[{}]", s) == "[convert]");
                CHECK(std::format("{:>9}", s) == "  convert");   // padding rides the string_view formatter
            }

            // self-aliasing mutations: value points into this->block, std::string guarantees these
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




        // Shortest round-trip floats: write(x) then reparse must reproduce x bitwise, for every
        // finite double. Covers the hand-rolled Dragon4 writer + AlgorithmM parser, both used at
        // compile time and runtime. Also checks scientific/exponent input forms
        static void FloatRoundTripTest()
        {
            auto roundTrip = [](double x) -> bool
            {
                UniqueEntryPtr root = NewEntry();
                if(!root)
                    return false;
                if(Entry* e = root->Emplace("v"))
                    e->SetValue(x);
                std::string out;
                WriteBuffer<Style{ .bCommas = false }>(*root, out);

                UniqueEntryPtr re = ParseBuffer(out);
                Entry* e = re? re->GetChild("v") : nullptr;
                if(!e || e->GetType() != Type::Float)
                    return false;
                auto span = e->GetValue<double>();
                return span.size() == 1 && std::bit_cast<uint64_t>(span[0]) == std::bit_cast<uint64_t>(x);
            };

            // Compile-time proof that the shared write/parse path round-trips in a consteval context
            // (Subnormal/edge constexpr cases are covered by the runtime fuzz below to keep the
            // static_assert's constexpr step count modest for MSVC.)
            static_assert(std::bit_cast<uint64_t>([]
            {
                std::string s;
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
            if(UniqueEntryPtr root = ParseBuffer(std::string("v = 1.0e\n")))
                CHECK(!root->GetChild("v") || root->GetChild("v")->GetType() != Type::Float);

            // Multi-dimensional float with an exponent component
            if(UniqueEntryPtr root = ParseBuffer(std::string("d = 1.5|2.0e3|0.001\n")))
            {
                Entry* e = root->GetChild("d");
                if(CHECK(e && e->GetType() == Type::Float))
                {
                    auto s = e->GetValue<double>();
                    CHECK(s.size() == 3 && s[0] == 1.5 && s[1] == 2.0e3 && s[2] == 0.001);
                }
            }

            // Multi-dimensional float with negative components
            if(UniqueEntryPtr root = ParseBuffer(std::string("d = 0.5|-0.5|1.0\n")))
            {
                Entry* e = root->GetChild("d");
                if(CHECK(e && e->GetType() == Type::Float))
                {
                    auto s = e->GetValue<double>();
                    CHECK(s.size() == 3 && s[0] == 0.5 && s[1] == -0.5 && s[2] == 1.0);
                }
            }

            // Negative component alongside an exponent
            if(UniqueEntryPtr root = ParseBuffer(std::string("d = 1.5e3|-2.5\n")))
            {
                Entry* e = root->GetChild("d");
                if(CHECK(e && e->GetType() == Type::Float))
                {
                    auto s = e->GetValue<double>();
                    CHECK(s.size() == 2 && s[0] == 1.5e3 && s[1] == -2.5);
                }
            }

            // A dash not immediately after '|' is not a valid float component
            if(UniqueEntryPtr root = ParseBuffer(std::string("d = 1.0-2.0\n")))
                CHECK(!root->GetChild("d") || root->GetChild("d")->GetType() != Type::Float);

            // Randomized fuzz: normals + subnormals through the full write/parse pipeline
            std::mt19937_64 rng(0xF0F0CABA);
            int fuzzFails = 0;
            for(int i = 0; i < 300000; i++)
            {
                double x = std::bit_cast<double>(rng());
                if(std::isnan(x) || std::isinf(x))
                    continue;
                if(!roundTrip(x) && ++fuzzFails <= 5)
                    CHECK_MSG(false, std::format("fuzz {:.17g}", x));
            }
            for(int i = 0; i < 100000; i++)
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




        // GetValue<Timestamp> decodes the raw text; SetValue(Timestamp) injects it back as canonical
        // ISO text. Covers field extraction, epoch conversion, ordinal/week normalization, and inject
        static void TimestampTest()
        {
            // Field extraction from a fully-specified value
            {
                UniqueEntryPtr root = ParseBuffer("t = 2024-12-24T15:30:00.123Z\n");
                Entry* e = root? root->GetChild("t") : nullptr;
                if(CHECK(e && e->GetType() == Type::Timestamp))
                {
                    Timestamp ts = e->GetValue<Timestamp>();
                    CHECK(ts.IsValid() && ts.bHasDate && ts.bHasTime);
                    CHECK(ts.year == 2024 && ts.month == 12 && ts.day == 24);
                    CHECK(ts.hour == 15 && ts.minute == 30 && ts.second == 0);
                    CHECK(ts.nanosecond == 123'000'000 && ts.fracDigits == 3);
                    CHECK(ts.tzKind == Timestamp::TzKind::Utc);
                }
            }

            // Epoch extraction: the famous Unix 1e9 instant, plus nanos and millis
            {
                Timestamp ts = ParseBuffer("t = 2001-09-09T01:46:40Z\n")->GetChild("t")->GetValue<Timestamp>();
                CHECK(ts.ToUnixSeconds() == 1'000'000'000);
                CHECK(ts.ToUnixMillis() == 1'000'000'000'000);
                CHECK(ts.ToUnixNanos()  == 1'000'000'000'000'000'000);
            }

            // A timezone offset normalizes to the same UTC instant as its Z equivalent
            {
                Timestamp off = ParseBuffer("t = 2024-01-01T12:00:00+05:00\n")->GetChild("t")->GetValue<Timestamp>();
                Timestamp utc = ParseBuffer("t = 2024-01-01T07:00:00Z\n")->GetChild("t")->GetValue<Timestamp>();
                CHECK(off.ToUnixSeconds() == utc.ToUnixSeconds());
                CHECK(off.tzOffsetMin == 300);
            }

            // Ordinal and week dates normalize to the same calendar day, remembering their origin
            {
                Timestamp ord  = ParseBuffer("t = 2024-359\n")->GetChild("t")->GetValue<Timestamp>();
                Timestamp week = ParseBuffer("t = 2024-W52-2\n")->GetChild("t")->GetValue<Timestamp>();
                CHECK(ord.year == 2024 && ord.month == 12 && ord.day == 24);
                CHECK(week.year == 2024 && week.month == 12 && week.day == 24);
                CHECK(ord.dateKind == Timestamp::DateKind::Ordinal);
                CHECK(week.dateKind == Timestamp::DateKind::Week);
            }

            // Time-only: no date present
            {
                Timestamp ts = ParseBuffer("t = 15:30:00\n")->GetChild("t")->GetValue<Timestamp>();
                CHECK(ts.IsValid() && ts.bHasTime && !ts.bHasDate);
                CHECK(ts.hour == 15 && ts.minute == 30);
            }

            // Inject from components, read back the fields and the canonical text
            {
                UniqueEntryPtr root = NewEntry();
                if(Entry* e = root->Emplace("t"); CHECK(e))
                {
                    e->SetValue(Timestamp::DateTime(2024, 12, 24, 15, 30, 0));
                    CHECK(e->GetType() == Type::Timestamp);
                    CHECK(FirstString(*e) == "2024-12-24T15:30:00");
                    Timestamp got = e->GetValue<Timestamp>();
                    CHECK(got.year == 2024 && got.month == 12 && got.day == 24 && got.hour == 15);
                }
            }

            // Inject from epoch, round-trips back to the same instant and the expected text
            {
                UniqueEntryPtr root = NewEntry();
                if(Entry* e = root->Emplace("t"); CHECK(e))
                {
                    e->SetValue(Timestamp::FromUnixSeconds(1'000'000'000));
                    CHECK(FirstString(*e) == "2001-09-09T01:46:40Z");
                    CHECK(e->GetValue<Timestamp>().ToUnixSeconds() == 1'000'000'000);
                }
            }

            // Epoch -> struct -> epoch is lossless across a spread of instants (incl. pre-1970)
            for(int64_t s : { int64_t(0), int64_t(1'000'000'000), int64_t(1'700'000'000), int64_t(-86400), int64_t(-1) })
                CHECK_MSG(Timestamp::FromUnixSeconds(s).ToUnixSeconds() == s, std::format("epoch {}", s));

            // Parsed values keep their exact original text through write (ordinal/week NOT normalized);
            // GetValue<Timestamp> decodes on demand without touching storage
            for(std::string_view raw : { "2024-359", "2024-W52-2", "15:30:00", "2024-12-24T15:30:00.123Z" })
            {
                UniqueEntryPtr root = ParseBuffer(std::format("t = {}\n", raw));
                std::string out;
                WriteBuffer<Style{ .bCommas = false }>(*root, out);
                CHECK_MSG(out.contains(raw), out);
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

        static bool EqualIgnoreCase(std::string_view a, std::string_view b)
        {
            if(a.size() != b.size())
                return false;
            auto up = [](char c) { return (c >= 'a' && c <= 'z')? static_cast<char>(c - 32) : c; };
            for(size_t i = 0; i < a.size(); i++)
                if(up(a[i]) != up(b[i]))
                    return false;
            return true;
        }

        // Semantic value equality (not byte equality): hex is case-insensitive, floats compare as the
        // decoded doubles, Null/Nil are the same type. Containers are handled by TreeEqual
        static bool ValueEqual(const Entry& a, const Entry& b)
        {
            if(a.GetType() != b.GetType())
                return false;
            switch(a.GetType())
            {
                case Type::Null:                      return true;
                case Type::Bool:                      return SpanEqual(a.GetValue<bool>(),     b.GetValue<bool>());
                case Type::Int:                       return SpanEqual(a.GetValue<int64_t>(),  b.GetValue<int64_t>());
                case Type::UInt:                       return SpanEqual(a.GetValue<uint64_t>(), b.GetValue<uint64_t>());
                case Type::Version:                    return SpanEqual(a.GetValue<Version>(),  b.GetValue<Version>());
                case Type::Float:                     return SpanEqual(a.GetValue<double>(),   b.GetValue<double>());
                case Type::String: case Type::Timestamp: return SpanEqual(a.GetValue<String>(), b.GetValue<String>());
                case Type::Hex:
                {
                    const std::span<const String> ha = a.GetValue<String>();
                    const std::span<const String> hb = b.GetValue<String>();
                    if(ha.size() != hb.size())
                        return false;
                    for(size_t i = 0; i < ha.size(); i++)
                    {
                        if(!EqualIgnoreCase(ha[i], hb[i]))
                            return false;
                    }
                    return true;
                }
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

        // parse -> write<STYLE> -> parse, assert the tree survived; then write the reparsed tree again
        // and assert byte-stability (the writer is idempotent once the text is in canonical form)
        template<Style STYLE>
        static void RoundTrip(const Entry& original, std::string_view label, bool bCheckStable)
        {
            std::string out;
            WriteBuffer<STYLE>(original, out);

            UniqueEntryPtr rt = ParseBuffer(out);
            if(!CHECK_MSG(rt && TreeEqual(original, *rt), std::format("[{}] tree mismatch:\n{}", label, out)))
                return;

            if(bCheckStable)
            {
                std::string out2;
                WriteBuffer<STYLE>(*rt, out2);
                CHECK_MSG(out == out2, std::format("[{}] writer not idempotent", label));
            }
        }

        // Every type, multi-dim, empty/quoted/escaped strings, nested containers, and timestamps,
        // round-tripped through a spread of Style permutations
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
                "hmd = 0xFF|0xAA|0x01\n"
                "ver3 = 1.2.3\n"
                "ver4 = 1.2.3.4\n"
                "tsDate = 2024-12-24\n"
                "tsFull = 2024-12-24T15:30:00.123Z\n"
                "tsOrd = 2024-359\n"
                "tsWeek = 2024-W52-2\n"
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

            // The real canonical design file across a few styles (exercises comments + deep nesting)
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
                Alloc::AllocationResult r = Alloc::AllocateAtLeast(request);
                if(!CHECK_MSG(r.ptr != nullptr, std::format("request {}", request)))
                    return;
                CHECK_MSG(r.size == expected, std::format("request {} -> granted {} (want {})", request, r.size, expected));
                std::memset(r.ptr, 0xAB, r.size);   // whole granted extent must be writable
                Alloc::Deallocate(r.ptr, r.size);
            };

            // bit_ceil ladder with an 8-byte floor, boundaries either side of each bucket
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

            // Round-trip through Allocate(size)/Deallocate(size) at each bucket boundary
            for(size_t size : { size_t(8), size_t(16), size_t(32), size_t(64), size_t(128), size_t(256), size_t(4096) })
            {
                void* p = Alloc::Allocate(size);
                if(CHECK_MSG(p != nullptr, std::format("Allocate({})", size)))
                {
                    std::memset(p, 0xCD, size);
                    CHECK(Alloc::Deallocate(p, size));
                }
            }

            // Distinct live requests hand back distinct, independently writable blocks
            {
                void* a = Alloc::Allocate(65);
                void* b = Alloc::Allocate(65);
                void* c = Alloc::Allocate(200);
                CHECK(a != b && a != c && b != c);
                if(a && b && c)
                {
                    std::memset(a, 1, 65);
                    std::memset(b, 2, 65);
                    std::memset(c, 3, 200);
                    CHECK(*static_cast<unsigned char*>(a) == 1);
                    CHECK(*static_cast<unsigned char*>(b) == 2);
                    CHECK(*static_cast<unsigned char*>(c) == 3);
                }
                Alloc::Deallocate(a, 65);
                Alloc::Deallocate(b, 65);
                Alloc::Deallocate(c, 200);
            }

            // Child vector regrowth across every bucket into the heap fallback (40 * 8B > 256)
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
            // Comment replacement: shrink in place, regrow within the granted slack, then force heap
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
                    const std::string longComment(300, 'x');
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
                std::string out;
                WriteBuffer<Style{ .bAlignCloseComments = true, .bCommas = false }>(*root, out);
                CHECK(out.find('\x01') == std::string::npos);  // no pad placeholder leaks
                CHECK(out.contains("// first") && out.contains("// second") && out.contains("// third"));
                auto cols = InlineCommentColumns(out);
                if(CHECK(cols.size() == 3))
                    CHECK(cols[0] > 0 && cols[0] == cols[1] && cols[1] == cols[2]);
            }

            // Unaligned: still inline, but each '//' just one space after its value (varying columns)
            {
                std::string out;
                WriteBuffer<Style{ .bAlignCloseComments = false, .bCommas = false }>(*root, out);
                auto cols = InlineCommentColumns(out);
                if(CHECK(cols.size() == 3))
                    CHECK(cols[0] != cols[2]);  // a=1 vs ccc=333 differ in width
            }

            // bEntryComment = false suppresses comments entirely
            {
                std::string out;
                WriteBuffer<Style{ .bEntryComment = false }>(*root, out);
                CHECK(!out.contains("//"));
            }

            // Raw storage: GetComment hands out the stored text unchanged, the mutable ref edits
            // in place, and the writer collapses newlines + leading whitespace at emit
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

                    std::string out;
                    WriteBuffer<Style{ .bCommas = false }>(*r, out);
                    CHECK_MSG(out.contains("// lead tail more"), out);

                    UniqueEntryPtr re = ParseBuffer(out);
                    Entry* k = re? re->GetChild("k") : nullptr;
                    if(CHECK(k != nullptr))
                        CHECK(k->GetComment() == "lead tail more");   // reparse yields the emitted form
                }
            }

            // File comment: stored raw, the block emit strips per-line leading whitespace, drops
            // blank lines, neutralizes a contained close sequence, and the written form is stable
            {
                UniqueEntryPtr r = NewEntry();
                if(CHECK(static_cast<bool>(r)))
                {
                    r->Emplace("k")->SetValue(static_cast<int64_t>(1));
                    r->GetComment() = "   header line\n\n   evil */ inside";
                    CHECK(r->GetComment() == "   header line\n\n   evil */ inside");   // raw

                    std::string out1;
                    WriteBuffer<Style{ .bCommas = false }>(*r, out1);
                    CHECK_MSG(out1.starts_with("/*#\n"), out1);
                    CHECK_MSG(out1.contains("    header line\n"), out1);   // leading whitespace stripped, newline kept
                    CHECK_MSG(out1.contains("evil * / inside"), out1);     // '*/' broken so the block can't close early

                    UniqueEntryPtr re = ParseBuffer(out1);
                    if(CHECK(static_cast<bool>(re)))
                    {
                        std::string out2;
                        WriteBuffer<Style{ .bCommas = false }>(*re, out2);
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
    return s.size() == 3 && s[0] == 7 && s[1] == 0 && s[2] == 0;
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
    return regrown.size() == 3 && regrown[0] == "alpha" && regrown[1] == "" && regrown[2] == "";
}
static_assert(ResizeStringProbe(), "consteval Resize handles String-backed arrays");

// SetType with no SetValue leaves the union pointing at the new type's member; a stricter
// constexpr union read here would be ill-formed if ResetDataNull didn't run
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

// ----- ISO-8601 timestamp validation -----
static_assert(fdf::detail::IsValidTimestamp("2024-12-24"),                "date");
static_assert(fdf::detail::IsValidTimestamp("2024-02-29"),                "leap day");
static_assert(fdf::detail::IsValidTimestamp("2024-359"),                  "ordinal");
static_assert(fdf::detail::IsValidTimestamp("2024-W52-2"),                "week date");
static_assert(fdf::detail::IsValidTimestamp("15:30:00"),                  "time only");
static_assert(fdf::detail::IsValidTimestamp("2024-12-24T15:30:00"),       "date + time");
static_assert(fdf::detail::IsValidTimestamp("2024-12-24T15:30:00Z"),      "utc");
static_assert(fdf::detail::IsValidTimestamp("2024-12-24T15:30:00.123Z"),  "fractional + utc");
static_assert(fdf::detail::IsValidTimestamp("2024-12-24T15:30:00+05:30"), "offset");
static_assert(fdf::detail::IsValidTimestamp("15:30:00-05:00"),            "time only + offset");
static_assert(!fdf::detail::IsValidTimestamp(""),                         "empty");
static_assert(!fdf::detail::IsValidTimestamp("2024-13-01"),               "month 13");
static_assert(!fdf::detail::IsValidTimestamp("2024-00-10"),               "month 00");
static_assert(!fdf::detail::IsValidTimestamp("2024-12-32"),               "day 32");
static_assert(!fdf::detail::IsValidTimestamp("2023-02-29"),               "non-leap feb 29");
static_assert(!fdf::detail::IsValidTimestamp("2024-367"),                 "ordinal 367");
static_assert(!fdf::detail::IsValidTimestamp("2024-W54-1"),               "week 54");
static_assert(!fdf::detail::IsValidTimestamp("2024-W52-8"),               "weekday 8");
static_assert(!fdf::detail::IsValidTimestamp("25:00:00"),                 "hour 25");
static_assert(!fdf::detail::IsValidTimestamp("15:60:00"),                 "minute 60");
static_assert(!fdf::detail::IsValidTimestamp("15:30:99"),                 "second 99");
static_assert(!fdf::detail::IsValidTimestamp("2024-12-24T15:30:00+25:00"),"offset hour 25");

// ----- Timestamp struct: decode, epoch conversion, normalization (all consteval) -----
static_assert(fdf::Timestamp::FromText("1970-01-01T00:00:00Z").ToUnixSeconds() == 0,             "epoch zero");
static_assert(fdf::Timestamp::FromText("2001-09-09T01:46:40Z").ToUnixSeconds() == 1'000'000'000, "famous epoch");
static_assert(fdf::Timestamp::FromUnixSeconds(1'700'000'000).ToUnixSeconds() == 1'700'000'000,   "epoch round-trip");
static_assert(fdf::Timestamp::FromUnixSeconds(-86400).ToUnixSeconds() == -86400,                  "pre-1970 round-trip");
static_assert(fdf::Timestamp::FromText("2024-359").month == 12 && fdf::Timestamp::FromText("2024-359").day == 24,   "ordinal -> calendar");
static_assert(fdf::Timestamp::FromText("2024-W52-2").month == 12 && fdf::Timestamp::FromText("2024-W52-2").day == 24, "week -> calendar");
static_assert(fdf::Timestamp::FromText("15:30:00.5").nanosecond == 500'000'000,                   "fractional scaled to nanos");
static_assert(!fdf::Timestamp::FromText("2024-13-01").IsValid(),                                  "invalid month -> not valid");
static_assert(fdf::Timestamp::FromText("2024-01-01T12:00:00+05:00").ToUnixSeconds()
            == fdf::Timestamp::FromText("2024-01-01T07:00:00Z").ToUnixSeconds(),                   "offset normalizes to UTC");

// Inject a Timestamp into an entry and read it back at compile time
consteval bool TimestampInjectProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    fdf::Entry* e = root->Emplace("t");
    if(!e)
        return false;
    e->SetValue(fdf::Timestamp::FromUnixSeconds(1'000'000'000));
    if(e->GetType() != fdf::Type::Timestamp || FirstString(*e) != "2001-09-09T01:46:40Z")
        return false;
    fdf::Timestamp got = e->GetValue<fdf::Timestamp>();
    return got.IsValid() && got.year == 2001 && got.month == 9 && got.day == 9 && got.ToUnixSeconds() == 1'000'000'000;
}
static_assert(TimestampInjectProbe(), "consteval timestamp inject + read back");

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
    const std::span<const fdf::String> hp = h->GetValue<fdf::String>();
    const std::span<const fdf::String> tp = t->GetValue<fdf::String>();
    return hp.size() == 2 && hp[0] == "0xFF" && hp[1] == "0xAA"
        && tp.size() == 2 && tp[0] == "2024-12-24" && tp[1] == "15:30:00";
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

// The std edit + read surface must fold at constant evaluation, terminator maintained throughout
consteval bool StringApiProbe()
{
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

// Self-aliasing edits: constant evaluation rejects reads of freed memory, so a growing
// self-append/assign that frees the source block before copying fails to compile on the bug
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

consteval bool HexTimestampStringViewProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("h = 0xFF5733\nt = 2024-12-24T15:30:00\n");
    if(!root)
        return false;
    const fdf::Entry* h = root->GetDirectChild("h");
    const fdf::Entry* t = root->GetDirectChild("t");
    return h && h->GetType() == fdf::Type::Hex && FirstString(*h) == "0xFF5733"
        && t && t->GetType() == fdf::Type::Timestamp && FirstString(*t) == "2024-12-24T15:30:00";
}
static_assert(HexTimestampStringViewProbe(), "consteval hex/timestamp String[1] round-trip");

// Every scalar type parsed at compile time. One self-contained probe per type so a failure points
// at the exact case. Each verifies the value parses to a single leaf child of the expected type
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
    return e && e->GetType() == fdf::Type::UInt && e->GetValue<uint64_t>()[0] == 18446744073709551615ull;
}
consteval bool FloatProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 3.5\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    return e && e->GetType() == fdf::Type::Float && e->GetValue<double>()[0] > 3.4 && e->GetValue<double>()[0] < 3.6;
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
    return e && e->GetType() == fdf::Type::Timestamp && FirstString(*e) == "2024-12-24T15:30:00Z";
}

static_assert(IntProbe(),       "consteval int parse");
static_assert(UIntProbe(),      "consteval uint (max u64) parse");
static_assert(FloatProbe(),     "consteval float parse");
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
    return third && third->GetValue<int64_t>()[0] == 30;
}
static_assert(ParseContainerProbe(), "consteval map + array parse");

// String escape decoding (\t -> tab, \\ -> backslash, \" -> quote) at compile time
consteval bool EscapeProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = \"a\\tb\\\\c\\\"d\"\n");
    if(!root)
        return false;
    const fdf::Entry* v = root->GetDirectChild("value");
    return v && FirstString(*v) == "a\tb\\c\"d";
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

    std::string out;
    fdf::WriteBuffer<fdf::Style{}>(*root, out);

    return ContainsAt(out, "i=-42")
        && ContainsAt(out, "u=18446744073709551615")
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

    std::string out;
    fdf::WriteBuffer<fdf::Style{}>(*root, out);
    return ContainsAt(out, "res=1920|1080") && ContainsAt(out, "pos=1.0|2.5|3.0")
        && ContainsAt(out, "who=\"ann\"|\"bo\"") && ContainsAt(out, "ver=1.2.3|4.5.6.0");
}
static_assert(WriteCompositeProbe(), "consteval write numeric/string/version packs");

// SetValue/GetValue round-trips exercised in a constant-evaluated context. Mirrors
// ValueTest; covers the consteval storage path the runtime tests cannot reach
consteval bool ValueRoundTripProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();

    fdf::Entry* i = root->Emplace("i");
    i->SetValue(static_cast<int64_t>(-7));
    if(i->GetType() != fdf::Type::Int || i->GetValue<int64_t>()[0] != -7)
        return false;

    fdf::Entry* u = root->Emplace("u");
    u->SetValue(static_cast<uint64_t>(42));
    if(u->GetType() != fdf::Type::UInt || u->GetValue<uint64_t>()[0] != 42u)
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

    std::string out;
    fdf::WriteBuffer<fdf::Style{}>(*root, out);
    return ContainsAt(out, "arr") && ContainsAt(out, "10") && ContainsAt(out, "20")
        && ContainsAt(out, "m") && ContainsAt(out, "k=true");
}
static_assert(WriteContainerProbe(), "consteval write containers");

// Full round-trip at compile time: build -> write -> parse -> verify values survive
consteval bool WriteRoundTripProbe()
{
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    root->Emplace("x")->SetValue(static_cast<int64_t>(123));
    root->Emplace("y")->SetValue("hello");
    fdf::Entry* arr = root->Emplace("arr");
    arr->SetValue(fdf::ArrayType{});
    arr->Emplace("")->SetValue(static_cast<int64_t>(7));
    arr->Emplace("")->SetValue(static_cast<int64_t>(8));

    std::string out;
    fdf::WriteBuffer<fdf::Style{}>(*root, out);

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
#endif




int main()
{
    using namespace fdf::detail;

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
    std::print("Running suite -- Found {} files\n{}", filesToTest.size(), separator);

    RunCase("ParseTest",           Test::ParseTest);
    RunCase("ReadTest",            Test::ReadTest);
    RunCase("WriteTest",           Test::WriteTest);
    RunCase("ValueTest",           Test::ValueTest);
    RunCase("MutateTest",          Test::MutateTest);
    RunCase("RecoveryTest",        Test::RecoveryTest);
    RunCase("NegativeTest",        Test::NegativeTest);
    RunCase("PackTest",            Test::PackTest);
    RunCase("StringRoundTripTest", Test::StringRoundTripTest);
    RunCase("StringStorageTest",   Test::StringStorageTest);
    RunCase("StringApiTest",       Test::StringApiTest);
    RunCase("FloatRoundTripTest",  Test::FloatRoundTripTest);
    RunCase("TimestampTest",       Test::TimestampTest);
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
