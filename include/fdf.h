
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
        Map
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










namespace fdf::detail
{
    template<size_t BLOCK_SIZE, size_t BLOCK_ALIGNMENT = BLOCK_SIZE, size_t CHUNK_SIZE = 4096, size_t LAZILY_DEALLOCATED_CHUNK_COUNT = 1>
    class SlabAllocator
    {
        static_assert(BLOCK_SIZE >= sizeof(void*), "BLOCK_SIZE can't be smaller than size of a pointer");

        struct Chunk
        {
            union U { alignas(BLOCK_ALIGNMENT) std::byte block[BLOCK_SIZE]; U* p; };
            static constexpr size_t ELEMENT_COUNT = (CHUNK_SIZE - 3 * sizeof(void*)) / sizeof(U);

            U* freeList;
            size_t used;
            Chunk* next;
            U data[ELEMENT_COUNT];


        public:
            constexpr Chunk() noexcept
                : freeList(data), used(0), next(nullptr), data{}
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

            [[nodiscard]] constexpr bool Owns(void* ptr) noexcept
            {
                return static_cast<void*>(data) <= ptr && ptr < static_cast<void*>(data + ELEMENT_COUNT);
            }

            [[nodiscard]] constexpr bool IsEmpty()        noexcept  { return used == 0; }
            [[nodiscard]] constexpr bool HasSpace()       noexcept  { return used < ELEMENT_COUNT; }
            [[nodiscard]] constexpr U*   ToUnion(void* e) noexcept  { return static_cast<U*>(e); }

            [[nodiscard]] void* Allocate() noexcept
            {
                void* allocated = freeList->block;
                freeList = freeList->p;
                used++;
                return allocated;
            }

            void Deallocate(void* ptr) noexcept
            {
                ToUnion(ptr)->p = freeList;
                freeList = ToUnion(ptr);
                used--;
            }
        };

        Chunk* head = nullptr;
        size_t emptyChunkCount = 0;

    public:
        static constexpr size_t PER_CHUNK_CAPACITY = Chunk::ELEMENT_COUNT;

        constexpr  SlabAllocator() noexcept = default;
        constexpr ~SlabAllocator() noexcept
        {
            while(head)
            {
                Chunk* temp = head->next;
                delete head;
                head = temp;
            }
        }




        [[nodiscard]] void* Allocate()
        {
            for(Chunk* chunk = head; chunk; chunk = chunk->next)
            {
                if(chunk->HasSpace())
                {
                    if constexpr(LAZILY_DEALLOCATED_CHUNK_COUNT > 0)
                    {
                        if(chunk->IsEmpty())
                            emptyChunkCount--;
                    }

                    return chunk->Allocate();
                }
            }

            Chunk* newChunk = new Chunk();
            newChunk->next = head;
            head = newChunk;
            return newChunk->Allocate();
        }

        [[nodiscard]] bool Deallocate(void* ptr)
        {
            Chunk** nextOfPrev = &head; // next pointer of previous chunk
            for(Chunk* chunk = head; chunk; nextOfPrev = &chunk->next, chunk = chunk->next)
            {
                if(chunk->Owns(ptr))
                {
                    chunk->Deallocate(ptr);
                    if(chunk->IsEmpty())
                    {
                        if constexpr(LAZILY_DEALLOCATED_CHUNK_COUNT == 0)
                        {
                            *nextOfPrev = chunk->next;
                            delete chunk;
                        }
                        else
                        {
                            if(emptyChunkCount >= LAZILY_DEALLOCATED_CHUNK_COUNT)
                            {
                                *nextOfPrev = chunk->next;
                                delete chunk;
                            }
                            else
                                emptyChunkCount++;
                        }
                    }
                    return true;
                }
            }

            return false;
        }




        template<typename T, typename... Args>
        [[nodiscard]] T* Create(Args&&... args)
        {
            static_assert(sizeof(T) <= BLOCK_SIZE, "T is too large for this SlabAllocator.");
            return new(Allocate()) T(std::forward<Args>(args)...);
        }

        template<typename T>
        [[nodiscard]] bool Destroy(T* obj)
        {
            static_assert(sizeof(T) <= BLOCK_SIZE, "T is too large for this SlabAllocator.");
            obj->~T();
            return Deallocate(obj);
        }
    };










    // TODO: Rethink how this is handled... It's only used for bigger allocations than 64 bytes and smaller allocations than 4096 bytes (minus overhead)
    template<size_t CHUNK_SIZE = 4096, size_t LAZILY_DEALLOCATED_CHUNK_COUNT = 1>
    class DynamicAllocator
    {
        struct Chunk
        {
            static constexpr size_t MAX_MEMORY = CHUNK_SIZE - sizeof(void*) - 2 * sizeof(uint32_t);
            static constexpr uint32_t MAGIC_VALUE = 495812;
            struct Header { uint32_t nextOrMagic; uint32_t size; };

