
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
                Entry* entry = e->GetChild("NON_EXISTING");
                std::print("{:<32}  ->  ", "NON_EXISTING");
                if(!entry)
                {
                    std::puts("<NON_EXISTING>");
                }
                else
                {
                    bResult = false;
                    std::puts("<ERROR>");
                }
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
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    auto val = entry->GetValue<std::string_view>();
                    std::println("{}", val);
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "escaped2");
                    std::puts("<ERROR>");
                }
            }


            {
                Entry* entry = e->GetChild("escaped5");
                if(entry && entry->type == Type::String)
                {
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    auto val = entry->GetValue<std::string_view>();
                    std::println("{}", val);
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "escaped5");
                    std::puts("<ERROR>");
                }
            }


            {
                Entry* entry = e->GetChild("escaped6");
                if(entry && entry->type == Type::String)
                {
                    std::print("{:<32}  ->  ", entry->GetIdentifier());
                    auto val = entry->GetValue<std::string_view>();
                    std::println("{}", val);
                }
                else
                {
                    bResult = false;
                    std::print("{:<32}  ->  ", "escaped6");
                    std::puts("<ERROR>");
                }
            }

            
            return bResult;
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

            bResult = PrintFile<Style{.singleLineContainerLimit = 80, .bTrailingCommas = false}>(root.get(), FDF_TEST_DIRECTORY "/output/WriteTest.txt") && bResult;
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
