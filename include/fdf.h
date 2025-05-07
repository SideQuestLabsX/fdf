
#pragma once

#if !FDF_USE_CPP_MODULES
    #include <cstdint>
    #include <type_traits>
    #include <string>
    #include <vector>
    #include <format>
    #include <filesystem>
    #include <fstream>
    #include <cctype>
    #include <span>
    #include <utility>
    #include <algorithm>
    #include <ranges>

    #define FDF_EXPORT
#endif




#define FDF_CHECK_TOKEN(TOKEN)         do { if(TOKEN.type == TokenType::Invalid  ) return false; } while (false)
#define FDF_CHECK_TOKEN_FOR_EOF(TOKEN) do { if(TOKEN.type == TokenType::EndOfFile) return false; } while (false)
#define FDF_FORWARD_ERROR(Cond)        do { if(!(Cond))                            return false; } while (false)




FDF_EXPORT namespace fdf
{
    enum class Type : uint8_t
    {
        Invalid,
        Null,
        Nil = Null,

        Bool,
        Int,
        UInt,
        Float,

        String,
        Hex,
        Version,
        Timestamp,

        Array,
        Map,
        TypedArray
    };


    struct Style
    {
        // Spacing
        bool bUseSpacesOverTabs = true;
        uint8_t tabSize = 4;
        bool bSpaceAfterComma = true;
        bool bSpaceWithinParentheses = true;
        bool bSpaceBeforeAndAfterEqualSign = false;
        bool bParenthesesOnNewLine = true;
        bool bEmptyLineAtEOF = true;

        // Comment
        bool bFileComment = true;
        bool bEntryComment = true;       // TODO: implement
        bool bAlignCloseComments = true; // TODO: implement

        // Single-line limits
        uint8_t singleLineCommentLimit = 80;  // Characters
        uint8_t singleLineArrayLimit = 5;     // Entry
        uint8_t singleLineMapLimit = 5;       // Entry

        // Array and Map
        bool bCommasOnArrays = true;
        bool bCommasOnMaps = true;
        bool bCommasOnLastElement = true;
        bool bUseEqualSignForSingleLineArraysAndMaps = false;

        // General
        bool bGroupSimilarTypes = true;
        bool bUppercaseHex = true;
        bool bUseNilInsteadOfNull = false;
        bool bAlwaysUseDoubleQuoteForStrings = false;
    };




    enum class CommentCombineStrategy : uint8_t
    {
        UseExisting,
        UseNew,
        UseNewIfExistingIsEmpty,
        Merge,
        Clear
    };

    enum class Diagnostic : uint8_t
    {
        Warning_Begin,
        AlreadyHasComment = Warning_Begin,
        Warning_End = AlreadyHasComment,

        Error_Begin,
        UnexpectedToken = Error_Begin,
        Error_End = UnexpectedToken,
    };

    constexpr bool IsWarning(Diagnostic type) noexcept
    {
        return static_cast<uint8_t>(type) >= static_cast<uint8_t>(Diagnostic::Warning_Begin) &&
               static_cast<uint8_t>(type) <= static_cast<uint8_t>(Diagnostic::Warning_End);
    }
    constexpr bool IsError(Diagnostic type) noexcept
    {
        return static_cast<uint8_t>(type) >= static_cast<uint8_t>(Diagnostic::Error_Begin) &&
               static_cast<uint8_t>(type) <= static_cast<uint8_t>(Diagnostic::Error_End);
    }

    class Entry;
}




namespace fdf::detail
{
    constexpr std::string_view EVALUATE_LITERAL_TEXT = "Evaluate Literal";
    constexpr std::string_view INVALID_TEXT = "<INVALID>";
    constexpr std::string_view ARRAY_TEXT   = "<ARRAY>";
    constexpr std::string_view MAP_TEXT     = "<MAP>";

    constexpr uint64_t  INT64_MAX_VALUE = std::numeric_limits< int64_t>::max();
    constexpr uint64_t UINT64_MAX_VALUE = std::numeric_limits<uint64_t>::max();
    constexpr double   DOUBLE_MAX_VALUE = std::numeric_limits<  double>::max();