            uint32_t freeList;
            uint32_t used;
            Chunk* next;
            std::byte data[MAX_MEMORY];

        public:
            constexpr Chunk() noexcept
                : freeList(0), used(0), next(nullptr), data{}
            {
                new(data) Header{uint32_t(-1), static_cast<uint32_t>(MAX_MEMORY - sizeof(Header))};
            }

            [[nodiscard]] constexpr bool Owns(void* ptr) noexcept
            {
                return static_cast<void*>(data) <= ptr && ptr < static_cast<void*>(data + MAX_MEMORY);
            }

            [[nodiscard]] constexpr bool    IsEmpty()                    noexcept  { return used == 0; }
            [[nodiscard]] constexpr bool    HasSpace()                   noexcept  { return freeList != uint32_t(-1); }
            [[nodiscard]] constexpr Header* GetHeaderPtr(uint32_t index) noexcept  { return index < MAX_MEMORY? std::launder(reinterpret_cast<Header*>(&data[index])) : nullptr; }

            [[nodiscard]] constexpr void* Allocate(uint32_t size, uint32_t alignment = 8) noexcept
            {
                if(alignment < alignof(Header))
                    alignment = alignof(Header);

                if(size % alignment != 0)
                    size = (((size / alignment) + 1) * alignment) + 8;

                uint32_t* nextOfPrev = &freeList;
                for(Header* list = GetHeaderPtr(freeList); list; nextOfPrev = &list->nextOrMagic, list = GetHeaderPtr(list->nextOrMagic))
                {
                    if(list->size > size && list->size - size >= 16)
                    {
                        Header* newHeader = new(reinterpret_cast<std::byte*>(list + 1) + size) Header{list->nextOrMagic, list->size - size - static_cast<uint32_t>(sizeof(Header))};
                        *nextOfPrev = reinterpret_cast<std::byte*>(newHeader) - data;
                        list->nextOrMagic = MAGIC_VALUE;
                        list->size = size;
                        used++;
                        return list + 1;
                    }

                    if(list->size == size)
                    {
                        *nextOfPrev = list->nextOrMagic;
                        list->nextOrMagic = MAGIC_VALUE;
                        used++;
                        return list + 1;
                    }
                }

                return nullptr;
            }

            void Deallocate(void* ptr)
            {
                Header* header = static_cast<Header*>(ptr) - 1;
                if(header->nextOrMagic != MAGIC_VALUE)
                    throw std::runtime_error("Invalid pointer provided to DynamicAllocator::Chunk::Deallocate");

                Header* prev = nullptr;
                Header* cur = GetHeaderPtr(freeList);
                while(cur && ptr < static_cast<void*>(cur))
                {
                    prev = cur;
                    cur = GetHeaderPtr(cur->nextOrMagic);
                }

                // TODO
            }
        };

        Chunk* head = nullptr;
        size_t emptyChunkCount = 0;

    public:
        static constexpr size_t PER_CHUNK_MEMORY = Chunk::MAX_MEMORY;

        constexpr  DynamicAllocator() noexcept = default;
        constexpr ~DynamicAllocator() noexcept
        {
            while(head)
            {
                Chunk* temp = head->next;
                delete head;
                head = temp;
            }
        }




        [[nodiscard]] void* Allocate(uint32_t size, uint32_t alignment = 8)
        {
            for(Chunk* chunk = head; chunk; chunk = chunk->next)
            {
                if(chunk->HasSpace())
                {
                    if constexpr(LAZILY_DEALLOCATED_CHUNK_COUNT > 0)
                    {
                        if(chunk->IsEmpty())
                        {
                            void* allocated = chunk->Allocate(size, alignment);
                            if(allocated)
                            {
                                emptyChunkCount--;
                                return allocated;
                            }
                        }
                    }

                    void* allocated = chunk->Allocate(size, alignment);
                    if(allocated)
                        return allocated;
                }
            }

            Chunk* newChunk = new Chunk();
            newChunk->next = head;
            head = newChunk;
            void* allocated = newChunk->Allocate(size, alignment);
            if(!allocated)
            {
                emptyChunkCount++;
                return nullptr;
            }
            return allocated;
        }

