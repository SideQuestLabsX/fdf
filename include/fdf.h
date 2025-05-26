
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

#if FDF_NO_COMMENTS
    #define FDF_COMMENT_SWITCH(...)
#else
    #define FDF_COMMENT_SWITCH(...) __VA_ARGS__
#endif




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
    struct Test;

    template<auto DIAGNOSTIC_CALLBACK>
    struct Utils;
}





FDF_EXPORT namespace fdf
{
    template <typename Callable>
    constexpr bool IsValidDiagnosticCallback = std::is_invocable_r_v<bool, Callable, Diagnostic, std::string_view>;
    inline constexpr auto DefaultDiagnosticCallback = [](Diagnostic diagnostic, std::string_view message) -> bool  { return !IsError(diagnostic); };

    template<auto DIAGNOSTIC_CALLBACK = DefaultDiagnosticCallback> requires(IsValidDiagnosticCallback<decltype(DIAGNOSTIC_CALLBACK)>)
    [[nodiscard]] Entry* ParseFile(const std::filesystem::path& filepath) noexcept;

    template<auto DIAGNOSTIC_CALLBACK = DefaultDiagnosticCallback> requires(IsValidDiagnosticCallback<decltype(DIAGNOSTIC_CALLBACK)>)
    [[nodiscard]] constexpr Entry* ParseFileContent(std::string_view content) noexcept;

    template<auto DIAGNOSTIC_CALLBACK = DefaultDiagnosticCallback> requires(IsValidDiagnosticCallback<decltype(DIAGNOSTIC_CALLBACK)>)
    constexpr void Release(Entry* e) noexcept;




    class Entry
    {
        constexpr  Entry() noexcept = default;
        constexpr ~Entry();

        friend struct detail::Test;

        template<auto ERROR_CALLBACK>
        friend struct detail::Utils;

        struct CommentControlBlock { uint32_t capacity, size; };

    private:
        Type type = Type::Invalid;
        uint8_t identifierSize = 0;
        uint16_t depth = 0;
        uint32_t size = 0;
        //uint32_t capacity = 0;
        Entry* parent = nullptr;
        void* data = nullptr;
        char* identifier = nullptr;
    #if !FDF_NO_COMMENTS
        CommentControlBlock* comment = nullptr;
    #endif
        struct Children
        {
            size_t capacity = 0;
            Entry* data[128] = {nullptr};
        };
        Children** children = reinterpret_cast<Children**>(&data);


    public:
        [[nodiscard]] constexpr size_t   GetChildCount() const noexcept  { return IsContainer()? size : 0; }
        [[nodiscard]] constexpr uint8_t  GetDepth()      const noexcept  { return depth; }
        [[nodiscard]] constexpr Type     GetType()       const noexcept  { return type; }
        [[nodiscard]] constexpr bool     IsValid()       const noexcept  { return type != Type::Invalid; }
        [[nodiscard]] constexpr bool     IsNull()        const noexcept  { return type == Type::Null; }
        [[nodiscard]] constexpr bool     IsNil()         const noexcept  { return IsNull(); }
        [[nodiscard]] constexpr bool     IsContainer()   const noexcept  { return type == Type::Array || type == Type::Map; }
        [[nodiscard]] constexpr bool     HasValue()      const noexcept  { return IsValid() && !IsNull() && !IsContainer(); }

        [[nodiscard]] constexpr std::string_view GetIdentifier() const noexcept  { return {identifier, identifierSize}; }

        [[nodiscard]] constexpr std::string GetFullIdentifier() const noexcept
        {
            const Entry* cur = parent;
            std::string temp = std::string(GetIdentifier());
            while(cur)
            {
                temp = std::format("{}.{}", cur->GetIdentifier(), temp);
                cur = cur->parent;
            }
            return temp;
        }

        [[nodiscard]] constexpr std::string_view GetComment() const noexcept
        {
            #if FDF_NO_COMMENTS
                return {};
            #else
                if(comment)
                    return {reinterpret_cast<char*>(comment + 1), comment->size};
                return {};
            #endif
        }

    public:
        constexpr void SetIdentifier(std::string_view newIdentifier, bool bClampIfLongerThanMax = true);
        constexpr void SetComment(std::string_view newComment);
        constexpr void ReleaseData();
        constexpr void ReleaseComment();

    public:
        constexpr void AddChild(Entry& e, bool bForceEvenIfHasParent = false);
        constexpr Entry* RemoveChild(Entry& e);
        constexpr Entry* RemoveChild(std::string_view identifier);
        constexpr Entry* RemoveChild(size_t i);
        constexpr void ClearChildren(bool bAlsoDestroy = true);
    };
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

    #if FDF_NO_COMMENTS
        constexpr size_t DATA_OVERHEAD_SIZE = sizeof(size_t);
    #else
        constexpr size_t DATA_OVERHEAD_SIZE = sizeof(size_t) + sizeof(void*);
    #endif

    constexpr size_t INITIAL_PARENT_DATA_SIZE = DATA_OVERHEAD_SIZE + 4 * sizeof(void*);


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
        explicit constexpr Tokenizer(std::string_view content_) noexcept
            : content(content_), index(0), line(1), lastNewLineIndex(0), currentToken(GetNextToken())  { }

        [[nodiscard]] constexpr Token Current() const noexcept  { return currentToken; }
        [[nodiscard]] constexpr Token Advance()       noexcept  { currentToken = GetNextToken(); return currentToken; }

        [[nodiscard]] constexpr std::string_view ToView(Token token) const noexcept { return content.substr(token.startPosition, token.count); }

    private:
        [[nodiscard]] constexpr Token GetNextToken() noexcept;

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
        constexpr static void* Allocate(size_t size)
        {
            if consteval
            {
                return ::operator new(size);
            }
            else
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
        }

        constexpr static bool Deallocate(void* p, size_t size)
        {
            if consteval
            {
                ::operator delete(p);
                return true;
            }
            else
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
        }




        template<size_t size>
        constexpr static void* Allocate()
        {
            if consteval
            {
                return ::operator new(size);
            }
            else
            {
                if constexpr(size <= 8)
                    return B8.Allocate();
                else if constexpr(size <= 16)
                    return B16.Allocate();
                else if constexpr(size <= 32)
                    return B32.Allocate();
                else if constexpr(size <= 64)
                    return B64.Allocate();
                //else if constexpr(size <= DynamicAllocator<>::PER_CHUNK_MEMORY)
                //    return D.Allocate(size);
                else
                    return ::operator new(size);
            }
        }

        template<size_t size>
        constexpr static bool Deallocate(void* p)
        {
            if consteval
            {
                ::operator delete(p);
                return true;
            }
            else
            {
                if constexpr(size <= 8)
                    return B8.Deallocate(p);
                else if constexpr(size <= 16)
                    return B16.Deallocate(p);
                else if constexpr(size <= 32)
                    return B32.Deallocate(p);
                else if constexpr(size <= 64)
                    return B64.Deallocate(p);
                //else if constexpr(size <= DynamicAllocator<>::PER_CHUNK_MEMORY)
                //    return D.Deallocate(p);
                else
                {
                    ::operator delete(p);
                    return true;
                }
            }
        }




        template<typename T, typename... Args>
        [[nodiscard]] constexpr static T* Create(Args&&... args)
        {
            if consteval
            {
                return new T(std::forward<Args>(args)...);
            }
            else
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
        }

        template<typename T>
        [[nodiscard]] constexpr static bool Destroy(T* obj)
        {
            if consteval
            {
                delete obj;
                return true;
            }
            else
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
                {
                    delete obj;
                    return true;
                }
            }
        }
    };
}










