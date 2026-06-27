
#if FDF_USE_CPP_MODULES
    import std;
    import std.compat;
    import fdf;
#else
    #include "fdf.h"
    #include <iostream>
    #include <print>
#endif




namespace fdf::detail
{
    constexpr std::string_view TOKEN_TYPE_TO_STRING[] =
    {
        "NonExisting     ",
        "Invalid         ",
        "NewLine         ",
        "EndOfFile       ",
        "Comment         ",

        "At              ",
        "Equal           ",
        "Comma           ",
        "Colon           ",

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
        "TimestampLiteral",

        "EvaluateLiteral "
    };

    constexpr std::string_view ENTRY_TYPE_TO_STRING[] =
    {
        "Invalid  ",
        "Null     ",
        
        "Bool     ",
        "Int      ",
        "UInt     ",
        "Float    ",
        
        "String   ",
        "Hex      ",
        "Version  ",
        "Timestamp",
        
        "Array    ",
        "Map      "
    };

    struct TestDirectories
    {
        TestDirectories(const std::filesystem::path& file)
        {
            inputFile = file.generic_string();
            outputFile = FDF_TEST_DIRECTORY "/output/" + file.stem().generic_string();

            tokenizedFile = outputFile + "-Tokenized.txt";
            entriesFile = outputFile + "-Entries.txt";
            outputFile = outputFile + "-Output.txt";
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
            #if !FDF_NO_COMMENTS
                addToBuffer(std::format("{:<{}}Type={}--Size={:03}--Name={:<20}--Value={:<50}--Comment={}", "", 4 * entry.CalculateDepth(), ENTRY_TYPE_TO_STRING[static_cast<size_t>(entry.type)], entry.size, entry.GetFullIdentifier(), entry.DataToView(temp), entry.comment));
            #else
                addToBuffer(std::format("{:<{}}Type={}--Size={:03}--Name={:<20}--Value={:<50}", "", 4 * entry.CalculateDepth(), ENTRY_TYPE_TO_STRING[static_cast<size_t>(entry.type)], entry.size, entry.GetFullIdentifier(), entry.DataToView(temp)));
            #endif
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

        


        static bool ParseTest()
        {
            bool bResult = true;
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
                
                if(e)
                {
                    PrintAllEntries(e.get(), directories.entriesFile);
                    PrintFile<Style{.bVariableCommas = false}>(e.get(), directories.outputFile);
                }
                else
                    bResult = false;
            }

            return bResult;
        }




        // We intentionally print each one via a "Entry::GetValue" instead of "Entry::DataToView" so we can test more of the code
        // TODO: Maybe automate ReadTest so we don't need to implement each Entry by hand? (and manually adjust formatting (currently 24))
        static bool ReadTest()
        {
            UniqueEntryPtr e = ParseFile(std::filesystem::path(filesToTest[0].inputFile));
            if(!e)
            {
                std::puts("[ERROR]: Failed to parse the design file... Should never happen unless initial parse failed too!");
                return false;
            }


            bool bResult = true;


            {
                std::println("          Entry Count: {:>3} (should be 135) (update when editing design file)", e->GetChildCountRecursive());
                std::println("Top Level Entry Count: {:>3} (should be  56) (update when editing design file)", e->GetChildCount());

                if(e->GetChildCountRecursive() != 135 || e->GetChildCount() != 56)
                {
                    bResult = false;
                    std::puts("[ERROR]: Invalid 'Entry Count' or 'Top Level Entry Count'");
                }

                std::println();
            }


            {
                Entry* entry = e->GetChild("appVersion");
                if(entry && entry->type == Type::Version)
                {
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    auto val = entry->GetValue<uint64_t>();
                    std::println("{}.{}.{}.{}", val[0], val[1], val[2], val[3]);
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "appVersion");
                    std::puts("<ERROR>");
                }
            }


            {
                Entry* entry = e->GetChild("name");
                if(entry && entry->type == Type::String)
                {
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    auto val = entry->GetValue<std::string_view>();
                    std::println("{}", val);
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "name");
                    std::puts("<ERROR>");
                }
            }