    constexpr size_t VARIANT_SIZE = 5 * sizeof(int64_t);  // 5x 64 bit value
    constexpr size_t VARIANT_32BIT_ELEMENT_COUNT = VARIANT_SIZE / sizeof(int32_t);
    constexpr size_t VARIANT_64BIT_ELEMENT_COUNT = VARIANT_SIZE / sizeof(int64_t);
    constexpr size_t VARIANT_DYNAMIC_STRING_HARD_LIMIT = (VARIANT_SIZE * 2.5);


    constexpr std::string_view KEYWORDS[] =
    {
        "null", "nil",
        "true", "false", " MD_BOOL_PLACEHOLDER "
    };
    constexpr size_t KEYWORD_COUNT = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

    enum class TokenType : uint8_t
    {
        NonExisting,  // Means the requested token doesn't exist/cannot be accessed, not necessarily an error
        Invalid,      // Means there is an error in the file content
        NewLine,
        EndOfFile,
        Comment,

        Equal,
        Comma,

        CurlyBraceOpen,
        CurlyBraceClose,
        SquareBraceOpen,
        SquareBraceClose,

        Identifier,

        Keyword,
        ValueLiteral_Begin = Keyword,
        IntLiteral,
        FloatLiteral,
        StringLiteral,
        HexLiteral,
        VersionLiteral,
        TimestampLiteral,

        EvaluateLiteral,
        ValueLiteral_End = EvaluateLiteral,
    };

    struct Token
    {
        constexpr Token() noexcept = default;
        constexpr Token(TokenType type_, uint32_t startPosition_ = 0, size_t count_ = 0)
            : type(type_), count(count_), startPosition(startPosition_)  { }

        constexpr std::string_view ToView(std::string_view buffer) const noexcept  { return buffer.substr(startPosition, count); }

        TokenType type = TokenType::NonExisting;
        uint8_t  extra8  = 0; // Token specific data, if needed
        uint16_t extra16 = 0; // Token specific data, if needed
        uint32_t count = 0;
        size_t startPosition = 0;
        uint32_t line = 0;
        uint32_t column = 0;
    };

    struct Tokenizer
    {
        constexpr Tokenizer(std::string_view content_) noexcept
            : content(content_), index(0), line(1), lastNewLineIndex(0), currentToken(GetNextToken())  { }

        constexpr Token Current() const noexcept  { return currentToken; }
        constexpr Token Advance()       noexcept  { currentToken = GetNextToken(); return currentToken; }

    private:
        constexpr Token GetNextToken() noexcept;

    private:
        std::string_view content;
        size_t index;
        size_t line;
        size_t lastNewLineIndex;
        Token currentToken;
    };
}




namespace fdf::detail
{
    struct Test;

    template<auto DIAGNOSTIC_CALLBACK>
    struct Utils;

    template <typename Callable>
    constexpr bool IsValidDiagnosticCallback = std::is_invocable_r_v<bool, Callable, Diagnostic, std::string_view>;
    inline constexpr auto DefaultDiagnosticCallback = [](Diagnostic diagnostic, std::string_view message) -> bool  { return !IsError(diagnostic); };


    constexpr void constexpr_memcpy(char* dest, const char* src, size_t size)
    {
        for(size_t i = 0; i < size; i++)
            dest[i] = src[i];
    }

    constexpr bool IsValueLiteral(TokenType type) noexcept
    { 
        return static_cast<uint8_t>(type) >= static_cast<uint8_t>(TokenType::ValueLiteral_Begin) &&
               static_cast<uint8_t>(type) <= static_cast<uint8_t>(TokenType::ValueLiteral_End);
    }

    constexpr void TrimWhitespaceMultilineInPlace(std::string_view view, std::string& out)
    {
        out.clear();
        out.reserve(view.size());

        bool bAfterNewLine = true;
        for(char c : view)
        {
            if(bAfterNewLine)
            {
                if(std::isspace(c))
                    continue;

                bAfterNewLine = false;
                out.push_back(c);
            }
            else
            {
                out.push_back(c);
                if(c == '\n')
                    bAfterNewLine = true;
            }
        }
    }
    constexpr std::string TrimWhitespaceMultiline(std::string_view view)
    {
        if(view.empty())
            return {};

        std::string temp;
        TrimWhitespaceMultilineInPlace(view, temp);
        return temp;
    }
}