namespace fdf::detail
{
    constexpr Token Tokenizer::GetNextToken() noexcept
    {
        if(index >= content.size())
            return TokenType::EndOfFile;

        while(std::isspace(content[index]))
        {
            if(content[index] == '\n')
            {
                Token token = Token(TokenType::NewLine, index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                while(index < content.size() && std::isspace(content[index]))
                {
                    if(content[index] == '\n')
                    {
                        line++;
                        lastNewLineIndex = index;
                    }


                    index++;
                    token.count++;
                }

                return token;
            }

            if(std::isspace(content[index]))
                index++;

            if(index >= content.size())
                return TokenType::EndOfFile;
        }



        if(content[index] == '\"' || content[index] == '\'')
        {
            size_t nextQuote = content.find_first_of(content[index], index + 1);
            if(nextQuote == std::string_view::npos)
                return TokenType::Invalid;  // Non matching quotes

            while(content[nextQuote - 1] == '\\' && content[nextQuote - 2] != '\\')
            {
                nextQuote = content.find_first_of(content[index], nextQuote + 1);
                if(nextQuote == std::string_view::npos)
                    return TokenType::Invalid;  // Non matching quotes
            }

            Token token = Token(TokenType::StringLiteral, index, nextQuote + 1 - index);
            index = nextQuote + 1;
            token.line = line;
            token.column = token.startPosition - lastNewLineIndex;
            token.extra8 = token.count - 2;
            return token;
        }



        if(content[index] == '/')
        {
            if(index + 2 >= content.size())
                return TokenType::Invalid; // not enough space for a comment

            if(content[index + 1] == '/') // single line comment
            {
                size_t newLinePos = content.find_first_of('\n', index + 2);
                Token token = Token(TokenType::Comment, content[index + 2] == ' '? index + 3 : index + 2);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;

                if(newLinePos != std::string_view::npos)
                {
                    token.count = newLinePos - token.startPosition;
                    index = newLinePos;
                    return token;
                }

                // There is no new lines left (comment is at the end of the file)
                token.count = content.size() - token.startPosition;
                index = -1;
                return token;
            }

            if(content[index + 1] == '*') // multi line comment
            {
                size_t slashPos = content.find_first_of('/', index + 2);
                while(true)
                {
                    if(slashPos == std::string_view::npos)
                        return TokenType::Invalid; // Non matching comment scope (There is only "/*" and not "*/")

                    if(content[slashPos - 1] == '*')
                    {
                        Token token = Token(TokenType::Comment, index + 2);
                        token.line = line;
                        token.column = token.startPosition - lastNewLineIndex;
                        token.extra8 = 1;  // Means multi line
                        token.count = slashPos - 2 - token.startPosition;

                        for(size_t i = index + 2; i < slashPos - 1; i++)
                        {
                            if(content[i] == '\n')
                                line++;
                        }

                        index = slashPos + 1;
                        if(index + 1 < content.size() && content[index] == '\n')
                        {
                            lastNewLineIndex = index;
                            line++;
                            index++;
                        }

                        if(token.count == -1)
                            token.count = 0;
                        return token;
                    }

                    slashPos = content.find_first_of('/', slashPos + 2);
                }
            }

            return TokenType::Invalid;  // slash "/" without a comment
        }



        if(content[index] == '{')
            return Token(TokenType::CurlyBraceOpen, index++, 1);
        if(content[index] == '}')
            return Token(TokenType::CurlyBraceClose, index++, 1);
        if(content[index] == '[')
            return Token(TokenType::SquareBraceOpen, index++, 1);
        if(content[index] == ']')
            return Token(TokenType::SquareBraceClose, index++, 1);

        if(content[index] == '=')
            return Token(TokenType::Equal, index++, 1);
        if(content[index] == ',')
            return Token(TokenType::Comma, index++, 1);



        if(content[index] == '$')
        {
            if(index + 1 < content.size() && content[index + 1] == '{')
            {
                size_t braceClose = content.find_first_of('}', index + 2);
                if(braceClose == std::string_view::npos) // we reached eof before "}"
                    return TokenType::Invalid;

                Token token = Token(TokenType::EvaluateLiteral, index, braceClose + 1 - index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                index = braceClose + 1;
                return token;
            }

            return TokenType::Invalid; // Random "$" without "{"
        }



        if(std::isalpha(content[index]) || content[index] == '_') // identifier, keyword
        {
            Token token = Token(TokenType::Identifier, index);
            token.line = line;
            token.column = token.startPosition - lastNewLineIndex;
            auto checkKeywords = [&](std::string_view view) -> void
            {
                for(size_t i = 0; i < KEYWORD_COUNT; i++)
                {
                    if(view == KEYWORDS[i])
                    {
                        token.type = TokenType::Keyword;
                        token.extra8 = i;  // Used as keyword index
                        return;
                    }
                }

                if(view.starts_with(KEYWORDS[2]) || view.starts_with(KEYWORDS[3]))
                {
                    token.type = TokenType::Keyword;
                    token.extra8 = 4;  // Used as keyword index
                }
            };

            size_t firstNonAlpha = index + 1;
            while(firstNonAlpha < content.size() && (std::isalpha(content[firstNonAlpha]) || std::isdigit(content[firstNonAlpha]) || content[firstNonAlpha] == '_'))
                firstNonAlpha++;

            token.count = firstNonAlpha - token.startPosition;
            std::string_view view = ToView(token);

            if(firstNonAlpha >= content.size()) // we reached eof before any space or any other token
            {
                checkKeywords(view);
                index = -1;
                return token;
            }

            checkKeywords(view);
            index = firstNonAlpha;
            return token;
        }



        if(std::isdigit(content[index]) || content[index] == '-')
        {
            if(content[index] == '0' && index + 3 < content.size() && content[index + 1] == 'x')  // Hex
            {
                size_t firstNonHex = content.find_first_not_of("0123456789abcdefABCDEF", index + 2);
                size_t firstChar = content.find_first_of("abcdefABCDEF", index + 2);
                size_t firstHash = content.find_first_of('#', index + 2);
                if(firstNonHex == firstHash && firstNonHex != std::string_view::npos) // First non hex character is "#"
                {
                    Token token = Token(TokenType::HexLiteral, index, firstNonHex - index);
                    token.line = line;
                    token.column = token.startPosition - lastNewLineIndex;
                    index = firstNonHex + 1;
                    token.extra8 = token.count;
                    return token;
                }

                if(firstNonHex == std::string_view::npos) // we reached eof before any space or any other token
                    return TokenType::Invalid;

                if(firstChar < firstNonHex) // it contains hex characters, so we can't let it slide as a number
                    return TokenType::Invalid;

                // Let it fallthrough as "multi dimensional int"
            }



            size_t firstNonDigit = content.find_first_not_of("0123456789", index + 1);
            if(firstNonDigit == std::string_view::npos)  // we reached eof before any space or any other token
            {
                Token token = Token(TokenType::IntLiteral, index, content.size() - index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc)
                index = -1;
                return token;
            }

            if(std::isspace(content[firstNonDigit]) || content[firstNonDigit] == ',')
            {
                Token token = Token(TokenType::IntLiteral, index, firstNonDigit - index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc)
                index = firstNonDigit;
                return token;
            }

            if(content[firstNonDigit] == '.')  // float, version or multi dimensional float
            {
                Token token = Token(TokenType::FloatLiteral, index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc)

                size_t dotCount = 0;
                size_t temp = firstNonDigit;
                char lastChar = '.';
                bool bContainsDash = false;

                auto calculateResult = [&]() -> void
                {
                    if(lastChar == '.' || lastChar == 'x')
                    {
                        token.type = TokenType::Invalid;  // Must end with a digit
                        return;
                    }

                    token.count = temp - token.startPosition;

                    if(dotCount == 2 || dotCount == 3)
                    {
                        if(bContainsDash)
                        {
                            token.type = TokenType::Invalid;  // Version cannot contain dash
                            return;
                        }

                        token.type = TokenType::VersionLiteral;
                        token.extra8 = dotCount + 1;
                        return;
                    }
                };

                while(temp < content.size())
                {
                    lastChar = content[temp];
                    if(std::isdigit(content[temp]) || (content[temp] == '-' && lastChar == 'x'))
                    {
                        if(content[temp] == '-')
                            bContainsDash = true;

                        temp++;
                        continue;
                    }

                    if(content[temp] == '.')
                    {
                        if(dotCount == 1 && token.extra8 > 1)
                            return TokenType::Invalid;  // Float can't have more than 1 dot
                        if(dotCount > 2)
                            return TokenType::Invalid;  // Version can have 3 dots maximum
                        
                        dotCount++;
                        temp++;
                        continue;
                    }

                    if(content[temp] == 'x')
                    {
                        dotCount = 0;
                        token.extra8++;
                        temp++;
                        continue;
                    }

                    if(std::isspace(content[temp]) || content[temp] == ',')
                    {
                        calculateResult();
                        index = temp;
                        return token;
                    }

                    return TokenType::Invalid; // Non allowed character
                }

                calculateResult();
                index = -1;
                return token;
            }

            if(content[firstNonDigit] == 'x')  // multi dimensional int
            {
                Token token = Token(TokenType::IntLiteral, index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                token.extra8 = 2;  // Used as dimension (2d, 3d, 4d, 5d, etc)

                size_t dotCount = 0;
                while(true)
                {
                    size_t previous = firstNonDigit;
                    firstNonDigit = content.find_first_not_of("0123456789", firstNonDigit + 1);

                    if(firstNonDigit == std::string_view::npos) // we reached eof before any space or any other token
                    {
                        token.count = content.size() - token.startPosition;
                        index = -1;
                        return token;
                    }

                    if(previous + 1 == firstNonDigit && !(content[previous] == ',' && std::isspace(content[firstNonDigit])) && !(content[previous] == 'x' && content[firstNonDigit] == '-'))
                        return TokenType::Invalid;  // It must have number(s) in between

                    if(std::isspace(content[firstNonDigit]) || content[firstNonDigit] == ',')
                    {
                        token.count = firstNonDigit - token.startPosition;
                        index = firstNonDigit;
                        return token;
                    }

                    if(content[firstNonDigit] == 'x')
                    {
                        token.extra8++;
                        dotCount = 0;
                        continue;
                    }

                    if(content[firstNonDigit] == '.')
                    {
                        dotCount++;
                        if(dotCount > 1)
                            return TokenType::Invalid;  // Multi dimensional numbers can't contain more than 1 dot (for each number)

                        continue;
                    }
                }
            }





            /* Possible datetime formats
            *  2024-12-24T15:30:00       -> Date + Time without timezone info (Usually interpreted as local time)
            *  2024-12-24T15:30:00Z      -> Date + Time with timezone info (Z means utc/zulu time)
            *  2024-12-24T15:30:00+05:30 -> Date + Time with timezone info (5 hours and 30 minutes ahead of UTC)
            *  2024-12-24                -> Date
            *  15:30:00                  -> Time
            *  2024-12-24T15:30:00.123Z  -> Date + Time with timezone info (Z means utc/zulu time) and milliseconds (123ms)
            *  2024-W52-2                -> Year + Week + Weekday (52nd week of 2024, tuesday)
            *  2024-359                  -> Year + Day of Year (359th day of 2024)
            */

            /* Possible duration formats (if we wanna support it, currently we don't) (Note: not here, it starts with a letter)
            *  P3D              -> 3 days
            *  P2W              -> 2 weeks (14 days)
            *  P1Y2M3D          -> 1 year, 2 months, 3 days
            *  P2WT3H           -> 2 weeks and 3 hours
            *  P5DT4H30M        -> 5 days, 4 hours, and 30 minutes
            *  PT1H45M          -> 1 hour and 45 minutes
            *  P1Y2M3DT4H30M10S -> 1 year, 2 months, 3 days, 4 hours, 30 minutes, and 10 seconds
            *  P10M             -> 10 minutes
            *  PT10M            -> 10 minutes (alternative representation for time)
            *  PT1.5S           -> 1.5 seconds (1 second and 500 milliseconds)
            *  PT0.000001S      -> 1 microsecond (0.000001 seconds)
            *  P1.5Y            -> 1.5 years (1 year and 6 months)
            *  P3DT5H30M        -> 3 days, 5 hours, and 30 minutes
            *  PT0.5H           -> 30 minutes (0.5 hours)
            *  P2Y3M4DT5H6M7S   -> 2 years, 3 months, 4 days, 5 hours, 6 minutes, and 7 seconds
            */


            if(content[firstNonDigit] == '-')  // date or datetime
            {
                Token token = Token(TokenType::TimestampLiteral, index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                size_t firstNonDate = content.find_first_not_of("0123456789TZW-+:.", index);
                if(firstNonDate == std::string_view::npos)
                {
                    token.count = content.size() - token.startPosition;
                    index = -1;
                    return token;
                }

                if(std::isspace(content[firstNonDate]) || content[firstNonDate] == ',')
                {
                    token.count = firstNonDate - token.startPosition;
                    index = firstNonDate;
                    return token;
                }

                return TokenType::Invalid;  // Invalid character after timestamp
            }

            if(content[firstNonDigit] == ':')  // time
            {
                Token token = Token(TokenType::TimestampLiteral, index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                size_t firstNonDate = content.find_first_not_of("0123456789+:.", index);  // idk if it can include timezone ("+" sign)
                if(firstNonDate == std::string_view::npos)
                {
                    token.count = content.size() - token.startPosition;
                    index = -1;
                    token.extra8 = token.count;
                    return token;
                }

                if(std::isspace(content[firstNonDate]) || content[firstNonDate] == ',')
                {
                    token.count = firstNonDate - token.startPosition;
                    index = firstNonDate;
                    token.extra8 = token.count;
                    return token;
                }

                return TokenType::Invalid;  // Invalid character after timestamp
            }

            return TokenType::Invalid;  // Something we didn't process yet?
        }

        return TokenType::Invalid;  // Something we didn't process yet?
    }
}











namespace fdf::detail
{
    template<auto DIAGNOSTIC_CALLBACK>
    struct Utils
    {
        template<typename... Args>
        [[nodiscard]] constexpr static Entry* Create(Args&&... args);
                      constexpr static void   Destroy(Entry* e);

        [[nodiscard]] constexpr static Entry* ParseFileContent(std::string_view content) noexcept;
        [[nodiscard]] constexpr static bool   ParseVariable   (Tokenizer& tokenizer, Entry& parent   FDF_COMMENT_SWITCH(, Token comment));
        [[nodiscard]] constexpr static bool   ParseSimpleValue(Tokenizer& tokenizer, Entry& entry    FDF_COMMENT_SWITCH(, Token comment));
        [[nodiscard]] constexpr static bool   ParseArray      (Tokenizer& tokenizer, Entry& array   FDF_COMMENT_SWITCH(, Token comment));
        [[nodiscard]] constexpr static bool   ParseMap        (Tokenizer& tokenizer, Entry& map   FDF_COMMENT_SWITCH(, Token comment));
    };
}









namespace fdf
{
    constexpr Entry::~Entry()
    {
        if(parent)
            parent->RemoveChild(*this);
        ReleaseData();
        if(identifier && identifierSize > 0)
            detail::GlobalAllocator::Deallocate(identifier, identifierSize + 1);
        ReleaseComment();
    }
    
    
    constexpr void Entry::SetIdentifier(std::string_view newIdentifier, const bool bClampIfLongerThanMax)
    {
        if(identifierSize != 0 || identifier != nullptr)
        {
            if(identifierSize != 0 && identifier != nullptr)
            {
                detail::GlobalAllocator::Deallocate(identifier, identifierSize + 1);
                identifierSize = 0;
                identifier = nullptr;
            }
            else
                throw std::runtime_error("Corrupted entry memory for identifier");
        }
        
        
        if(newIdentifier.size() > std::numeric_limits<uint8_t>::max())
        {
            if(bClampIfLongerThanMax)
                identifierSize = std::numeric_limits<uint8_t>::max();
            else
                throw std::runtime_error(std::format("new identifier is too long for Entry::SetIdentifier\nLimit: 39 characters - Provided: {} characters ()", newIdentifier.size(), newIdentifier));
        }
        else
            identifierSize = newIdentifier.size();
            

        identifier = static_cast<char*>(detail::GlobalAllocator::Allocate(identifierSize + 1));
        detail::constexpr_memcpy(identifier, newIdentifier.data(), identifierSize);
        identifier[identifierSize] = '\0';
    }

    constexpr void Entry::SetComment(std::string_view newComment)
    {
        if(comment)
        {
            if(newComment.size() > comment->capacity)
            {
                detail::GlobalAllocator::Deallocate(comment, comment->capacity + sizeof(Entry::CommentControlBlock) + 1);
                comment = static_cast<CommentControlBlock*>(detail::GlobalAllocator::Allocate(newComment.size() + sizeof(Entry::CommentControlBlock) + 1));
                comment->capacity = newComment.size();
                comment->size = 0;
            }
            else
            {
                comment->size = 0;
            }
        }
        else
        {
            comment = static_cast<CommentControlBlock*>(detail::GlobalAllocator::Allocate(newComment.size() + sizeof(Entry::CommentControlBlock) + 1));
            comment->capacity = newComment.size();
            comment->size = 0;
        }

        detail::constexpr_memcpy(reinterpret_cast<char*>(comment + 1), newComment.data(), newComment.size());
        reinterpret_cast<char*>(comment + 1)[newComment.size()] = '\0';
    }

    constexpr void Entry::ReleaseData()
    {
        switch(type)
        {
        case Type::Bool:
            detail::GlobalAllocator::Deallocate(data, size * sizeof(bool));
            break;
        case Type::Int:
            detail::GlobalAllocator::Deallocate(data, size * sizeof(int64_t));
            break;
        case Type::UInt:
            detail::GlobalAllocator::Deallocate(data, size * sizeof(uint64_t));
            break;
        case Type::Float:
            detail::GlobalAllocator::Deallocate(data, size * sizeof(double));
            break;
        case Type::String:
            detail::GlobalAllocator::Deallocate(data, size * sizeof(char));
            break;
        case Type::Hex:
            detail::GlobalAllocator::Deallocate(data, size * sizeof(char) / 8);
            break;
        case Type::Version:
            detail::GlobalAllocator::Deallocate(data, size * sizeof(uint64_t));
            break;
        case Type::Timestamp:
            detail::GlobalAllocator::Deallocate(data, size * sizeof(char));
            break;
        case Type::Array:
        case Type::Map:
            ClearChildren();
            return;
        default:
            return;
        }
        
        data = nullptr;
        type = Type::Null;
    }

    constexpr void Entry::ReleaseComment()
    {
        if(comment)
            detail::GlobalAllocator::Deallocate(comment, comment->capacity + sizeof(Entry::CommentControlBlock) + 1);
    }


    constexpr void Entry::AddChild(Entry& e, bool bForceEvenIfHasParent)
    {
        if(type != Type::Array && type != Type::Map)
            return;
        if(e.parent)
        {
            if(!bForceEvenIfHasParent)
                return;
            e.parent->RemoveChild(e);
        }
        e.parent = this;

        if(data)
        {
            const size_t capacity = *static_cast<size_t*>(data);
            
            if(static_cast<size_t>(size) >= capacity)
            {
                void* newBuffer = detail::GlobalAllocator::Allocate(2 * capacity * sizeof(void*) + sizeof(size_t));
                *static_cast<size_t*>(newBuffer) = 2 * capacity;

                for(size_t i = 0; i < size; i++)
                    (static_cast<Entry**>(newBuffer) + 1)[i] = (static_cast<Entry**>(data) + 1)[i];

                (void)detail::GlobalAllocator::Deallocate(data, capacity * sizeof(void*) + sizeof(size_t));
                data = newBuffer;
            }

            (static_cast<Entry**>(data) + 1)[size++] = &e;
        }
        else
        {
            data = detail::GlobalAllocator::Allocate(4 * sizeof(void*) + sizeof(size_t));
            *static_cast<size_t*>(data) = 4;
            size = 0;  // Just in case
            (static_cast<Entry**>(data) + 1)[size++] = &e;
        }
    }

    constexpr Entry* Entry::RemoveChild(Entry& e)
    {
        if(size == 0 || (type != Type::Array && type != Type::Map))
            return nullptr;

        for(size_t i = 0; i < size; i++)
        {
            if((static_cast<Entry**>(data) + 1)[i] == &e)
                return RemoveChild(i);
        }
        return nullptr;
    }

    constexpr Entry* Entry::RemoveChild(std::string_view identifier)
    {
        if(size == 0 || type != Type::Map)
            return nullptr;

        for(size_t i = 0; i < size; i++)
        {
            if((static_cast<Entry**>(data) + 1)[i]->GetIdentifier() == identifier)
                return RemoveChild(i);
        }
        return nullptr;
    }

    constexpr Entry* Entry::RemoveChild(size_t i)
    {
        if(i >= size || (type != Type::Array && type != Type::Map))
            return nullptr;

        Entry* original = (static_cast<Entry**>(data) + 1)[i];
        for(; i + 1 < size; i++)
            (static_cast<Entry**>(data) + 1)[i] = (static_cast<Entry**>(data) + 1)[i + 1];

        (static_cast<Entry**>(data) + 1)[i] = nullptr;
        size--;
        return original;
    }
    constexpr void Entry::ClearChildren(bool bAlsoDestroy)
    {
        if((type != Type::Array && type != Type::Map))
            return;
        
        if(data)
        {
            if(bAlsoDestroy)
            {
                for(size_t i = 0; i < size; i++)
                {
                    (static_cast<Entry**>(data) + 1)[i]->parent = nullptr;
                    detail::Utils<0>::Destroy((static_cast<Entry**>(data) + 1)[i]);
                }
            }
            
            (void)detail::GlobalAllocator::Deallocate(data, *static_cast<size_t*>(data) * sizeof(void*) + sizeof(size_t));
            data = nullptr;
            type = Type::Null;
        }
    }
}










namespace fdf
{
    template<auto DIAGNOSTIC_CALLBACK> requires(IsValidDiagnosticCallback<decltype(DIAGNOSTIC_CALLBACK)>)
    inline Entry* ParseFile(const std::filesystem::path& filepath) noexcept
    {
        if(!std::filesystem::exists(filepath) || !std::filesystem::is_regular_file(filepath))
            return nullptr;

        std::ifstream file(filepath);
        if(!file)
            return nullptr;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return ParseFileContent(content);
    }

    template<auto DIAGNOSTIC_CALLBACK> requires(IsValidDiagnosticCallback<decltype(DIAGNOSTIC_CALLBACK)>)
    constexpr Entry* ParseFileContent(std::string_view content) noexcept
    {
        return detail::Utils<DIAGNOSTIC_CALLBACK>::ParseFileContent(content);
    }

    template <auto DIAGNOSTIC_CALLBACK> requires (IsValidDiagnosticCallback<decltype(DIAGNOSTIC_CALLBACK)>)
    constexpr void Release(Entry* e) noexcept
    {
        return detail::Utils<DIAGNOSTIC_CALLBACK>::Destroy(e);
    }
}

namespace fdf::detail
{
    template<auto DIAGNOSTIC_CALLBACK>
    template<typename... Args>
    inline constexpr Entry* detail::Utils<DIAGNOSTIC_CALLBACK>::Create(Args&&... args)
    {
        if(std::is_constant_evaluated())
        {
            return new Entry{std::forward<Args>(args)...};
        }
        else
        {
            return new(detail::GlobalAllocator::Allocate<sizeof(Entry)>()) Entry{std::forward<Args>(args)...};
        }
    }

    template<auto DIAGNOSTIC_CALLBACK>
    inline constexpr void detail::Utils<DIAGNOSTIC_CALLBACK>::Destroy(Entry* e)
    {
        if(std::is_constant_evaluated())
        {
            delete e;
        }
        else
        {
            e->~Entry();
            (void)detail::GlobalAllocator::Deallocate<sizeof(Entry)>(e);
        }
    }

    template<auto DIAGNOSTIC_CALLBACK>
    inline constexpr Entry* Utils<DIAGNOSTIC_CALLBACK>::ParseFileContent(std::string_view content) noexcept
    {
        Tokenizer tokenizer(content);
        #if !FDF_NO_COMMENTS
            Token fileCommentToken = TokenType::NonExisting;
        #endif

        Entry* root = Create();
        root->type = Type::Map;
        root->depth = static_cast<uint16_t>(-1);
        
        while(true)
        {
            #if !FDF_NO_COMMENTS
                Token comment = TokenType::NonExisting;
            #endif
            Token currentToken = tokenizer.Current();
            if(currentToken.type == TokenType::Invalid)
            {
                Destroy(root);
                return nullptr;
            }
        
            while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
            {
            #if !FDF_NO_COMMENTS
                if(currentToken.type == TokenType::Comment)
                {
                    if(root->size == 0 && root->comment == nullptr && currentToken.count > 0 && content[currentToken.startPosition] == '#')
                    {
                        std::string_view sv = tokenizer.ToView(currentToken);
                        size_t firstChar = sv.find_first_not_of("# ");
                        if(firstChar == std::string_view::npos)
                        {
                            currentToken.startPosition += currentToken.count;
                            currentToken.count = 0;
                        }
                        else
                        {
                            currentToken.startPosition += firstChar;
                            currentToken.count = currentToken.count - firstChar;
                            if(content[currentToken.startPosition] == '\n')
                            {
                                currentToken.startPosition++;
                                currentToken.count--;
                            }
                        }
        
                        //if(fileCommentToken.type != TokenType::NonExisting)
                        //    if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("File already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", fileCommentToken.ToView(content), fileCommentToken.line, fileCommentToken.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                        //        return false;
                        fileCommentToken = currentToken;
                    }
                    else
                    {
                        //if(comment.type != TokenType::NonExisting)
                        //    if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                        //        return false;
                        comment = currentToken;
                    }
                }
            #endif
        
                currentToken = tokenizer.Advance();
            }
        
            if(currentToken.type == TokenType::Identifier)
            {
                if(!ParseVariable(tokenizer, *root   FDF_COMMENT_SWITCH(,comment)))
                {
                    Destroy(root);
                    return nullptr;
                }
        
                continue;
            }
            
            if(currentToken.type == TokenType::EndOfFile)
                break;
        
            Destroy(root);
            return nullptr;  // First token can't be anything else
        }

        #if !FDF_NO_COMMENTS
            // Trim the whitespace from the comment (not '\n')
            if(fileCommentToken.type != TokenType::NonExisting)
            {
                std::string_view view = tokenizer.ToView(fileCommentToken);
                root->comment = static_cast<Entry::CommentControlBlock*>(GlobalAllocator::Allocate(view.size() + sizeof(Entry::CommentControlBlock) + 1));
                root->comment->capacity = view.size();
                root->comment->size = 0;
                reinterpret_cast<char*>(root->comment + 1)[view.size()] = '\0';

                char* cur = reinterpret_cast<char*>(root->comment + 1);
                bool bAfterNewLine = true;

                for(char c : view)
                {
                    if(bAfterNewLine)
                    {
                        if(std::isspace(c))
                            continue;

                        cur[root->comment->size++] = c;
                        bAfterNewLine = false;
                    }
                    else
                    {
                        cur[root->comment->size++] = c;
                        bAfterNewLine = (c == '\n');
                    }
                }

                cur[root->comment->size] = '\0';
            }
        #endif
        
        return root;
    }

    template<auto DIAGNOSTIC_CALLBACK>
    [[nodiscard]] constexpr bool Utils<DIAGNOSTIC_CALLBACK>::ParseVariable   (Tokenizer& tokenizer, Entry& parent   FDF_COMMENT_SWITCH(, Token comment))
    {
        Token currentToken = tokenizer.Current();
        Entry* entry = Create();
        parent.AddChild(*entry);

        if(parent.type != Type::Array)
        {
            entry->SetIdentifier(tokenizer.ToView(currentToken), true);
            currentToken = tokenizer.Advance();
        }

        FDF_CHECK_TOKEN(currentToken);
        FDF_CHECK_TOKEN_FOR_EOF(currentToken);

        if(parent.type != Type::Array && parent.type != Type::Map)
            throw std::runtime_error("Non-supported parent type");

        entry->depth = parent.depth + 1;
        if(entry->depth == static_cast<uint16_t>(-1))
            throw std::runtime_error("Invalid depth");


        bool bHasEqual = false;
        if(currentToken.type == TokenType::Equal)
        {
            bHasEqual = true;
            currentToken = tokenizer.Advance();
            FDF_CHECK_TOKEN(currentToken);
            FDF_CHECK_TOKEN_FOR_EOF(currentToken);
        }

        while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
        {
        #if !FDF_NO_COMMENTS
            if(currentToken.type == TokenType::Comment)
            {
                //if(comment.type != TokenType::NonExisting)
                //    if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                //        return false;
                comment = currentToken;
            }
        #endif

            currentToken = tokenizer.Advance();
            FDF_CHECK_TOKEN(currentToken);
            FDF_CHECK_TOKEN_FOR_EOF(currentToken);
        }

        const bool result = [&]() -> bool
        {
            if(IsValueLiteral(currentToken.type) && (bHasEqual || parent.type == Type::Array))
                return ParseSimpleValue(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment));
            if(currentToken.type == TokenType::CurlyBraceOpen)
                return ParseMap(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment));
            if(currentToken.type == TokenType::SquareBraceOpen)
                return ParseArray(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment));
        
            return false;  // Something we didn't process yet?
        }();

        if(!result)
        {
            Destroy(entry);
        }

        return result;
    }
    template<auto DIAGNOSTIC_CALLBACK>
    [[nodiscard]] constexpr bool Utils<DIAGNOSTIC_CALLBACK>::ParseSimpleValue(Tokenizer& tokenizer, Entry& entry    FDF_COMMENT_SWITCH(, Token comment))
    {
        Token currentToken = tokenizer.Current();
        const std::string_view view = tokenizer.ToView(currentToken);

        auto postProcess = [&]()
        {
            currentToken = tokenizer.Advance();
            if(currentToken.type == TokenType::Comment)
            {
            #if !FDF_NO_COMMENTS
                //if(comment.type != TokenType::NonExisting)
                //    if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                //        return false;
                comment = currentToken;
            #endif
                currentToken = tokenizer.Advance();
            }

            if(currentToken.type == TokenType::NewLine)
                (void)tokenizer.Advance();

        #if !FDF_NO_COMMENTS
            if(comment.type != TokenType::NonExisting)
                entry.SetComment(tokenizer.ToView(comment));
        #endif
            return true;
        };

        if(currentToken.type == TokenType::Keyword)
        {
            if(currentToken.extra8 == 0 || currentToken.extra8 == 1)
            {
                entry.type = Type::Null;
                return postProcess();
            }
            if(currentToken.extra8 == 2 || currentToken.extra8 == 3)
            {
                entry.type = Type::Bool;
                entry.size = 1;
                entry.data = GlobalAllocator::Allocate(entry.size * sizeof(bool));
                static_cast<bool*>(entry.data)[0] = currentToken.extra8 == 2;
                return postProcess();
            }

            if(currentToken.extra8 == 4)
            {
                std::string_view mdBool = view;
                
                entry.type = Type::Bool;
                entry.size = std::ranges::count(mdBool, 'x');
                entry.data = GlobalAllocator::Allocate(entry.size * sizeof(bool));
                size_t cur = 0;

                bool bLastWasBoolLiteral = false;
                while(!mdBool.empty())
                {
                    if(mdBool.starts_with(KEYWORDS[2]))
                    {
                        if(bLastWasBoolLiteral)
                        {
                            entry.type = Type::Invalid;
                            return false;
                        }

                        bLastWasBoolLiteral = true;
                        static_cast<bool*>(entry.data)[cur++] = true;
                        mdBool = mdBool.substr(4);
                    }
                    else if(mdBool.starts_with(KEYWORDS[3]))
                    {
                        if(bLastWasBoolLiteral)
                        {
                            entry.type = Type::Invalid;
                            return false;
                        }

                        bLastWasBoolLiteral = true;
                        static_cast<bool*>(entry.data)[cur++] = false;
                        mdBool = mdBool.substr(5);
                    }
                    else if(mdBool.starts_with('x'))
                    {
                        if(!bLastWasBoolLiteral)
                        {
                            entry.type = Type::Invalid;
                            return false;
                        }

                        bLastWasBoolLiteral = false;
                        mdBool = mdBool.substr(1);
                    }
                    else
                    {
                        entry.type = Type::Invalid;
                        return false;
                    }
                }

                return postProcess();
            }

            return false;  // Invalid keyword when expected a value
        }




        if(currentToken.type == TokenType::EvaluateLiteral)
        {
            entry.size = EVALUATE_LITERAL_TEXT.size();
            entry.type = Type::String;
            entry.data = GlobalAllocator::Allocate(entry.size + 1 + sizeof(uint32_t));
            *static_cast<uint32_t*>(entry.data) = entry.size;
            constexpr_memcpy((static_cast<char*>(entry.data) + sizeof(uint32_t)), EVALUATE_LITERAL_TEXT.data(), EVALUATE_LITERAL_TEXT.size() + 1);
            (static_cast<char*>(entry.data) + sizeof(uint32_t))[EVALUATE_LITERAL_TEXT.size()] = '\0';

            auto DEBUG = std::string_view(static_cast<char*>(entry.data) + sizeof(uint32_t), entry.size);
            return postProcess();
        }



        
        entry.size = currentToken.extra8;  // Dimension count or string length




        if(currentToken.type == TokenType::IntLiteral)
        {
            entry.data = GlobalAllocator::Allocate(entry.size * sizeof(int64_t));
            
            bool bIsUnsigned = false;
            bool bContainsAnyNegative = false;
            bool bIsFirstChar = true;
            bool bIsNegative = false;

            uint64_t result = 0;
            uint8_t currentDimension = 0;
            const uint8_t dimensionCount = entry.size;

            auto finishDimension = [&]() -> bool
            {
                if(bIsNegative)
                {
                    if(bIsUnsigned || result > INT64_MAX_VALUE)
                        return false;

                    static_cast<int64_t*>(entry.data)[currentDimension] = -static_cast<int64_t>(result);
                }
                else
                {
                    const bool bWasUnsigned = bIsUnsigned;
                    if(result > INT64_MAX_VALUE)
                        bIsUnsigned = true;

                    if(bIsUnsigned)
                    {
                        if(bContainsAnyNegative)
                            return false;

                        if(!bWasUnsigned)
                        {
                            for(uint8_t i = 0; i < currentDimension - 1; i++)
                            {
                                const int64_t temp = static_cast<int64_t*>(entry.data)[i];
                                if(temp > INT64_MAX_VALUE)
                                    return false;
                                static_cast<uint64_t*>(entry.data)[i] = temp;
                            }
                        }

                        static_cast<uint64_t*>(entry.data)[currentDimension] = result;
                    }
                    else
                        static_cast<int64_t*>(entry.data)[currentDimension] = static_cast<int64_t>(result);
                }

                return true;
            };


            for(size_t i = 0; i < view.size(); i++)
            {
                char c = view[i];
                if(bIsFirstChar && c == '-')
                {
                    bIsNegative = true;
                    bContainsAnyNegative = true;
                }
                else if(std::isdigit(c))
                {
                    if(result > UINT64_MAX_VALUE / 10)
                        return false;  // Overflow

                    result *= 10;

                    const uint64_t digit = c - '0';
                    if(result > UINT64_MAX_VALUE - digit)
                        return false; // Overflow

                    result += digit;
                }
                else if(c == '.')
                {
                    while(i < view.size() && (view[i] == '.' || std::isdigit(view[i])))
                        i++;
                }
                else if(c == 'x')
                {
                    if(currentDimension >= dimensionCount - 1)
                        return false;  // Too much dimensions

                    if(!finishDimension())
                        return false;

                    bIsFirstChar = true;
                    bIsNegative = false;

                    result = 0;
                    currentDimension++;

                    continue;
                }
                else
                    return false;  // unknown character

                bIsFirstChar = false;
            }

            if(!finishDimension())
                return false;

            entry.type = bIsUnsigned? Type::UInt : Type::Int;
            auto DEBUG = std::span<int64_t>(static_cast<int64_t*>(entry.data), entry.size);
            return postProcess();
        }



        
        if(currentToken.type == TokenType::FloatLiteral)
        {
            entry.type = Type::Float;
            entry.data = GlobalAllocator::Allocate(entry.size * sizeof(double));

            bool bIsFirstChar = true;
            bool bIsNegative = false;
            bool bAfterDot = false;

            double multiplier = 1.0;
            double result = 0.0;
            uint8_t currentDimension = 0;
            const uint8_t dimensionCount = entry.size;

            for(char c : view)
            {
                if(bIsFirstChar && c == '-')
                {
                    bIsNegative = true;
                }
                else if(std::isdigit(c))
                {
                    if(result > DOUBLE_MAX_VALUE / 10)
                        return false;  // Overflow

                    if(!bAfterDot)
                        result *= 10;

                    const double value = static_cast<double>(c - '0') * multiplier;
                    if(result > DOUBLE_MAX_VALUE - value)
                        return false; // Overflow

                    result += value;
                }
                else if(c == '.')
                {
                    if(bAfterDot)
                        return false;  // Can't contain multiple dots

                    bAfterDot = true;
                }
                else if(c == 'x')
                {
                    if(currentDimension >= dimensionCount - 1)
                        return false;  // Too much dimensions

                    static_cast<double*>(entry.data)[currentDimension] = bIsNegative? -result : result;

                    bIsFirstChar = true;
                    bIsNegative = false;
                    bAfterDot = false;

                    multiplier = 1.0;
                    result = 0.0;
                    currentDimension++;

                    continue;
                }
                else
                    return false;  // unknown character

                bIsFirstChar = false;
                if(bAfterDot)
                    multiplier *= 0.1;
            }

            static_cast<double*>(entry.data)[currentDimension] = bIsNegative? -result : result;
            auto DEBUG = std::span<double>(static_cast<double*>(entry.data), entry.size);
            return postProcess();
        }
    
    
    
    
        if(currentToken.type == TokenType::VersionLiteral)
        {
            entry.type = Type::Version;
            entry.data = GlobalAllocator::Allocate(4 * sizeof(uint64_t));
            static_cast<uint64_t*>(entry.data)[3] = 0;
    
            uint8_t currentDimension = 0;
            const uint8_t dimensionCount = entry.size;
    
            uint64_t result = 0;
            for(char c : view)
            {
                if(std::isdigit(c))
                {
                    if(result > UINT64_MAX_VALUE / 10)
                        return false;  // Overflow
    
                    result *= 10;
    
                    const uint64_t digit = c - '0';
                    if(result > UINT64_MAX_VALUE - digit)
                        return false; // Overflow
    
                    result += digit;
                }
                else if(c == '.')
                {
                    if(currentDimension >= dimensionCount - 1)
                        return false;  // Too much dimensions
    
                    static_cast<uint64_t*>(entry.data)[currentDimension] = result;
    
                    result = 0;
                    currentDimension++;
                }
                else
                    return false;  // unknown character
            }
    
            static_cast<uint64_t*>(entry.data)[currentDimension] = result;
            auto DEBUG = std::span<uint64_t>(static_cast<uint64_t*>(entry.data), entry.size);
            return postProcess();
        }

        


        entry.data = GlobalAllocator::Allocate(entry.size + 1 + sizeof(uint32_t));
        *static_cast<uint32_t*>(entry.data) = entry.size;
        size_t size = 0;
        auto writeCharacter = [&](char c)
        {
            (static_cast<char*>(entry.data) + sizeof(uint32_t))[size++] = c;
        };

        if(currentToken.type == TokenType::StringLiteral)
        {
            entry.type = Type::String;

            const size_t start = 1;
            const size_t end = view.size() - 1;

            auto isEscapableChar     = [](char c) -> bool  { return c == '\"' || c == '\'' || c == '\\'; };
            auto isMergeEscapeChar   = [](char c) -> bool  { return c == 'n'  || c == 'r'  || c == 't' || c == 'v' || c == 'b' || c == 'f' || c == 'a'; };
            auto isUnicodeEscapeChar = [](char c) -> bool  { return c == 'u'  || c == 'U'; };  // TODO: Maybe handle unicode?

            auto convertMergedEscapeChar = [](char c) -> char
            {
                if(c == 'n')
                    return '\n';
                if(c == 'r')
                    return '\r';
                if(c == 't')
                    return '\t';
                if(c == 'v')
                    return '\v';
                if(c == 'b')
                    return '\b';
                if(c == 'f')
                    return '\f';
                if(c == 'a')
                    return '\a';
                return c;
            };

            for(int i = start; i < end; i++)
            {
                if(view[i] == '\\' && i + 1 < end && (isEscapableChar(view[i + 1]) || isMergeEscapeChar(view[i + 1])))
                {
                    i++;
                    entry.size--;

                    if(isEscapableChar(view[i]))
                        writeCharacter(view[i]);
                    else
                        writeCharacter(convertMergedEscapeChar(view[i]));
                }
                else
                {
                    writeCharacter(view[i]);
                }
            }
            writeCharacter('\0');

            auto DEBUG = std::string_view(static_cast<char*>(entry.data) + sizeof(uint32_t), entry.size);
            return postProcess();
        }




        if(currentToken.type == TokenType::HexLiteral || currentToken.type == TokenType::TimestampLiteral)
        {
            entry.type = currentToken.type == TokenType::HexLiteral? Type::Hex : Type::Timestamp;
            constexpr_memcpy((static_cast<char*>(entry.data) + sizeof(uint32_t)), view.data(), entry.size);
            (static_cast<char*>(entry.data) + sizeof(uint32_t))[entry.size] = '\0';

            auto DEBUG = std::string_view(static_cast<char*>(entry.data) + sizeof(uint32_t), entry.size);
            return postProcess();
        }

        return false;  // Something we didn't process yet?
    }
    template<auto DIAGNOSTIC_CALLBACK>
    [[nodiscard]] constexpr bool Utils<DIAGNOSTIC_CALLBACK>::ParseArray      (Tokenizer& tokenizer, Entry& array   FDF_COMMENT_SWITCH(, Token comment))
    {
        array.type = Type::Array;
        
        Token currentToken = tokenizer.Advance();
        FDF_CHECK_TOKEN(currentToken);
        FDF_CHECK_TOKEN_FOR_EOF(currentToken);

        while(true)
        {
        #if !FDF_NO_COMMENTS
            Token childComment;
        #endif
            while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
            {
            #if !FDF_NO_COMMENTS
                if(currentToken.type == TokenType::Comment)
                {
                    //if(childComment.type != TokenType::NonExisting)
                    //    if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", childComment.ToView(content), childComment.line, childComment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                    //        return false;
                    childComment = currentToken;
                }
            #endif

                currentToken = tokenizer.Advance();
                FDF_CHECK_TOKEN(currentToken);
                FDF_CHECK_TOKEN_FOR_EOF(currentToken);
            }


            if(IsValueLiteral(currentToken.type) || currentToken.type == TokenType::CurlyBraceOpen || currentToken.type == TokenType::SquareBraceOpen)
            {
                if(!ParseVariable(tokenizer, array   FDF_COMMENT_SWITCH(,childComment)))
                    return false;

                currentToken = tokenizer.Current();
                if(currentToken.type == TokenType::Comma)
                {
                    currentToken = tokenizer.Advance();
                    FDF_CHECK_TOKEN(currentToken);
                    FDF_CHECK_TOKEN_FOR_EOF(currentToken);
                }
            }
            else if(currentToken.type == TokenType::SquareBraceClose)
            {
                currentToken = tokenizer.Advance();
                FDF_CHECK_TOKEN(currentToken);

                if(currentToken.type == TokenType::Comment)
                {
                #if !FDF_NO_COMMENTS
                    //if(comment.type != TokenType::NonExisting)
                    //    if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                    //        return false;
                    comment = currentToken;
                #endif
                    currentToken = tokenizer.Advance();
                    FDF_CHECK_TOKEN(currentToken);
                }

                if(currentToken.type == TokenType::NewLine)
                {
                    currentToken = tokenizer.Advance();
                    FDF_CHECK_TOKEN(currentToken);
                }

            #if !FDF_NO_COMMENTS
                if(comment.type != TokenType::NonExisting)
                    array.SetComment(tokenizer.ToView(comment));
            #endif

                auto DEBUG = std::span<Entry*>((static_cast<Entry**>(array.data) + 1), array.size);
                return true;
            }
            else
                return false;
        }
    }
    template<auto DIAGNOSTIC_CALLBACK>
    [[nodiscard]] constexpr bool Utils<DIAGNOSTIC_CALLBACK>::ParseMap        (Tokenizer& tokenizer, Entry& map   FDF_COMMENT_SWITCH(, Token comment))
    {
        map.type = Type::Map;

        Token currentToken = tokenizer.Advance();
        FDF_CHECK_TOKEN(currentToken);
        FDF_CHECK_TOKEN_FOR_EOF(currentToken);

        while(true)
        {
        #if !FDF_NO_COMMENTS
            Token childComment;
        #endif
            while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
            {
            #if !FDF_NO_COMMENTS
                if(currentToken.type == TokenType::Comment)
                {
                    //if(childComment.type != TokenType::NonExisting)
                    //    if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", childComment.ToView(content), childComment.line, childComment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                    //        return false;
                    childComment = currentToken;
                }
            #endif

                currentToken = tokenizer.Advance();
                FDF_CHECK_TOKEN(currentToken);
                FDF_CHECK_TOKEN_FOR_EOF(currentToken);
            }


            if(currentToken.type == TokenType::Identifier)
            {
                if(!ParseVariable(tokenizer, map   FDF_COMMENT_SWITCH(,childComment)))
                    return false;

                currentToken = tokenizer.Current();
                if(currentToken.type == TokenType::Comma)
                {
                    currentToken = tokenizer.Advance();
                    FDF_CHECK_TOKEN(currentToken);
                    FDF_CHECK_TOKEN_FOR_EOF(currentToken);
                }
            }
            else if(currentToken.type == TokenType::CurlyBraceClose)
            {
                currentToken = tokenizer.Advance();
                FDF_CHECK_TOKEN(currentToken);

                if(currentToken.type == TokenType::Comment)
                {
                #if !FDF_NO_COMMENTS
                    //if(comment.type != TokenType::NonExisting)
                    //    if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                    //        return false;
                    comment = currentToken;
                #endif
                    currentToken = tokenizer.Advance();
                    FDF_CHECK_TOKEN(currentToken);
                }

                if(currentToken.type == TokenType::NewLine)
                {
                    currentToken = tokenizer.Advance();
                    FDF_CHECK_TOKEN(currentToken);
                }

            #if !FDF_NO_COMMENTS
                if(comment.type != TokenType::NonExisting)
                    map.SetComment(tokenizer.ToView(comment));
            #endif

                auto DEBUG = std::span<Entry*>((static_cast<Entry**>(map.data) + 1), map.size);
                return true;
            }
            else
                return false;
        }
    }
}










#undef FDF_EXPORT
#undef FDF_CHECK_TOKEN
#undef FDF_CHECK_TOKEN_FOR_EOF
#undef FDF_FORWARD_ERROR
#undef FDF_COMMENT_SWITCH