            {
                Entry* entry = e->GetChild("enabled1");
                if(entry && entry->type == Type::Bool && entry->size == 1)
                {
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    auto val = entry->GetValue<bool>();
                    std::println("{}", val[0]);
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "enabled1");
                    std::puts("<ERROR>");
                }
            }


            {
                Entry* entry = e->GetChild("id");
                if(entry && entry->type == Type::Int)
                {
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    auto val = entry->GetValue<int>();
                    std::println("{}", val[0]);
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "id");
                    std::puts("<ERROR>");
                }
            }


            {
                Entry* entry = e->GetChild("uuid");
                if(entry && entry->type == Type::String)
                {
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    auto val = entry->GetValue<char>();
                    for(char c : val)
                    {
                        if(c == '\n')
                            std::cout << "\\n";
                        else
                            std::cout << c;
                    }
                    std::println();
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "uuid");
                    std::puts("<ERROR>");
                }
            }


            {
                Entry* entry = e->GetChild("pi");
                if(entry && entry->type == Type::Float)
                {
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    auto val = entry->GetValue<float>();
                    std::println("{}", val[0]);
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "pi");
                    std::puts("<ERROR>");
                }
            }


            {
                Entry* entry = e->GetChild("value");
                if(entry && entry->type == Type::Null)
                {
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    std::puts("null");
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "value");
                    std::puts("<ERROR>");
                }
            }


            {
                Entry* entry = e->GetChild("value2");
                if(entry && entry->type == Type::Null)
                {
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    std::puts("null");
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "value2");
                    std::puts("<ERROR>");
                }
            }


            {
                Entry* entry = e->GetChild("gameSettings1.resolution");
                if(entry && entry->type == Type::Int && entry->size == 2)
                {
                    std::print("{:<32}  ->  ", entry->GetFullIdentifier());
                    auto val = entry->GetValue<int64_t>();
                    std::println("{}x{}", val[0], val[1]);
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "gameSettings1.resolution");
                    std::puts("<ERROR>");
                }
            }


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


            {
                Entry* entry = e->GetChild("escaped1");
                if(entry && entry->type == Type::String)
                {
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    auto val = entry->GetValue<std::string_view>();
                    std::println("{}", val);
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "escaped1");
                    std::puts("<ERROR>");
                }
            }


            {
                Entry* entry = e->GetChild("escaped2");
                if(entry && entry->type == Type::String)
                {
                    Timestamp ts = e->GetValue<Timestamp>();
                    CHECK(ts.IsValid() && ts.bHasDate && ts.bHasTime);
                    CHECK(ts.year == 2024 && ts.month == 12 && ts.day == 24);
                    CHECK(ts.hour == 15 && ts.minute == 30 && ts.second == 0);
                    CHECK(ts.nanosecond == 123'000'000 && ts.fracDigits == 3);
                    CHECK(ts.tzKind == Timestamp::TzKind::Utc);
                }
            }


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


            {
                Entry* entry = e->GetChild("escaped6");
                if(entry && entry->type == Type::String)
                {
                    e->SetValue(Timestamp::DateTime(2024, 12, 24, 15, 30, 0));
                    CHECK(e->GetType() == Type::Timestamp);
                    CHECK(e->GetValue<std::string_view>() == "2024-12-24T15:30:00");
                    Timestamp got = e->GetValue<Timestamp>();
                    CHECK(got.year == 2024 && got.month == 12 && got.day == 24 && got.hour == 15);
                }
                else
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




        static bool WriteTest()
        {
            UniqueEntryPtr root = NewEntry();
            if(!root)
                return false;
            bool bResult = true;

            {
                if(Entry* e = root->Emplace("name"))
                    e->SetValue("Test");
                else
                    bResult = false;
            }

            {
                if(Entry* e = root->Emplace("pi"))
                    e->SetValue(3.14);
                else
                    bResult = false;
            }

            {
                if(Entry* e = root->Emplace("position"))
                {
                    double position[3] = {0.0, 0.0, 100.0};
                    e->SetValue(std::span(position, 3));
                }
                else
                    bResult = false;
            }
            
            {
                if(Entry* e = root->Emplace("results"))
                {
                    e->SetValue(ArrayType());
                    
                    if(Entry* ee = e->Emplace(""))
                    {
                        ee->SetValue(42);
                    }
                    else
                        bResult = false;
                    
                    if(Entry* ee = e->Emplace(""))
                    {
                        ee->SetValue(0.75f);
                    }
                    else
                        bResult = false;
                    
                    if(Entry* ee = e->Emplace(""))
                    {
                        ee->SetValue(false);
                    }
                    else
                        bResult = false;
                    
                    if(Entry* ee = e->Emplace(""))
                    {
                        ee->SetValue("UNKNOWN!");
                    }
                    else
                        bResult = false;
                    
                    if(Entry* ee = e->Emplace(""))
                    {
                        ee->SetValue(MapType());
                        
                        if(Entry* eee = ee->Emplace("found"))
                        {
                            eee->SetValue(true);
                        }
                        else
                            bResult = false;
                        
                        if(Entry* eee = ee->Emplace("value"))
                        {
                            eee->SetValue(815);
                        }
                        else
                            bResult = false;
                    }
                    else
                        bResult = false;
                }
                else
                    bResult = false;
            }

            bResult = PrintFile<Style{.singleLineContainerLimit = 40, .bTrailingCommas = false}>(root.get(), FDF_TEST_DIRECTORY "/output/WriteTest.txt") && bResult;
            return bResult;
        }
    };
}




