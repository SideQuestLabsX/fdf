
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
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

        "CurlyBraceOpen  ",
        "CurlyBraceClose ",
        "SquareBraceOpen ",
        "SquareBraceClose",

        "Identifier      ",

        "Keyword         ",
        "IntLiteral      ",
        "FloatLiteral    ",
        "StringLiteral   ",
        "HexLiteral      ",
        "VersionLiteral  ",
        "TimestampLiteral"
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

namespace fdf::test
{

    inline bool ReportCheck(bool bCond, const char* expr, const char* file, int line, std::string_view msg = {}) noexcept
    {
        g_checks++;
        if(bCond)
            return true;

        g_failed++;
        if(msg.empty())
            std::println("  [FAIL] {}:{}  {}", file, line, expr);
        else
            std::println("  [FAIL] {}:{}  {}  --  {}", file, line, expr, msg);
        return false;
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
                addToBuffer(std::format("{:<{}}Type={}--Size={:03}--Name={:<20}--Value={:<50}--Comment={}", "", 4 * entry.CalculateDepth(), ENTRY_TYPE_TO_STRING[static_cast<size_t>(entry.GetType())], entry.GetChildCount(), entry.GetFullIdentifier(), entry.DataToView(temp), entry.GetComment()));
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




        // Asserts concrete values from examples/example.fdf
        static void ReadTest()
        {
            UniqueEntryPtr e = ParseFile(std::filesystem::path(filesToTest[0].inputFile));
            CHECK_MSG(static_cast<bool>(e), "Failed to parse the design file");
            if(!e)
                return;

            // Structural smoke check, tied to examples/example.fdf (update when editing it)
            CHECK_MSG(e->GetChildCountRecursive() == 125, std::format("recursive count = {}", e->GetChildCountRecursive()));
            CHECK_MSG(e->GetChildCount() == 47, std::format("top level count = {}", e->GetChildCount()));

            if(Entry* entry = e->GetChild("appVersion"); CHECK(entry && entry->GetType() == Type::Version))
            {
                auto val = entry->GetValue<uint64_t>();
                CHECK(val.size() == 4 && val[0] == 1 && val[1] == 0 && val[2] == 0 && val[3] == 0);
            }

            if(Entry* entry = e->GetChild("name"); CHECK(entry && entry->GetType() == Type::String))
                CHECK(entry->GetValue<std::string_view>() == "MyGame");

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
                CHECK(entry->GetValue<std::string_view>() == "a123-xyz");

            if(Entry* entry = e->GetChild("pi"); CHECK(entry && entry->GetType() == Type::Float))
            {
                auto val = entry->GetValue<double>();
                CHECK(val.size() == 1 && val[0] > 3.13 && val[0] < 3.15);
            }

            if(Entry* entry = e->GetChild("value"); CHECK(entry))
                CHECK(entry->GetType() == Type::Null);

            if(Entry* entry = e->GetChild("value2"); CHECK(entry))
                CHECK(entry->GetType() == Type::Null);

            if(Entry* entry = e->GetChild("gameSettings1.resolution"); CHECK(entry && entry->GetType() == Type::Int))
            {
                auto val = entry->GetValue<int64_t>();
                CHECK(val.size() == 2 && val[0] == 1920 && val[1] == 1080);
            }

            CHECK(e->GetChild("NON_EXISTING") == nullptr);

            // Escape handling: \t -> tab, unknown \p kept literally
            if(Entry* entry = e->GetChild("escaped5"); CHECK(entry && entry->GetType() == Type::String))
                CHECK(entry->GetValue<std::string_view>() == "asd\tasd\\p");

            // doubled backslash collapses to one
            if(Entry* entry = e->GetChild("escaped6"); CHECK(entry && entry->GetType() == Type::String))
                CHECK(entry->GetValue<std::string_view>() == "\\asd\\");
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
                    CHECK(e->GetValue<std::string_view>() == "Test");
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
                CHECK(e->GetType() == Type::String && e->GetValue<std::string_view>() == "hello");
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
                CHECK(e->GetType() == Type::String && e->GetValue<std::string_view>() == "now a string");
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

            // Resize numeric scalar arrays: existing elements preserved, new ones zero-filled
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
            }

            if(Entry* e = root->Emplace("flags"); CHECK(e))
            {
                e->SetValue(true);
                e->Resize(3);
                auto v = e->GetValue<bool>();
                CHECK(v.size() == 3 && v[0] == true && v[1] == false && v[2] == false);
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
                { DiagnosticType::InvalidNumber,       "ts = 0xGG#\nok = 1\n",                               false },  // malformed value, recoverable
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
                    CHECK(e->GetValue<std::string_view>() == ts);
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




        // Multi-dim numbers across the matrix: Int/UInt/Float, 2..N dims, signs in every position,
        // widening, and malformed-input recovery
        static void MultiDimTest()
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

            // Int: dimensions 2..5, negatives in leading / middle / trailing / all positions
            checkInt("1x2",         { 1, 2 });
            checkInt("1x2x3",       { 1, 2, 3 });
            checkInt("1x2x3x4",     { 1, 2, 3, 4 });
            checkInt("1x2x3x4x5",   { 1, 2, 3, 4, 5 });
            checkInt("-1x2",        { -1, 2 });
            checkInt("1x-2",        { 1, -2 });
            checkInt("1x2x-3",      { 1, 2, -3 });
            checkInt("-1x-2x-3",    { -1, -2, -3 });

            // no fixed dimension cap
            checkInt("1x2x3x4x5x6", { 1, 2, 3, 4, 5, 6 });

            // float, incl exponents and mixed signs
            checkFloat("1.0x2.0",             { 1.0, 2.0 });
            checkFloat("0.5x-0.5x1.0",        { 0.5, -0.5, 1.0 });
            checkFloat("-1.5x2.5x-3.5x4.5",   { -1.5, 2.5, -3.5, 4.5 });
            checkFloat("1.5e3x-2.5",          { 1.5e3, -2.5 });

            // one float component widens the whole vector to float
            checkFloat("1x2.0",       { 1.0, 2.0 });
            checkFloat("1x2.5x3",     { 1.0, 2.5, 3.0 });
            checkFloat("-1x2.5",      { -1.0, 2.5 });
            checkFloat("1x-2.5x3",    { 1.0, -2.5, 3.0 });

            // UInt component beyond INT64_MAX keeps the whole vector unsigned
            if(UniqueEntryPtr root = ParseBuffer(std::string("v = 18446744073709551615x1\n")))
            {
                Entry* e = root->GetChild("v");
                if(CHECK(e && e->GetType() == Type::UInt))
                {
                    auto s = e->GetValue<uint64_t>();
                    CHECK(s.size() == 2 && s[0] == 18446744073709551615ull && s[1] == 1);
                }
            }

            // malformed multi-dim values are rejected but recoverable: bad entry skipped, sibling
            // survives. also the heap-corruption regression, a mid-parse failure must not leave a
            // Map-typed entry pointing at an int buffer
            constexpr std::string_view recover[] =
            {
                "bad = 1x99999999999999999999999\nok = 7\n",   // component overflows u64
                "bad = -1x99999999999999999999999\nok = 7\n",  // negative then unsigned-overflow
                "bad = 1..0\nok = 7\n",                         // empty version component
                "bad = -x1\nok = 7\n",                          // empty leading component
                "bad = 1x\nok = 7\n",                           // empty trailing component
                "bad = 1xx2\nok = 7\n",                         // empty middle component
                "bad = 1.0x.\nok = 7\n",                        // empty trailing float component
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
                    CHECK_MSG(e->GetValue<std::string_view>() == want, out);
            }
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
            if(UniqueEntryPtr root = ParseBuffer(std::string("d = 1.5x2.0e3x0.001\n")))
            {
                Entry* e = root->GetChild("d");
                if(CHECK(e && e->GetType() == Type::Float))
                {
                    auto s = e->GetValue<double>();
                    CHECK(s.size() == 3 && s[0] == 1.5 && s[1] == 2.0e3 && s[2] == 0.001);
                }
            }

            // Multi-dimensional float with negative components
            if(UniqueEntryPtr root = ParseBuffer(std::string("d = 0.5x-0.5x1.0\n")))
            {
                Entry* e = root->GetChild("d");
                if(CHECK(e && e->GetType() == Type::Float))
                {
                    auto s = e->GetValue<double>();
                    CHECK(s.size() == 3 && s[0] == 0.5 && s[1] == -0.5 && s[2] == 1.0);
                }
            }

            // Negative component alongside an exponent
            if(UniqueEntryPtr root = ParseBuffer(std::string("d = 1.5e3x-2.5\n")))
            {
                Entry* e = root->GetChild("d");
                if(CHECK(e && e->GetType() == Type::Float))
                {
                    auto s = e->GetValue<double>();
                    CHECK(s.size() == 2 && s[0] == 1.5e3 && s[1] == -2.5);
                }
            }

            // A dash not immediately after 'x' is not a valid float component
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
                    CHECK(e->GetValue<std::string_view>() == "2024-12-24T15:30:00");
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
                    CHECK(e->GetValue<std::string_view>() == "2001-09-09T01:46:40Z");
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
                case Type::UInt: case Type::Version:  return SpanEqual(a.GetValue<uint64_t>(), b.GetValue<uint64_t>());
                case Type::Float:                     return SpanEqual(a.GetValue<double>(),   b.GetValue<double>());
                case Type::String: case Type::Timestamp: return a.GetValue<std::string_view>() == b.GetValue<std::string_view>();
                case Type::Hex:                       return EqualIgnoreCase(a.GetValue<std::string_view>(), b.GetValue<std::string_view>());
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
                "bmd = truexfalsextrue\n"
                "i1 = -42\n"
                "imd = -1x2x-3\n"
                "umax = 18446744073709551615\n"
                "f1 = 3.14\n"
                "fWhole = 100.0\n"
                "fNeg = -2.5\n"
                "fmd = 1.0x2.5x3.0\n"
                "s1 = \"hello\"\n"
                "sEmpty = \"\"\n"
                "sQuote = \"say \\\"hi\\\"\"\n"
                "sApos = \"it's\"\n"
                "sBack = \"a\\\\b\"\n"
                "sCtrl = \"tab\\there\"\n"
                "hexv = 0xFF00AA#\n"
                "ver3 = 1.2.3\n"
                "ver4 = 1.2.3.4\n"
                "tsDate = 2024-12-24\n"
                "tsFull = 2024-12-24T15:30:00.123Z\n"
                "tsOrd = 2024-359\n"
                "tsWeek = 2024-W52-2\n"
                "tsTime = 15:30:00\n"
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
    if(e->GetType() != fdf::Type::Timestamp || e->GetValue<std::string_view>() != "2001-09-09T01:46:40Z")
        return false;
    fdf::Timestamp got = e->GetValue<fdf::Timestamp>();
    return got.IsValid() && got.year == 2001 && got.month == 9 && got.day == 9 && got.ToUnixSeconds() == 1'000'000'000;
}
static_assert(TimestampInjectProbe(), "consteval timestamp inject + read back");

static_assert(ExtractValue<int64_t>("value = 25x50") == 25, "consteval multidim int parse");
static_assert(ExtractValue<bool>("value = truexfalsextrue") == true, "consteval multidim bool parse");
static_assert(ExtractValue<double>("value = 3.5") > 3.4 && ExtractValue<double>("value = 3.5") < 3.6, "consteval float parse");

consteval bool MultiDimNegFloatProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 0.5x-0.5x1.0\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    if(!e || e->GetType() != fdf::Type::Float)
        return false;
    auto s = e->GetValue<double>();
    return s.size() == 3 && s[0] == 0.5 && s[1] == -0.5 && s[2] == 1.0;
}
static_assert(MultiDimNegFloatProbe(), "consteval multidim negative float parse");

consteval bool MultiDimWidenProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 1x2.0x3\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    if(!e || e->GetType() != fdf::Type::Float)
        return false;
    auto s = e->GetValue<double>();
    return s.size() == 3 && s[0] == 1.0 && s[1] == 2.0 && s[2] == 3.0;
}
static_assert(MultiDimWidenProbe(), "consteval multidim int->float widening");

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
    return e && e->GetType() == fdf::Type::String && e->GetValue<std::string_view>() == "hi";
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
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 1920x1080\n");
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
    auto v = e->GetValue<uint64_t>();
    return v.size() == 3 && v[0] == 1 && v[1] == 2 && v[2] == 3;
}
consteval bool TimestampProbe()
{
    fdf::UniqueEntryPtr root = fdf::ParseBuffer("value = 2024-12-24T15:30:00Z\n");
    const fdf::Entry* e = root? root->GetDirectChild("value") : nullptr;
    return e && e->GetType() == fdf::Type::Timestamp && e->GetValue<std::string_view>() == "2024-12-24T15:30:00Z";
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
    return v && v->GetValue<std::string_view>() == "a\tb\\c\"d";
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

    std::string out;
    fdf::WriteBuffer<fdf::Style{}>(*root, out);
    return ContainsAt(out, "res=1920x1080") && ContainsAt(out, "pos=1.0x2.5x3.0");
}
static_assert(WriteCompositeProbe(), "consteval write multidim int/float");

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

    fdf::Entry* s = root->Emplace("s");
    s->SetValue("hi");
    if(s->GetType() != fdf::Type::String || s->GetValue<std::string_view>() != "hi")
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
        && y && y->GetValue<std::string_view>() == "hello"
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

    // example.fdf goes first so ReadTest can rely on filesToTest[0]
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

    std::print("Parse test -- Found {} files\n{}", filesToTest.size(), separator);
    Test::ParseTest();

    std::print("{0}\n\nRead test -- file: {1}\n{0}", separator, filesToTest[0].inputFile);
    Test::ReadTest();

    std::print("{0}\n\nWrite test -- file: {1}\n{0}", separator, FDF_OUTPUT_DIRECTORY "/WriteTest.fdf");
    Test::WriteTest();

    std::print("{0}\n\nValue test\n{0}", separator);
    Test::ValueTest();

    std::print("{0}\n\nMutate test\n{0}", separator);
    Test::MutateTest();

    std::print("{0}\n\nRecovery test\n{0}", separator);
    Test::RecoveryTest();

    std::print("{0}\n\nNegative test\n{0}", separator);
    Test::NegativeTest();

    std::print("{0}\n\nMulti-dimensional number test\n{0}", separator);
    Test::MultiDimTest();

    std::print("{0}\n\nString round-trip test\n{0}", separator);
    Test::StringRoundTripTest();

    std::print("{0}\n\nFloat round-trip test\n{0}", separator);
    Test::FloatRoundTripTest();

    std::print("{0}\n\nTimestamp test\n{0}", separator);
    Test::TimestampTest();

    std::print("{0}\n\nRound-trip test\n{0}", separator);
    Test::RoundTripTest();

#if !FDF_NO_COMMENTS
    std::print("{0}\n\nWriter comment test\n{0}", separator);
    Test::WriterCommentTest();
#endif

    std::print("{0}\nChecks: {1} run, {2} failed\n{0}", separator, fdf::test::g_checks, fdf::test::g_failed);
    return fdf::test::g_failed == 0? 0 : -1;
}