FDF_EXPORT namespace fdf
{
    class Entry
    {
    public:
        Type type = Type::Invalid;
        Type subType = Type::Invalid;
        uint8_t depth = 0;
        uint8_t identifierSize = 0;
        uint32_t size = 0;
        Entry* parent = nullptr;
        void* data = nullptr;
        void* comment = nullptr;
        char identifier[32] = {};
    };
}










namespace fdf::detail
{
    class EntryAllocator
    {
    public:
        struct Chunk
        {
            union U { Entry e; U* p; };
            static constexpr size_t ELEMENT_COUNT = (4096 - sizeof(void*)) / sizeof(U);

            U* freeList;
            size_t used;
            U data[ELEMENT_COUNT];


        public:
            Chunk() noexcept
                : freeList(data), used(0), data{}
            {
                U* cur = freeList;
                U* next = cur + 1;
                U* end = data + ELEMENT_COUNT;
                
                while(next < end)
                {
                    cur->p = next;
                    cur = next++;
                }

                cur->p = nullptr;
            }

            [[nodiscard]] constexpr bool Owns(Entry* ptr) noexcept
            {
                return static_cast<void*>(data) <= static_cast<void*>(ptr) && static_cast<void*>(ptr) < static_cast<void*>(data + 4096);
            }

            [[nodiscard]] constexpr bool IsEmpty()  noexcept  { return used == 0; }
            [[nodiscard]] constexpr bool HasSpace() noexcept  { return used <  ELEMENT_COUNT; }
            [[nodiscard]] constexpr U* EntryToUnion(Entry* e) noexcept  { return &data[e - &data->e]; }

            [[nodiscard]] constexpr Entry* Allocate() noexcept
            {
                Entry* allocated = &freeList->e;
                freeList = freeList->p;
                new(allocated)Entry{};
                used++;
                return allocated;
            }

            constexpr void Deallocate(Entry* ptr) noexcept
            {
                ptr->~Entry();
                EntryToUnion(ptr)->p = freeList;
                freeList = EntryToUnion(ptr);
                used--;
            }
        };

        struct ChunkVec
        {
            using T = std::unique_ptr<Chunk>;

            size_t size;
            size_t cap;
            T* data;

        public:
            ~ChunkVec()
            {
                for(size_t i = 0; i < size; ++i)
                    data[i].~T();
                ::operator delete[](data);
            }

            constexpr T* begin() noexcept  { return data; }
            constexpr T* end()   noexcept  { return data + size; }

            constexpr T& Emplace()
            {
                if(size < cap)
                    return *new(data + size++) T(std::make_unique<Chunk>());

                const size_t newCap = std::max(cap * 2ull, 4ull);
                T* newData = static_cast<T*>(::operator new[](newCap * sizeof(T)));

                for(size_t i = 0; i < size; i++)
                {
                    new(newData + i) T(std::move(data[i]));
                    data[i].~T();
                }

                ::operator delete[](data);
                data = newData;
                cap = newCap;
                return *new(data + size++) T(std::make_unique<Chunk>());
            }

            constexpr void Erase(T* chunk)
            {
                if(chunk < data || chunk > data + size)
                    throw std::runtime_error("ChunkVec::Erase element doesn't belong to ChunkVec");

                size_t i = chunk - data;
                data[i].~T();
                for(; i + 1 < size; i++)
                    new(data + i) T(std::move(data[i + 1]));
                
                --size;
                if(i != 0)
                    data[size].~T();
            }

            void Clear() noexcept
            {
                for(size_t i = 0; i < size; ++i)
                    data[i].~T();
                ::operator delete[](data);

                size = 0;
                cap = 0;
                data = nullptr;
            }
        };

        inline static constinit ChunkVec chunks = {};

    public:
        [[nodiscard]] constexpr static Entry* Allocate() noexcept
        {
            for(auto& chunk : chunks)
            {
                if(chunk->HasSpace())
                    return chunk->Allocate();
            }

            return chunks.Emplace()->Allocate();
        }

        static void Deallocate(Entry* ptr)
        {
            for(auto& chunk : chunks)
            {
                if(chunk->Owns(ptr))
                {
                    chunk->Deallocate(ptr);
                    if(chunk->IsEmpty())
                        chunks.Erase(&chunk);
                    return;
                }
            }

            throw std::runtime_error("EntryAllocator::Deallocate -- pointer doesn't belongs to EntryAllocator");
        }

        static void Clear() noexcept
        {
            chunks.Clear();
        }
    };
}