int main()
{
    using namespace fdf::detail;
    
    std::string buffff;
    fdf::UniqueEntryPtr root = fdf::NewEntry();
    (void)root->SetIdentifier("test");
    root->SetValue(15);
    root->SetComment("Commm");
    [[maybe_unused]] auto qqqq = root->GetComment();
    WriteBuffer(*root, buffff);

    std::filesystem::path currentDesignFile = FDF_ROOT_DIRECTORY "/designs/Design_5.txt";
    std::filesystem::path testDir = FDF_TEST_DIRECTORY;
    std::filesystem::path outputDir = FDF_TEST_DIRECTORY "/output";

    if(!std::filesystem::exists(outputDir))
        std::filesystem::create_directory(outputDir);

    if(std::filesystem::exists(currentDesignFile))
        filesToTest.emplace_back(std::move(currentDesignFile));
    
    for(const auto& entry : std::filesystem::directory_iterator(testDir))
    {
        if(entry.is_regular_file() && entry.path().stem() != "CMakeLists" && (entry.path().extension() == ".txt" || entry.path().extension() == ".fdf"))
        {
            size_t length = filesToTest.emplace_back(entry.path()).inputFile.size();
            if(length > longestFilename)
                longestFilename = length;
        }
    }


    constexpr std::string_view separator = "--------------------------------------------------\n";
    bool bResult = true;

    std::print("Parse test -- Found {} files\n{}", filesToTest.size(), separator);
    bool bTempResult = Test::ParseTest();
    bResult = bTempResult && bResult;
    std::print("{1}RESULT: {2}\n{1}\n\n\nRead test -- file: {0}\n{1}", filesToTest[0].inputFile, separator, bTempResult);
    bTempResult = Test::ReadTest();
    bResult = bTempResult && bResult;
    std::print("{1}RESULT: {2}\n{1}\n\n\nWrite test -- file: {0}\n{1}", FDF_TEST_DIRECTORY "/output/WriteTest.txt", separator, bTempResult);
    bTempResult = Test::WriteTest();
    bResult = bTempResult && bResult;
    std::print("RESULT: {}\n{}\n\n\n", bTempResult, separator);

        // Shortest round-trip floats: write(x) then reparse must reproduce x bitwise, for every
        // finite double. Covers the hand-rolled Dragon4 writer + AlgorithmM parser, both used at
        // compile time and runtime. Also checks scientific/exponent input forms
        static void FloatRoundTripTest()
        {
            auto roundTrip = [](double x) -> bool
    return bResult? 0 : -1;
}


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

consteval size_t ExtractCommentSize(auto buffer)
{
    fdf::UniqueEntryPtr eRoot = fdf::ParseBuffer(buffer);
    if(!eRoot)
        return 0;
    if(auto* eValue = eRoot->GetDirectChild("value"))
    {
        return eValue->GetComment().size();
    }
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


//[[maybe_unused]] constexpr auto value0 = ExtractValue<int64_t>("value = 25x50");
//[[maybe_unused]] constexpr auto value1 = ExtractValue<bool>("value = truexfalsextrue");
//constexpr char str[] = "//TestComment\nvalue = 0";
//[[maybe_unused]] constexpr auto comment = ExtractComment<str>();


/*int main()
{
    std::string temp;
    temp.reserve(1024);
    
    if(fdf::UniqueEntryPtr e = fdf::ParseBuffer("category{ name = 'test' }"))
    {
        (void)e->ParseCombineBuffer("pi = 3.14");
        (void)e->ParseCombineBuffer("pi = 3.2");
        e->ForEach<fdf::ForEachFlags::Recursive | fdf::ForEachFlags::IncludeSelf>([&temp](fdf::Entry& myEntry)
        {
            std::print("name: {}   -   depth: {}   -   data: {}\n", myEntry.GetIdentifier(), myEntry.CalculateDepth(), myEntry.DataToView(temp));
        });
        std::puts("\n\n");
        
        if(fdf::UniqueEntryPtr ee = fdf::ParseBuffer("pi = 3.3"))
        {
            if(e->Combine(ee))
            {
                e->ForEach<fdf::ForEachFlags::Recursive | fdf::ForEachFlags::IncludeSelf>([&temp](fdf::Entry& myEntry)
                {
                    std::print("name: {}   -   depth: {}   -   data: {}\n", myEntry.GetIdentifier(), myEntry.CalculateDepth(), myEntry.DataToView(temp));
                });
                std::puts("\n\n");
            }
        }
    }
    
    //TODO try to recover as much as possible when it comes to failures, but emit a warning for each failure
    
    if(fdf::UniqueEntryPtr e = fdf::ParseFile(FDF_ROOT_DIRECTORY "/designs/Design_5.txt"))
    {
        e->ForEach<fdf::ForEachFlags::Recursive | fdf::ForEachFlags::IncludeSelf | fdf::ForEachFlags::Group>([&temp](fdf::Entry& myEntry)
        {
            std::print("name: {}   -   depth: {}   -   data: {}\n", myEntry.GetIdentifier(), myEntry.CalculateDepth(), myEntry.DataToView(temp));
        });
        [[maybe_unused]] auto* id = e->GetDirectChild("id");
        [[maybe_unused]] size_t count = e->GetChildCountRecursive();
        std::cout << "comment: " << e->GetComment() << '\n';
        temp.clear();
        e->SetIdentifier("TestTest");
        fdf::WriteBuffer(*e, temp);
    }
    std::puts("\n\n");
}*/


        // GetValue<Timestamp> decodes the raw text; SetValue(Timestamp) injects it back as canonical
        // ISO text. Covers field extraction, epoch conversion, ordinal/week normalization, and inject
        static void TimestampTest()
        {
            // Field extraction from a fully-specified value
            // Epoch extraction: the famous Unix 1e9 instant, plus nanos and millis
            {
                Timestamp ts = ParseBuffer("t = 2001-09-09T01:46:40Z\n")->GetChild("t")->GetValue<Timestamp>();
                CHECK(ts.ToUnixSeconds() == 1'000'000'000);
                CHECK(ts.ToUnixMillis() == 1'000'000'000'000);
                CHECK(ts.ToUnixNanos()  == 1'000'000'000'000'000'000);
            }
            // Time-only: no date present
            {
                Timestamp ts = ParseBuffer("t = 15:30:00\n")->GetChild("t")->GetValue<Timestamp>();
                CHECK(ts.IsValid() && ts.bHasTime && !ts.bHasDate);
                CHECK(ts.hour == 15 && ts.minute == 30);
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