        [[nodiscard]] bool Deallocate(void* ptr)
        {
            Chunk** nextOfPrev = &head; // next pointer of previous chunk
            for(Chunk* chunk = head; chunk; nextOfPrev = &chunk->next, chunk = chunk->next)
            {
                if(chunk->Owns(ptr))
                {
                    chunk->Deallocate(ptr);
                    if(chunk->IsEmpty())
                    {
                        if constexpr(LAZILY_DEALLOCATED_CHUNK_COUNT == 0)
                        {
                            *nextOfPrev = chunk->next;
                            delete chunk;
                        }
                        else
                        {
                            if(emptyChunkCount >= LAZILY_DEALLOCATED_CHUNK_COUNT)
                            {
                                *nextOfPrev = chunk->next;
                                delete chunk;
                            }
                            else
                                emptyChunkCount++;
                        }
                    }
                    return true;
                }
            }

            return false;
        }




        template<typename T, typename... Args>
        [[nodiscard]] T* Create(Args&&... args)
        {
            static_assert(sizeof(T) <= Chunk::MAX_MEMORY, "T is too large for this DynamicAllocator.");
            return new(Allocate(sizeof(T), alignof(T))) T(std::forward<Args>(args)...);
        }

        template<typename T>
        [[nodiscard]] bool Destroy(T* obj)
        {
            static_assert(sizeof(T) <= Chunk::MAX_MEMORY, "T is too large for this DynamicAllocator.");
            obj->~T();
            return Deallocate(obj);
        }
    };










    class GlobalAllocator
    {
        inline static constinit SlabAllocator<8, 8, 4096>  B8;
        inline static constinit SlabAllocator<16, 8, 4096> B16;
        inline static constinit SlabAllocator<32, 8, 4096> B32;
        inline static constinit SlabAllocator<64, 8, 4096> B64;
        //inline static constinit DynamicAllocator<4096>     D;

    public:
        static void* Allocate(size_t size)
        {
            if(size <= 8)
                return B8.Allocate();
            if(size <= 16)
                return B16.Allocate();
            if(size <= 32)
                return B32.Allocate();
            if(size <= 64)
                return B64.Allocate();
            //if(size <= DynamicAllocator<>::PER_CHUNK_MEMORY)
            //    return D.Allocate(size);

            return ::operator new(size);
        }

        static bool Deallocate(void* p, size_t size)
        {

            if(size <= 8)
                return B8.Deallocate(p);
            if(size <= 16)
                return B16.Deallocate(p);
            if(size <= 32)
                return B32.Deallocate(p);
            if(size <= 64)
                return B64.Deallocate(p);
            //if(size <= DynamicAllocator<>::PER_CHUNK_MEMORY)
            //    return D.Deallocate(p);

            ::operator delete(p);
            return true;
        }




        template<typename T, typename... Args>
        [[nodiscard]] static T* Create(Args&&... args)
        {
            if constexpr(sizeof(T) <= 8)
                return B8.Create<T>(std::forward<Args>(args)...);
            else if constexpr(sizeof(T) <= 16)
                return B16.Create<T>(std::forward<Args>(args)...);
            else if constexpr(sizeof(T) <= 32)
                return B32.Create<T>(std::forward<Args>(args)...);
            else if constexpr(sizeof(T) <= 64)
                return B64.Create<T>(std::forward<Args>(args)...);
            //else if constexpr(sizeof(T) <= DynamicAllocator<>::PER_CHUNK_MEMORY)
            //    return D.Create<T>(std::forward<Args>(args)...);
            else
                return new T(std::forward<Args>(args)...);
        }

        template<typename T>
        [[nodiscard]] static bool Destroy(T* obj)
        {
            if constexpr(sizeof(T) <= 8)
                return B8.Destroy(obj);
            else if constexpr(sizeof(T) <= 16)
                return B16.Destroy(obj);
            else if constexpr(sizeof(T) <= 32)
                return B32.Destroy(obj);
            else if constexpr(sizeof(T) <= 64)
                return B64.Destroy(obj);
            //else if constexpr(sizeof(T) <= DynamicAllocator<>::PER_CHUNK_MEMORY)
            //    return D.Destroy(obj);
            else
                return delete obj;
        }
    };
}










FDF_EXPORT namespace fdf
{
    class Entry
    {
        template<size_t BLOCK_SIZE, size_t BLOCK_ALIGNMENT, size_t CHUNK_SIZE, size_t LAZILY_DEALLOCATED_CHUNK_COUNT>
        friend class detail::SlabAllocator;

        constexpr  Entry() noexcept = default;
        constexpr ~Entry() noexcept = default;

    public:
        static Entry* Create (        )  { return detail::GlobalAllocator::Create<Entry>(); }
        static void   Destroy(Entry* e)  { (void)detail::GlobalAllocator::Destroy(e); }

    public:
        Type type = Type::Invalid;
        uint8_t identifierSize = 0;
        uint16_t depth = 0;
        uint32_t size = 0;
        Entry* parent = nullptr;
        void* data = nullptr;
        char identifier[40] = {};
    };
}
