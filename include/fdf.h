
#pragma once

#if !FDF_USE_CPP_MODULES
    #include <algorithm>
    #include <cassert>
    #include <cctype>
    #include <cstdint>
    #include <filesystem>
    #include <format>
    #include <fstream>
    #include <ranges>
    #include <span>
    #include <stack>
    #include <string>
    #include <type_traits>
    #include <utility>
    #include <vector>

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
    
    enum class DiagnosticSeverity : uint8_t
    {
        None,
        Info,
        Warning,
        Error,
        Fatal,
    };

    enum class DiagnosticType : uint8_t
    {
        AlreadyHasComment,
        UnexpectedToken,
    };

    class Entry;
}





namespace fdf::detail
{
    struct Test;

    template<auto DIAGNOSTIC_CALLBACK = nullptr>
    struct Utils;
    
    template<typename T>
    concept IsValidIDType = std::integral<std::remove_cvref_t<T>> || std::convertible_to<std::remove_cvref_t<T>, std::string_view>;
    
    template<typename Callable>
    constexpr bool IsValidDiagnosticCallback = std::is_invocable_v<Callable, DiagnosticSeverity, DiagnosticType, std::string_view>;

    constexpr size_t MAX_IDENTIFIER_LENGTH = FDF_NO_COMMENTS && FDF_EXTENDED_NO_COMMENT_IDENTIFIERS? 40 : 32;
    
    struct EntryDeleter { static constexpr void operator()(Entry* e) noexcept; };
}





FDF_EXPORT namespace fdf
{
    namespace ForEachFlags
    {
        enum Flag : uint8_t
        {
            None = 0,
            Recursive   = 1 << 0,
            Group       = 1 << 1,
            IncludeSelf = 1 << 2,
            All = Recursive | Group | IncludeSelf
        };

        constexpr bool IsSet(std::underlying_type_t<Flag> flags, std::underlying_type_t<Flag> checkFlag)    noexcept  { return (flags & checkFlag) != Flag::None; }
        constexpr bool IsNotSet(std::underlying_type_t<Flag> flags, std::underlying_type_t<Flag> checkFlag) noexcept  { return (flags & checkFlag) == Flag::None; }
        constexpr bool IsValidForEachFlag(std::underlying_type_t<Flag> flags)                               noexcept  { return (flags & ~Flag::All) == 0; }
    }





    using UniqueEntryPtr = std::unique_ptr<Entry, detail::EntryDeleter>;
    class Entry
    {
    public:
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        
        constexpr Entry(Entry&& other) noexcept;
        constexpr Entry& operator=(Entry&& other) noexcept;
        
    private:
        constexpr  Entry() noexcept
        {
            SetIdentifierSize(0);
        }
        constexpr ~Entry() noexcept;

        friend struct detail::Test;

        template<auto DIAGNOSTIC_CALLBACK>
        friend struct detail::Utils;

        struct CommentControlBlock { uint32_t capacity, size; };

    private:
        char identifier[detail::MAX_IDENTIFIER_LENGTH + 1] = {};
        Type type = Type::Invalid;
        uint8_t depth = 0;
        uint8_t unused = 0;
        uint32_t size = 0;
        Entry* parent = nullptr;
        void* data = nullptr;
    #if !FDF_NO_COMMENTS
        void* comment = nullptr;
    #endif

    private:
        template<typename T>
        [[nodiscard]] constexpr       std::vector<T>* GetDataVector()       noexcept  { return static_cast<      std::vector<T>*>(data); }
        template<typename T>
        [[nodiscard]] constexpr const std::vector<T>* GetDataVector() const noexcept  { return static_cast<const std::vector<T>*>(data); }

        template<typename T>
        [[nodiscard]] constexpr       T* GetDataAs()       noexcept  { return static_cast<      T*>(data); }
        template<typename T>
        [[nodiscard]] constexpr const T* GetDataAs() const noexcept  { return static_cast<const T*>(data); }
        
        
        [[nodiscard]] constexpr       CommentControlBlock* GetCommentControlBlock()       noexcept  { return static_cast<      CommentControlBlock*>(comment); }
        [[nodiscard]] constexpr const CommentControlBlock* GetCommentControlBlock() const noexcept  { return static_cast<const CommentControlBlock*>(comment); }
        [[nodiscard]] constexpr       char*                GetCommentData()               noexcept  { return static_cast<      char*>(comment) + sizeof(CommentControlBlock); }
        [[nodiscard]] constexpr const char*                GetCommentData()         const noexcept  { return static_cast<const char*>(comment) + sizeof(CommentControlBlock); }

        [[nodiscard]] constexpr       std::string* GetCommentString()       noexcept  { return static_cast<      std::string*>(data); }
        [[nodiscard]] constexpr const std::string* GetCommentString() const noexcept  { return static_cast<const std::string*>(data); }
        
        [[nodiscard]] constexpr uint8_t GetIdentifierSize()                    const noexcept  { return static_cast<uint8_t>(detail::MAX_IDENTIFIER_LENGTH) - static_cast<uint8_t>(identifier[detail::MAX_IDENTIFIER_LENGTH]); }
                      constexpr void    SetIdentifierSize(const uint8_t value)       noexcept  { identifier[detail::MAX_IDENTIFIER_LENGTH] = static_cast<char>(detail::MAX_IDENTIFIER_LENGTH - value); }

    public:
        [[nodiscard]] constexpr uint32_t GetChildCount() const noexcept  { return IsContainer()? size : 0; }
        [[nodiscard]] constexpr uint8_t  GetDepth()      const noexcept  { return depth; }
        [[nodiscard]] constexpr Type     GetType()       const noexcept  { return type; }
        [[nodiscard]] constexpr bool     IsValid()       const noexcept  { return type != Type::Invalid; }
        [[nodiscard]] constexpr bool     IsNull()        const noexcept  { return type == Type::Null; }
        [[nodiscard]] constexpr bool     IsNil()         const noexcept  { return IsNull(); }
        [[nodiscard]] constexpr bool     IsContainer()   const noexcept  { return type == Type::Array || type == Type::Map; }
        [[nodiscard]] constexpr bool     HasValue()      const noexcept  { return IsValid() && !IsNull() && !IsContainer(); }
        [[nodiscard]] constexpr Entry*   GetParent()           noexcept  { return parent; }
        [[nodiscard]] constexpr Entry*   GetParent()     const noexcept  { return parent; }

        [[nodiscard]] constexpr std::string_view GetIdentifier() const noexcept  { return {identifier, GetIdentifierSize()}; }
        [[nodiscard]] constexpr std::string GetFullIdentifier() const noexcept
        {
            const Entry* cur = parent;
            std::string temp = std::string(GetIdentifier());
            while(cur)
            {
                if(cur->depth == static_cast<uint8_t>(-1))
                {
                    assert(cur->parent == nullptr && "If depth is '-1', it should be head!");
                    return temp;
                }
                
                temp = std::format("{}.{}", cur->GetIdentifier(), temp);
                cur = cur->parent;
            }
            return temp;
        }

        [[nodiscard]] constexpr std::string_view GetComment() const noexcept
        {
            #if !FDF_NO_COMMENTS
                if consteval
                {
                    if(comment)
                        return *GetCommentString();
                }
                else
                {
                    if(comment)
                        return {GetCommentData(), GetCommentControlBlock()->size};
                }
            #endif
                return {};
        }

    public:
        constexpr bool SetIdentifier(std::string_view newIdentifier) noexcept;
        constexpr void SetComment(std::string_view newComment) noexcept;
        constexpr void ReleaseData() noexcept;
        constexpr void ReleaseComment() noexcept;
        constexpr void ReleaseEverything() noexcept;

    private:
        [[nodiscard]] constexpr Entry* Emplace() noexcept;
    public:
        [[nodiscard]] constexpr Entry* AddChild(UniqueEntryPtr& e) noexcept;
        [[nodiscard]] constexpr bool   RemoveChild(Entry& e) noexcept;
        [[nodiscard]] constexpr bool   RemoveChild(std::string_view _identifier) noexcept;
        [[nodiscard]] constexpr bool   RemoveChild(uint32_t index) noexcept;
        [[nodiscard]] constexpr bool   ClearChildren() noexcept;
        
        [[nodiscard]] constexpr UniqueEntryPtr OrphanChild(Entry& e) noexcept;
        [[nodiscard]] constexpr UniqueEntryPtr OrphanChild(std::string_view _identifier) noexcept;
        [[nodiscard]] constexpr UniqueEntryPtr OrphanChild(uint32_t index) noexcept;
        [[nodiscard]] constexpr std::vector<UniqueEntryPtr> OrphanChildren() noexcept;
        
    private:
        [[nodiscard]] constexpr Entry* OrphanChild_INTERNAL(Entry& e) noexcept;
        [[nodiscard]] constexpr Entry* OrphanChild_INTERNAL(std::string_view _identifier) noexcept;
        [[nodiscard]] constexpr Entry* OrphanChild_INTERNAL(uint32_t index) noexcept;
        [[nodiscard]] constexpr std::span<Entry*> OrphanChildren_INTERNAL() noexcept;

    public:
        template<detail::IsValidIDType T, detail::IsValidIDType... Args>
        [[nodiscard]] constexpr       Entry* GetChild(T&& param, Args&&... args) noexcept;
        template<detail::IsValidIDType T, detail::IsValidIDType... Args>
        [[nodiscard]] constexpr const Entry* GetChild(T&& param, Args&&... args) const noexcept;

        [[nodiscard]] constexpr       Entry* GetDirectChild(std::string_view _identifier) noexcept;
        [[nodiscard]] constexpr const Entry* GetDirectChild(std::string_view _identifier) const noexcept;
        [[nodiscard]] constexpr       Entry* GetDirectChild(uint32_t index) noexcept;
        [[nodiscard]] constexpr const Entry* GetDirectChild(uint32_t index) const noexcept;
        
        [[nodiscard]] constexpr std::span<Entry*>         GetChildren() noexcept;
        [[nodiscard]] constexpr std::span<const Entry*>   GetChildren() const noexcept;
        [[nodiscard]] constexpr std::vector<Entry*>       GetChildrenRecursive() noexcept;
        [[nodiscard]] constexpr std::vector<const Entry*> GetChildrenRecursive() const noexcept;
        [[nodiscard]] constexpr size_t                    GetChildCountRecursive() const noexcept;
    
    private:
        [[nodiscard]] constexpr std::span<Entry*>         GetChildren_INTERNAL() noexcept;
        [[nodiscard]] constexpr std::span<const Entry*>   GetChildren_INTERNAL() const noexcept;


    public:
        template<std::underlying_type_t<ForEachFlags::Flag> FLAGS = ForEachFlags::None, typename Callable> requires(std::is_invocable_v<Callable, Entry&>)
        constexpr void ForEach(Callable callback) noexcept(std::is_nothrow_invocable_v<Callable, Entry&>);
        template<std::underlying_type_t<ForEachFlags::Flag> FLAGS = ForEachFlags::None, typename Callable> requires(std::is_invocable_v<Callable, const Entry&>)
        constexpr void ForEach(Callable callback) const noexcept(std::is_nothrow_invocable_v<Callable, const Entry&>);

    public:
        template<typename T>
        [[nodiscard]] constexpr auto GetValue()       noexcept       { static_assert(false, "Invalid type"); }
        template<typename T>
        [[nodiscard]] constexpr auto GetValue() const noexcept       { static_assert(false, "Invalid type"); }

        template<Style STYLE = {}>
        [[nodiscard]] constexpr std::string_view DataToView(std::string& temp) const noexcept;

    public:
        template<auto DIAGNOSTIC_CALLBACK = nullptr>
        [[nodiscard]] bool ParseCombineFile(const std::filesystem::path& filepath, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseNewIfExistingIsEmpty);
        template<auto DIAGNOSTIC_CALLBACK = nullptr>
        [[nodiscard]] constexpr bool ParseCombineBuffer(std::string_view content, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseNewIfExistingIsEmpty) noexcept;
        [[nodiscard]] constexpr bool Combine(UniqueEntryPtr& other, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseNewIfExistingIsEmpty) noexcept;

    public:
        template<auto DIAGNOSTIC_CALLBACK = nullptr>
        [[nodiscard]] static UniqueEntryPtr ParseFile(const std::filesystem::path& filepath);
        template<auto DIAGNOSTIC_CALLBACK = nullptr>
        [[nodiscard]] static constexpr UniqueEntryPtr ParseBuffer(std::string_view content) noexcept;
        
        template<Style STYLE = {}>
        [[nodiscard]] static bool WriteFile(const Entry& e, const std::filesystem::path& filepath, bool bCreateIfNotExists = true);
        template<Style STYLE = {}>
        static constexpr void WriteBuffer(const Entry& root, std::string& buffer) noexcept;
    };
}




namespace fdf::detail
{
    constexpr std::string_view EVALUATE_LITERAL_TEXT = "Evaluate Literal";
    constexpr std::string_view INVALID_TEXT = "<INVALID>";
    constexpr std::string_view UNEXPECTED_TEXT = "<UNEXPECTED-ERROR>";
    constexpr std::string_view ARRAY_TEXT   = "<ARRAY>";
    constexpr std::string_view MAP_TEXT     = "<MAP>";

    constexpr auto INT64_MAX_VALUE  = std::numeric_limits< int64_t>::max();
    constexpr auto UINT64_MAX_VALUE = std::numeric_limits<uint64_t>::max();
    constexpr auto DOUBLE_MAX_VALUE = std::numeric_limits<  double>::max();

    #if FDF_NO_COMMENTS
        constexpr size_t DATA_OVERHEAD_SIZE = sizeof(size_t);
    #else
        constexpr size_t DATA_OVERHEAD_SIZE = sizeof(size_t) + sizeof(void*);
    #endif

    constexpr size_t INITIAL_PARENT_DATA_SIZE = DATA_OVERHEAD_SIZE + (4 * sizeof(void*));


    constexpr std::string_view KEYWORDS[] =
    {
        "null", "nil",
        "true", "false", " MD_BOOL_PLACEHOLDER "
    };
    constexpr size_t KEYWORD_COUNT = std::size(KEYWORDS);

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
        constexpr Token(TokenType type_, uint32_t startPosition_ = 0, uint32_t count_ = 0) noexcept
            : type(type_), count(count_), startPosition(startPosition_)  { }

        TokenType type = TokenType::NonExisting;
        uint8_t  extra8  = 0; // Token specific data
        uint16_t column = 0;
        uint32_t count = 0;
        uint32_t startPosition = 0;
        uint32_t line = 0;
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
        uint32_t index;
        uint32_t line;
        uint32_t lastNewLineIndex;
        Token currentToken;
    };
}




namespace fdf::detail
{
    constexpr void constexpr_memcpy(char* dest, const char* src, size_t size) noexcept
    {
        for(size_t i = 0; i < size; i++)
            dest[i] = src[i];
    }

    [[nodiscard]] constexpr bool IsValueLiteral(TokenType type) noexcept
    { 
        return static_cast<uint8_t>(type) >= static_cast<uint8_t>(TokenType::ValueLiteral_Begin) &&
               static_cast<uint8_t>(type) <= static_cast<uint8_t>(TokenType::ValueLiteral_End);
    }

    [[nodiscard]] constexpr bool constexpr_isspace(char c) noexcept
    {
        return (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r');
    }

    [[nodiscard]] constexpr bool constexpr_isalpha(char c) noexcept
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    [[nodiscard]] constexpr bool constexpr_isdigit(char c) noexcept
    {
        return c >= '0' && c <= '9';
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
            Chunk() noexcept
                : freeList(data), used(0), next(nullptr), data{}
            {
                U* cur = freeList;
                U* nex = cur + 1;
                U* end = data + ELEMENT_COUNT;
                
                while(nex < end)
                {
                    cur->p = nex;
                    cur = nex++;
                }

                cur->p = nullptr;
            }

            [[nodiscard]] bool Owns(const void* ptr) noexcept
            {
                return static_cast<void*>(data) <= ptr && ptr < static_cast<void*>(data + ELEMENT_COUNT);
            }

            [[nodiscard]]        bool IsEmpty()        const noexcept  { return used == 0; }
            [[nodiscard]]        bool HasSpace()       const noexcept  { return used < ELEMENT_COUNT; }
            [[nodiscard]] static U*   ToUnion(void* e)       noexcept  { return static_cast<U*>(e); }

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

         SlabAllocator() noexcept = default;
        ~SlabAllocator() noexcept
        {
            while(head)
            {
                Chunk* temp = head->next;
                delete head;
                head = temp;
            }
        }




        [[nodiscard]] void* Allocate() noexcept
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

            Chunk* newChunk = new (std::nothrow) Chunk();
            assert(newChunk && "Allocation shouldn't fail");
            newChunk->next = head;
            head = newChunk;
            return newChunk->Allocate();
        }

        [[nodiscard]] bool Deallocate(void* ptr) noexcept
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
        [[nodiscard]] T* Create(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            static_assert(sizeof(T) <= BLOCK_SIZE, "T is too large for this SlabAllocator.");
            return new(Allocate()) T(std::forward<Args>(args)...);
        }

        template<typename T>
        [[nodiscard]] bool Destroy(T* obj) noexcept(std::is_nothrow_destructible_v<T>)
        {
            static_assert(sizeof(T) <= BLOCK_SIZE, "T is too large for this SlabAllocator.");
            obj->~T();
            return Deallocate(obj);
        }
    };










    class GlobalAllocator
    {
        template<auto DIAGNOSTIC_CALLBACK>
        friend struct detail::Utils;
        
        inline static constinit SlabAllocator<8, 8, 4096>  B8;
        inline static constinit SlabAllocator<16, 8, 4096> B16;
        inline static constinit SlabAllocator<32, 8, 4096> B32;
        inline static constinit SlabAllocator<64, 8, 4096> B64;
        inline static constinit SlabAllocator<sizeof(Entry), alignof(Entry), 4096>  ENTRY_ALLOCATOR;

    public:
        static void* Allocate(size_t size) noexcept
        {
            if(size <= 8)
                return B8.Allocate();
            if(size <= 16)
                return B16.Allocate();
            if(size <= 32)
                return B32.Allocate();
            if(size <= 64)
                return B64.Allocate();
            
            void* p = ::operator new(size, std::nothrow);
            assert(p && "Allocation shouldn't fail");
            return p;
        }

        static bool Deallocate(void* p, size_t size) noexcept
        {
            if(size <= 8)
                return B8.Deallocate(p);
            if(size <= 16)
                return B16.Deallocate(p);
            if(size <= 32)
                return B32.Deallocate(p);
            if(size <= 64)
                return B64.Deallocate(p);
            
            ::operator delete(p);
            return true;
        }




        template<size_t size>
        static void* Allocate() noexcept
        {
            if constexpr(size <= 8)
                return B8.Allocate();
            else if constexpr(size <= 16)
                return B16.Allocate();
            else if constexpr(size <= 32)
                return B32.Allocate();
            else if constexpr(size <= 64)
                return B64.Allocate();
            else
            {
                void* p = ::operator new(size, std::nothrow);
                assert(p && "Allocation shouldn't fail");
                return p;
            }
        }

        template<size_t size>
        static bool Deallocate(void* p) noexcept
        {
            if constexpr(size <= 8)
                return B8.Deallocate(p);
            else if constexpr(size <= 16)
                return B16.Deallocate(p);
            else if constexpr(size <= 32)
                return B32.Deallocate(p);
            else if constexpr(size <= 64)
                return B64.Deallocate(p);
            else
            {
                ::operator delete(p);
                return true;
            }
        }




        template<typename T, typename... Args>
        [[nodiscard]] static T* Create(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            if constexpr(sizeof(T) <= 8)
                return B8.Create<T>(std::forward<Args>(args)...);
            else if constexpr(sizeof(T) <= 16)
                return B16.Create<T>(std::forward<Args>(args)...);
            else if constexpr(sizeof(T) <= 32)
                return B32.Create<T>(std::forward<Args>(args)...);
            else if constexpr(sizeof(T) <= 64)
                return B64.Create<T>(std::forward<Args>(args)...);
            else
            {
                T* p = new (std::nothrow) T(std::forward<Args>(args)...);
                assert(p && "Allocation shouldn't fail");
                return p;
            }
        }

        template<typename T>
        [[nodiscard]] static bool Destroy(T* obj) noexcept(std::is_nothrow_destructible_v<T>)
        {
            if constexpr(sizeof(T) <= 8)
                return B8.Destroy(obj);
            else if constexpr(sizeof(T) <= 16)
                return B16.Destroy(obj);
            else if constexpr(sizeof(T) <= 32)
                return B32.Destroy(obj);
            else if constexpr(sizeof(T) <= 64)
                return B64.Destroy(obj);
            else
            {
                delete obj;
                return true;
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

        while(constexpr_isspace(content[index]))
        {
            if(content[index] == '\n')
            {
                Token token = Token(TokenType::NewLine, index);
                token.line = line;
                token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                while(index < content.size() && constexpr_isspace(content[index]))
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

            if(constexpr_isspace(content[index]))
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

            Token token = Token(TokenType::StringLiteral, index, static_cast<uint32_t>(nextQuote) + 1 - index);
            index = static_cast<uint32_t>(nextQuote + 1);
            token.line = line;
            token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
            token.extra8 = static_cast<uint8_t>(token.count - 2);
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
                token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);

                if(newLinePos != std::string_view::npos)
                {
                    token.count = static_cast<uint32_t>(newLinePos - token.startPosition);
                    index = static_cast<uint32_t>(newLinePos);
                    return token;
                }

                // There is no new lines left (comment is at the end of the file)
                token.count = static_cast<uint32_t>(content.size() - token.startPosition);
                index = static_cast<uint32_t>(-1);
                return token;
            }

            if(content[index + 1] == '*') // multi line comment
            {
                size_t slashPos = content.find_first_of('/', index + 2);
                while(true)
                {
                    if(slashPos == std::string_view::npos)
                        return TokenType::Invalid; // Non-matching comment scope (There is only "/*" and not "*/")

                    if(content[slashPos - 1] == '*')
                    {
                        Token token = Token(TokenType::Comment, index + 2);
                        token.line = line;
                        token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                        token.extra8 = 1;  // Means multi line
                        token.count = static_cast<uint32_t>(slashPos - 2 - token.startPosition);

                        for(size_t i = index + 2; i < slashPos - 1; i++)
                        {
                            if(content[i] == '\n')
                                line++;
                        }

                        index = static_cast<uint32_t>(slashPos + 1);
                        if(index + 1 < content.size() && content[index] == '\n')
                        {
                            lastNewLineIndex = index;
                            line++;
                            index++;
                        }

                        if(token.count == static_cast<uint32_t>(-1))
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

                Token token = Token(TokenType::EvaluateLiteral, index, static_cast<uint32_t>(braceClose) + 1 - index);
                token.line = line;
                token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                index = static_cast<uint32_t>(braceClose + 1);
                return token;
            }

            return TokenType::Invalid; // Random "$" without "{"
        }



        if(constexpr_isalpha(content[index]) || content[index] == '_') // identifier, keyword
        {
            Token token = Token(TokenType::Identifier, index);
            token.line = line;
            token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
            auto checkKeywords = [&](std::string_view view) -> void
            {
                for(size_t i = 0; i < KEYWORD_COUNT; i++)
                {
                    if(view == KEYWORDS[i])
                    {
                        token.type = TokenType::Keyword;
                        token.extra8 = static_cast<uint8_t>(i);  // Used as keyword index
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
            while(firstNonAlpha < content.size() && (constexpr_isalpha(content[firstNonAlpha]) || constexpr_isdigit(content[firstNonAlpha]) || content[firstNonAlpha] == '_'))
                firstNonAlpha++;

            token.count = static_cast<uint32_t>(firstNonAlpha) - token.startPosition;
            const std::string_view view = ToView(token);

            if(firstNonAlpha >= content.size()) // we reached eof before any space or any other token
            {
                checkKeywords(view);
                index = static_cast<uint32_t>(-1);
                return token;
            }

            checkKeywords(view);
            index = static_cast<uint32_t>(firstNonAlpha);
            return token;
        }



        if(constexpr_isdigit(content[index]) || content[index] == '-')
        {
            if(content[index] == '0' && index + 3 < content.size() && content[index + 1] == 'x')  // Hex
            {
                const size_t firstNonHex = content.find_first_not_of("0123456789abcdefABCDEF", index + 2);
                const size_t firstChar = content.find_first_of("abcdefABCDEF", index + 2);
                const size_t firstHash = content.find_first_of('#', index + 2);
                if(firstNonHex == firstHash && firstNonHex != std::string_view::npos) // First non-hex character is "#"
                {
                    Token token = Token(TokenType::HexLiteral, index, static_cast<uint32_t>(firstNonHex) - index);
                    token.line = line;
                    token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                    index = static_cast<uint32_t>(firstNonHex + 1);
                    token.extra8 = static_cast<uint8_t>(token.count);
                    return token;
                }

                if(firstNonHex == std::string_view::npos) // we reached eof before any space or any other token
                    return TokenType::Invalid;

                if(firstChar < firstNonHex) // it contains hex characters, so we can't let it slide as a number
                    return TokenType::Invalid;

                // Let it fallthrough as "multidimensional int"
            }



            size_t firstNonDigit = content.find_first_not_of("0123456789", index + 1);
            if(firstNonDigit == std::string_view::npos)  // we reached eof before any space or any other token
            {
                Token token = Token(TokenType::IntLiteral, index, static_cast<uint32_t>(content.size()) - index);
                token.line = line;
                token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc.)
                index = static_cast<uint32_t>(-1);
                return token;
            }

            if(constexpr_isspace(content[firstNonDigit]) || content[firstNonDigit] == ',')
            {
                Token token = Token(TokenType::IntLiteral, index, static_cast<uint32_t>(firstNonDigit) - index);
                token.line = line;
                token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc.)
                index = static_cast<uint32_t>(firstNonDigit);
                return token;
            }

            if(content[firstNonDigit] == '.')  // float, version or multi-dimensional float
            {
                Token token = Token(TokenType::FloatLiteral, index);
                token.line = line;
                token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc.)

                uint8_t dotCount = 0;
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

                    token.count = static_cast<uint32_t>(temp) - token.startPosition;

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
                    if(constexpr_isdigit(content[temp]) || (content[temp] == '-' && lastChar == 'x'))
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

                    if(constexpr_isspace(content[temp]) || content[temp] == ',')
                    {
                        calculateResult();
                        index = static_cast<uint32_t>(temp);
                        return token;
                    }

                    return TokenType::Invalid; // Non allowed character
                }

                calculateResult();
                index = static_cast<uint32_t>(-1);
                return token;
            }

            if(content[firstNonDigit] == 'x')  // multi-dimensional int
            {
                Token token = Token(TokenType::IntLiteral, index);
                token.line = line;
                token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                token.extra8 = 2;  // Used as dimension (2d, 3d, 4d, 5d, etc.)

                size_t dotCount = 0;
                while(true)
                {
                    const size_t previous = firstNonDigit;
                    firstNonDigit = content.find_first_not_of("0123456789", firstNonDigit + 1);

                    if(firstNonDigit == std::string_view::npos) // we reached eof before any space or any other token
                    {
                        token.count = static_cast<uint32_t>(content.size()) - token.startPosition;
                        index = static_cast<uint32_t>(-1);
                        return token;
                    }

                    if(previous + 1 == firstNonDigit && (content[previous] != ',' || !constexpr_isspace(content[firstNonDigit])) && (content[previous] != 'x' || content[firstNonDigit] != '-'))
                        return TokenType::Invalid;  // It must have number(s) in between

                    if(constexpr_isspace(content[firstNonDigit]) || content[firstNonDigit] == ',')
                    {
                        token.count = static_cast<uint32_t>(firstNonDigit - token.startPosition);
                        index = static_cast<uint32_t>(firstNonDigit);
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
                            return TokenType::Invalid;  // Multidimensional numbers can't contain more than 1 dot (for each number)

                        continue;
                    }
                }
            }





            /* Possible datetime formats (ISO 8601)
            *  2024-12-24T15:30:00       -> Date + Time without timezone info (Usually interpreted as local time)
            *  2024-12-24T15:30:00Z      -> Date + Time with timezone info (Z means utc/zulu time)
            *  2024-12-24T15:30:00+05:30 -> Date + Time with timezone info (5 hours and 30 minutes ahead of UTC)
            *  2024-12-24                -> Date
            *  15:30:00                  -> Time
            *  2024-12-24T15:30:00.123Z  -> Date + Time with timezone info (Z means utc/zulu time) and milliseconds (123ms)
            *  2024-W52-2                -> Year + Week + Weekday (52nd week of 2024, tuesday)
            *  2024-359                  -> Year + Day of Year (359th day of 2024)
            */

            /* Possible duration formats (if we want to support it, currently we don't) (Note: not here, it starts with a letter)
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
                token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                const size_t firstNonDate = content.find_first_not_of("0123456789TZW-+:.", index);
                if(firstNonDate == std::string_view::npos)
                {
                    token.count = static_cast<uint32_t>(content.size()) - token.startPosition;
                    token.extra8 = static_cast<uint8_t>(token.count);
                    index = static_cast<uint32_t>(-1);
                    return token;
                }

                if(constexpr_isspace(content[firstNonDate]) || content[firstNonDate] == ',')
                {
                    token.count = static_cast<uint32_t>(firstNonDate - token.startPosition);
                    token.extra8 = static_cast<uint8_t>(token.count);
                    index = static_cast<uint32_t>(firstNonDate);
                    return token;
                }

                return TokenType::Invalid;  // Invalid character after timestamp
            }

            if(content[firstNonDigit] == ':')  // time
            {
                Token token = Token(TokenType::TimestampLiteral, index);
                token.line = line;
                token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                const size_t firstNonDate = content.find_first_not_of("0123456789+:.", index);  // idk if it can include timezone ("+" sign)
                if(firstNonDate == std::string_view::npos)
                {
                    token.count = static_cast<uint32_t>(content.size()) - token.startPosition;
                    index = static_cast<uint32_t>(-1);
                    token.extra8 = static_cast<uint8_t>(token.count);
                    return token;
                }

                if(constexpr_isspace(content[firstNonDate]) || content[firstNonDate] == ',')
                {
                    token.count = static_cast<uint32_t>(firstNonDate - token.startPosition);
                    index = static_cast<uint32_t>(firstNonDate);
                    token.extra8 = static_cast<uint8_t>(token.count);
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
        [[nodiscard]] static constexpr UniqueEntryPtr Create(Args&&... args) noexcept;
                      static constexpr void           Destroy(Entry* e) noexcept;

        [[nodiscard]] static constexpr UniqueEntryPtr ParseBuffer(std::string_view content) noexcept;
        [[nodiscard]] static constexpr bool           ParseVariable   (Tokenizer& tokenizer, Entry& parent   FDF_COMMENT_SWITCH(, Token comment)) noexcept;
        [[nodiscard]] static constexpr bool           ParseSimpleValue(Tokenizer& tokenizer, Entry& entry    FDF_COMMENT_SWITCH(, Token comment)) noexcept;
        [[nodiscard]] static constexpr bool           ParseArray      (Tokenizer& tokenizer, Entry& array    FDF_COMMENT_SWITCH(, Token comment)) noexcept;
        [[nodiscard]] static constexpr bool           ParseMap        (Tokenizer& tokenizer, Entry& map      FDF_COMMENT_SWITCH(, Token comment)) noexcept;

        template<Style STYLE>
        static constexpr void WriteBuffer(const Entry& root, std::string& buffer, bool bOverwrite = true) noexcept;
    };
}










namespace fdf
{
    constexpr Entry::Entry(Entry&& other) noexcept
    {
        type = other.type;
        depth = other.depth;
        size = other.size;
        parent = other.parent;
        data = other.data;
            
        other.type = Type::Invalid;
        other.parent = nullptr;
        other.data = nullptr;
        
        detail::constexpr_memcpy(identifier, other.identifier, detail::MAX_IDENTIFIER_LENGTH + 1);
            
#if !FDF_NO_COMMENTS
        comment = other.comment;
        other.comment = nullptr;
#endif
    }
    
    constexpr Entry& Entry::operator=(Entry&& other) noexcept
    {
        if(parent)
            (void)parent->OrphanChild(*this);
        ReleaseData();
        ReleaseComment();
        
        type = other.type;
        depth = other.depth;
        size = other.size;
        parent = other.parent;
        data = other.data;
            
        other.type = Type::Invalid;
        other.parent = nullptr;
        other.data = nullptr;
            
        detail::constexpr_memcpy(identifier, other.identifier, detail::MAX_IDENTIFIER_LENGTH + 1);
            
#if !FDF_NO_COMMENTS
        comment = other.comment;
        other.comment = nullptr;
#endif
        
        return *this;
    }
    
    
    constexpr Entry::~Entry() noexcept
    {
        if(parent)
            (void)parent->OrphanChild_INTERNAL(*this);
        ReleaseData();
        ReleaseComment();
    }
    
    
    constexpr bool Entry::SetIdentifier(std::string_view newIdentifier) noexcept
    {
        if(newIdentifier.size() > detail::MAX_IDENTIFIER_LENGTH)
            return false;
        SetIdentifierSize(static_cast<uint8_t>(newIdentifier.size()));
        detail::constexpr_memcpy(identifier, newIdentifier.data(), GetIdentifierSize());
        identifier[GetIdentifierSize()] = '\0';
        return true;
    }

    constexpr void Entry::SetComment(std::string_view newComment) noexcept
    {
        if consteval
        {
            if(comment)
                *GetCommentString() = newComment;
            else
            {
                comment = new (std::nothrow) std::string(newComment);
                assert(comment && "Allocation shouldn't fail");
            }
        }
        else
        {
            if(comment)
            {
                if(newComment.size() > GetCommentControlBlock()->capacity)
                {
                    detail::GlobalAllocator::Deallocate(comment, GetCommentControlBlock()->capacity + sizeof(Entry::CommentControlBlock) + 1);
                    comment = static_cast<CommentControlBlock*>(detail::GlobalAllocator::Allocate(newComment.size() + sizeof(Entry::CommentControlBlock) + 1));
                    GetCommentControlBlock()->capacity = 0;
                    GetCommentControlBlock()->size = 0;
                }
                else
                {
                    GetCommentControlBlock()->size = 0;
                }
            }
            else
            {
                comment = static_cast<CommentControlBlock*>(detail::GlobalAllocator::Allocate(newComment.size() + sizeof(Entry::CommentControlBlock) + 1));
                GetCommentControlBlock()->capacity = static_cast<uint32_t>(newComment.size());
                GetCommentControlBlock()->size = 0;
            }

            detail::constexpr_memcpy(GetCommentData(), newComment.data(), newComment.size());
            GetCommentData()[newComment.size()] = '\0';
        }
    }

    constexpr void Entry::ReleaseData() noexcept
    {
        switch(type)
        {
        case Type::Bool:
            if consteval
                { delete[] GetDataAs<bool>(); }
            else
                { detail::GlobalAllocator::Deallocate(data, size * sizeof(bool)); }
            break;
        case Type::Int:
            if consteval
                { delete GetDataVector<int64_t>(); }
            else
                { detail::GlobalAllocator::Deallocate(data, size * sizeof(int64_t)); }
            break;
        case Type::UInt:
            if consteval
                { delete GetDataVector<uint64_t>(); }
            else
                { detail::GlobalAllocator::Deallocate(data, size * sizeof(uint64_t)); }
            break;
        case Type::Float:
            if consteval
                { delete GetDataVector<double>(); }
            else
                { detail::GlobalAllocator::Deallocate(data, size * sizeof(double)); }
            break;
        case Type::String:
            if consteval
                { delete GetDataAs<std::string>(); }
            else
                { detail::GlobalAllocator::Deallocate(data, size * sizeof(char)); }
            break;
        case Type::Hex:
            if consteval
                { delete GetDataAs<std::string>(); }
            else
                { detail::GlobalAllocator::Deallocate(data, size * sizeof(char) / 8); }
            break;
        case Type::Version:
            if consteval
                { delete GetDataVector<uint64_t>(); }
            else
                { detail::GlobalAllocator::Deallocate(data, size * sizeof(uint64_t)); }
            break;
        case Type::Timestamp:
            if consteval
                { delete GetDataAs<std::string>(); }
            else
                { detail::GlobalAllocator::Deallocate(data, size * sizeof(char)); }
            break;
        case Type::Array:
        case Type::Map:
            (void)ClearChildren();
            if consteval
                { delete GetDataVector<Entry*>(); }
            else
                { (void)detail::GlobalAllocator::Deallocate(data, (*static_cast<size_t*>(data) * sizeof(void*)) + sizeof(size_t)); }
            return;
        case Type::Invalid:
        case Type::Null:
        default:
            return;
        }
        
        data = nullptr;
        type = Type::Null;
    }

    constexpr void Entry::ReleaseComment() noexcept
    {
    #if !FDF_NO_COMMENTS
        if consteval
        {
            if(comment)
                delete GetCommentString();
        }
        else
        {
            if(comment)
                detail::GlobalAllocator::Deallocate(comment, GetCommentControlBlock()->capacity + sizeof(Entry::CommentControlBlock) + 1);
        }

        comment = nullptr;
    #endif
    }
    
    constexpr void Entry::ReleaseEverything() noexcept
    {
        ReleaseData();
        ReleaseComment();
    }



    
    constexpr Entry* Entry::Emplace() noexcept
    {
        auto e = detail::Utils<>::Create();
        return AddChild(e);
    }
    
    constexpr Entry* Entry::AddChild(UniqueEntryPtr& e) noexcept
    {
        if(!IsContainer() || e->parent || e->depth == static_cast<uint8_t>(-1))
            return nullptr;
        
        e->parent = this;
        
        //TODO: In this case, we always prefer new one silently. We should allow customizing that behaviour
        if(type == Type::Map)
        {
            if(Entry* found = GetDirectChild(e->GetIdentifier()))
            {
                std::swap(found->type, e->type);
                std::swap(found->size, e->size);
                std::swap(found->data, e->data);
            #if !FDF_NO_COMMENTS
                std::swap(found->comment, e->comment);
            #endif
                e.reset();
                return found;
            }
        }
        
        if(e->depth != depth + 1)
        {
            if(e->IsContainer())
            {
                e->ForEach<ForEachFlags::Recursive>([diff = e->depth - (depth + 1)](Entry& entry)
                {
                    entry.depth = static_cast<uint8_t>(entry.depth + diff);
                });
            }
            e->depth = depth + 1;
        }

        if consteval
        {
            if(!data)
                data = new (std::nothrow) std::vector<Entry*>();
            assert(data && "Allocation shouldn't fail");
            GetDataVector<Entry*>()->push_back(e.get());
            size = static_cast<uint32_t>(GetDataVector<Entry*>()->size());
            return e.release();
        }
        else
        {
            if(data)
            {
                const size_t capacity = *static_cast<size_t*>(data);
            
                if(static_cast<size_t>(size) >= capacity) //TODO: convert >= to == and add an assert as a sanity check
                {
                    void* newBuffer = detail::GlobalAllocator::Allocate((2 * capacity * sizeof(void*)) + sizeof(size_t));
                    *static_cast<size_t*>(newBuffer) = 2 * capacity;

                    for(size_t i = 0; i < size; i++)
                        (static_cast<Entry**>(newBuffer) + 1)[i] = (static_cast<Entry**>(data) + 1)[i];

                    (void)detail::GlobalAllocator::Deallocate(data, (capacity * sizeof(void*)) + sizeof(size_t));
                    data = newBuffer;
                }
            }
            else
            {
                data = detail::GlobalAllocator::Allocate((4 * sizeof(void*)) + sizeof(size_t));
                *static_cast<size_t*>(data) = 4;
                size = 0;  // Just in case
            }

            (static_cast<Entry**>(data) + 1)[size++] = e.get();
            return e.release();
        }
    }

    // ReSharper disable once CppParameterMayBeConstPtrOrRef (technically we don't modify provided object, but it's modified through another mechanism)
    constexpr bool Entry::RemoveChild(Entry& e) noexcept
    {
        if(size == 0 || !IsContainer())
            return false;

        for(uint32_t i = 0; i < size; i++)
        {
            if consteval
            {
                if((*GetDataVector<Entry*>())[i] == &e)
                    return RemoveChild(i);
            }
            else
            {
                if((static_cast<Entry**>(data) + 1)[i] == &e)
                    return RemoveChild(i);
            }
        }
        return false;
    }

    constexpr bool Entry::RemoveChild(std::string_view _identifier) noexcept
    {
        if(size == 0 || type != Type::Map)
            return false;

        for(uint32_t i = 0; i < size; i++)
        {
            if consteval
            {
                if((*GetDataVector<Entry*>())[i]->GetIdentifier() == _identifier)
                    return RemoveChild(i);
            }
            else
            {
                if((static_cast<Entry**>(data) + 1)[i]->GetIdentifier() == _identifier)
                    return RemoveChild(i);
            }
        }
        return false;
    }

    constexpr bool Entry::RemoveChild(uint32_t index) noexcept
    {
        if(index >= size || !IsContainer())
            return false;

        if consteval
        {
            detail::Utils<>::Destroy((*GetDataVector<Entry*>())[index]);
            GetDataVector<Entry*>()->erase(GetDataVector<Entry*>()->begin() + index);
        }
        else
        {
            detail::Utils<>::Destroy((static_cast<Entry**>(data) + 1)[index]);
            for(; index + 1 < size; index++)
                (static_cast<Entry**>(data) + 1)[index] = (static_cast<Entry**>(data) + 1)[index + 1];
            (static_cast<Entry**>(data) + 1)[index] = nullptr;
        }
        
        size--;
        return true;
    }
    
    constexpr bool Entry::ClearChildren() noexcept
    {
        if(!IsContainer() || !data)
            return false;

        if consteval
        {
            std::vector<Entry*>& childVec = (*GetDataVector<Entry*>());
            for(size_t i = 0; i < childVec.size(); i++)
                detail::Utils<>::Destroy(childVec[i]);
            
            // For some reason, this doesn't compile on clang on Windows...
            //for(Entry* e : childVec)
            //    detail::Utils<>::Destroy(e);
        }
        else
        {
            for(size_t i = 0; i < size; i++)
            {
                (static_cast<Entry**>(data) + 1)[i]->parent = nullptr;
                detail::Utils<>::Destroy((static_cast<Entry**>(data) + 1)[i]);
            }
        }
        
        size = 0;
        return true;
    }



    
    constexpr UniqueEntryPtr Entry::OrphanChild(Entry& e) noexcept
    {
        return UniqueEntryPtr(OrphanChild_INTERNAL(e));
    }

    constexpr UniqueEntryPtr Entry::OrphanChild(std::string_view _identifier) noexcept
    {
        return UniqueEntryPtr(OrphanChild_INTERNAL(_identifier));
    }

    constexpr UniqueEntryPtr Entry::OrphanChild(uint32_t index) noexcept
    {
        return UniqueEntryPtr(OrphanChild_INTERNAL(index));
    }
    
    constexpr std::vector<UniqueEntryPtr> Entry::OrphanChildren() noexcept
    {
        std::vector<UniqueEntryPtr> vec;
        vec.reserve(size);
        for(Entry* e : OrphanChildren_INTERNAL())
            vec.emplace_back(e);
        return vec;
    }
    
    
    
    
    // ReSharper disable once CppParameterMayBeConstPtrOrRef (technically we don't modify provided object, but it's modified through another mechanism)
    constexpr Entry* Entry::OrphanChild_INTERNAL(Entry& e) noexcept
    {
        if(size == 0 || !IsContainer())
            return nullptr;

        for(uint32_t i = 0; i < size; i++)
        {
            if consteval
            {
                if((*GetDataVector<Entry*>())[i] == &e)
                    return OrphanChild_INTERNAL(i);
            }
            else
            {
                if((static_cast<Entry**>(data) + 1)[i] == &e)
                    return OrphanChild_INTERNAL(i);
            }
        }
        return nullptr;
    }
    
    constexpr Entry* Entry::OrphanChild_INTERNAL(std::string_view _identifier) noexcept
    {
        if(size == 0 || type != Type::Map)
            return nullptr;

        for(uint32_t i = 0; i < size; i++)
        {
            if consteval
            {
                if((*GetDataVector<Entry*>())[i]->GetIdentifier() == _identifier)
                    return OrphanChild_INTERNAL(i);
            }
            else
            {
                if((static_cast<Entry**>(data) + 1)[i]->GetIdentifier() == _identifier)
                    return OrphanChild_INTERNAL(i);
            }
        }
        return nullptr;
    }
    
    constexpr Entry* Entry::OrphanChild_INTERNAL(uint32_t index) noexcept
    {
        if(index >= size || !IsContainer())
            return nullptr;

        Entry* original;
        if consteval
        {
            original = (*GetDataVector<Entry*>())[index];
            GetDataVector<Entry*>()->erase(GetDataVector<Entry*>()->begin() + index);
        }
        else
        {
            original = (static_cast<Entry**>(data) + 1)[index];
            for(; index + 1 < size; index++)
                (static_cast<Entry**>(data) + 1)[index] = (static_cast<Entry**>(data) + 1)[index + 1];

            (static_cast<Entry**>(data) + 1)[index] = nullptr;
        }
        
        size--;
        return original;
    }
    
    constexpr std::span<Entry*> Entry::OrphanChildren_INTERNAL() noexcept
    {
        if(!IsContainer() || !data)
            return {};
        
        const auto children = GetChildren();
        for(Entry* e : children)
            e->parent = nullptr;
        
        size = 0;
        return children;
    }




    template<detail::IsValidIDType T, detail::IsValidIDType ... Args>
    constexpr Entry* Entry::GetChild(T&& param, Args&&... args) noexcept
    {
        return const_cast<Entry*>(static_cast<const Entry*>(this)->GetChild(std::forward<T>(param), std::forward<Args>(args)...));
    }

    template<detail::IsValidIDType T, detail::IsValidIDType ... Args>
    constexpr const Entry* Entry::GetChild(T&& param, Args&&... args) const noexcept
    {
        if(size == 0 || !IsContainer())
            return nullptr;

        const Entry* currentEntry = this;
        
        if constexpr(std::integral<std::remove_cvref_t<T>>)
            currentEntry = GetDirectChild(static_cast<uint32_t>(param));
        else
        {
            std::string_view _identifier = param;
            for(size_t searchDepth = static_cast<uint64_t>(std::ranges::count(_identifier, '.') + 1); searchDepth > 0 && currentEntry; searchDepth--)
            {
                std::string_view cur;
                const size_t dotPos = _identifier.find('.');
                if(dotPos == std::string_view::npos)
                    cur = _identifier;
                else
                {
                    cur = _identifier.substr(0, dotPos);
                    _identifier = _identifier.substr(dotPos + 1);
                }

                if(std::ranges::all_of(cur, [](char c) { return c >= '0' && c <= '9'; }))
                {
                    uint32_t value;
                    if(std::from_chars(cur.data(), cur.data() + cur.size(), value).ec != std::errc())
                        return nullptr;

                    currentEntry = currentEntry->GetDirectChild(value);
                }
                else
                    currentEntry = currentEntry->GetDirectChild(cur);
            }
        }
        
        if constexpr(sizeof...(args) > 0)
            return currentEntry? currentEntry->GetChild(std::forward<Args>(args)...) : nullptr;
        return currentEntry == this? nullptr : currentEntry;
    }



    
    constexpr Entry* Entry::GetDirectChild(std::string_view _identifier) noexcept
    {
        return const_cast<Entry*>(static_cast<const Entry*>(this)->GetDirectChild(_identifier));
    }
    constexpr const Entry* Entry::GetDirectChild(std::string_view _identifier) const noexcept
    {
        if(size == 0 || type != Type::Map)
            return nullptr;
        
        for(uint32_t i = 0; i < size; i++)
        {
            if consteval
            {
                if((*GetDataVector<Entry*>())[i]->GetIdentifier() == _identifier)
                    return GetDirectChild(i);
            }
            else
            {
                if((static_cast<Entry**>(data) + 1)[i]->GetIdentifier() == _identifier)
                    return GetDirectChild(i);
            }
        }
        
        return nullptr;
    }
    
    constexpr Entry* Entry::GetDirectChild(uint32_t index) noexcept
    {
        return const_cast<Entry*>(static_cast<const Entry*>(this)->GetDirectChild(index));
    }
    constexpr const Entry* Entry::GetDirectChild(uint32_t index) const noexcept
    {
        if(index >= size || !IsContainer())
            return nullptr;
        
        if consteval
        {
            return (*GetDataVector<Entry*>())[index];
        }
        else
        {
            return (static_cast<Entry**>(data) + 1)[index];
        }
    }

    
    
    
    constexpr std::span<Entry*> Entry::GetChildren() noexcept
    {
        if(size == 0 || !IsContainer())
            return {};

        if consteval
        {
            return *GetDataVector<Entry*>();
        }
        return {(static_cast<Entry**>(data) + 1), size};
    }
    constexpr std::span<const Entry*> Entry::GetChildren() const noexcept
    {
        if(size == 0 || !IsContainer())
            return {};

        if consteval
        {
            return {const_cast<const Entry**>(const_cast<Entry*>(this)->GetDataVector<Entry*>()->data()), size};
        }
        return {(static_cast<const Entry**>(data) + 1), size};
    }
    
    constexpr std::span<Entry*> Entry::GetChildren_INTERNAL() noexcept
    {
        assert(size != 0 || IsContainer() && "If we opt into this version which doesn't checks these, it should be already in a known good state!");
        
        if consteval
        {
            return *GetDataVector<Entry*>();
        }
        return {(static_cast<Entry**>(data) + 1), size};
    }
    constexpr std::span<const Entry*> Entry::GetChildren_INTERNAL() const noexcept
    {
        assert(size != 0 || IsContainer() && "If we opt into this version which doesn't checks these, it should be already in a known good state!");
        
        if consteval
        {
            return {const_cast<const Entry**>(const_cast<Entry*>(this)->GetDataVector<Entry*>()->data()), size};
        }
        return {(static_cast<const Entry**>(data) + 1), size};
    }










    constexpr size_t Entry::GetChildCountRecursive() const noexcept
    {
        if(size == 0 || !IsContainer())
            return 0;
        
        size_t total = 0;
        std::vector<const Entry*> stack;
        stack.push_back(this);

        while(!stack.empty())
        {
            const Entry* current = stack.back();
            stack.pop_back();

            if((current->type == Type::Array || current->type == Type::Map) && current->size > 0)
            {
                total += current->size;
                for(const Entry* child : std::ranges::reverse_view(current->GetChildren_INTERNAL()))
                    stack.push_back(child);
            }
        }
        
        return total;
    }

    constexpr std::vector<Entry*> Entry::GetChildrenRecursive() noexcept
    {
        if(size == 0 || !IsContainer())
            return {};
        
        std::vector<Entry*> stack;
        std::vector<Entry*> result;
        stack.push_back(this);

        while(!stack.empty())
        {
            Entry* current = stack.back();
            stack.pop_back();
            result.push_back(current);

            if((current->type == Type::Array || current->type == Type::Map) && current->size > 0)
            {
                for(Entry* child : std::ranges::reverse_view(current->GetChildren_INTERNAL()))
                    stack.push_back(child);
            }
        }
        
        return result;
    }

    constexpr std::vector<const Entry*> Entry::GetChildrenRecursive() const noexcept
    {
        if(size == 0 || !IsContainer())
            return {};
        
        std::vector<const Entry*> stack;
        std::vector<const Entry*> result;
        stack.push_back(this);

        while(!stack.empty())
        {
            const Entry* current = stack.back();
            stack.pop_back();
            result.push_back(current);

            if((current->type == Type::Array || current->type == Type::Map) && current->size > 0)
            {
                for(const Entry* child : std::ranges::reverse_view(current->GetChildren_INTERNAL()))
                    stack.push_back(child);
            }
        }
        
        return result;
    }




    template<std::underlying_type_t<ForEachFlags::Flag> FLAGS, typename Callable> requires (std::is_invocable_v<Callable, Entry&>)
    constexpr void Entry::ForEach(Callable callback) noexcept(std::is_nothrow_invocable_v<Callable, Entry&>)
    {
        if constexpr(!ForEachFlags::IsValidForEachFlag(FLAGS))
        {
            static_assert(false, "Invalid flag");
        }
        else if constexpr(ForEachFlags::IsNotSet(FLAGS, ForEachFlags::Recursive | ForEachFlags::Group))
        {
            // Non-recursive, non-sorted
            if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                callback(*this);
            for(Entry* child : GetChildren())
                callback(*child);
        }
        else if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::Group) && ForEachFlags::IsNotSet(FLAGS, ForEachFlags::Recursive))
        {
            // Non-recursive, sorted
            if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                callback(*this);

            if(size == 0 || !IsContainer())
                return;

            const auto children = GetChildren_INTERNAL();
            for(Entry* e : children)
            {
                if(!e->IsContainer())
                    callback(*e);
            }
            for(Entry* e : children)
            {
                if(e->type == Type::Array)
                    callback(*e);
            }
            for(Entry* e : children)
            {
                if(e->type == Type::Map)
                    callback(*e);
            }
        }
        else if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::Recursive) && ForEachFlags::IsNotSet(FLAGS, ForEachFlags::Group))
        {
            // Recursive, non-sorted
            if(size == 0 || !IsContainer())
            {
                if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                    callback(*this);
                return;
            }
            
            std::stack<Entry*> stack;
            if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
            {
                stack.push(this);
            }
            else
            {
                for(Entry* child : std::ranges::reverse_view(GetChildren_INTERNAL()))
                    stack.push(child);
            }

            while(!stack.empty())
            {
                Entry* current = stack.top();
                stack.pop();
                callback(*current);

                for(Entry* child : std::ranges::reverse_view(current->GetChildren()))
                    stack.push(child);
            }
        }
        else if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::Recursive | ForEachFlags::Group))
        {
            // Recursive, sorted
            if(size == 0 || !IsContainer())
            {
                if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                    callback(*this);
                return;
            }
            
            enum class Phase : uint8_t { Pre, Leaf, Array, Map };
            struct Frame { Entry* e; Phase phase; uint32_t idx;};
        
            std::stack<Frame> stack;
            stack.push(Frame{ this, Phase::Pre, 0 });
        
            while(!stack.empty())
            {
                Frame& f = stack.top();
                switch(f.phase)
                {
                    case Phase::Pre:
                    {
                        if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                        {
                            callback(*f.e);
                        }
                        else
                        {
                            if(f.e != this)
                                callback(*f.e);
                        }
            
                        f.phase = Phase::Leaf;
                        break;
                    }
                    case Phase::Leaf:
                    {
                        for(Entry* c : f.e->GetChildren_INTERNAL())
                        {
                            if(!c->IsContainer())
                                callback(*c);
                        }
                        
                        f.phase = Phase::Array;
                        break;
                    }
                    case Phase::Array:
                    {
                        auto children = f.e->GetChildren_INTERNAL();
                        bool pushed = false;
                        while(f.idx < children.size())
                        {
                            Entry* c = children[f.idx++];
                            if(c->IsContainer() && c->type == Type::Array)
                            {
                                stack.push(Frame{ c, Phase::Pre, 0 });
                                pushed = true;
                                break;
                            }
                        }
                        if(!pushed)
                        {
                            f.phase = Phase::Map;
                            f.idx = 0;
                        }
                        break;
                    }
                    case Phase::Map:
                    {
                        auto children = f.e->GetChildren_INTERNAL();
                        bool pushed = false;
                        while(f.idx < children.size())
                        {
                            Entry* c = children[f.idx++];
                            if(c->IsContainer() && c->type == Type::Map)
                            {
                                stack.push(Frame{ c, Phase::Pre, 0 });
                                pushed = true;
                                break;
                            }
                        }
                        if(!pushed)
                            stack.pop();
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }

    template<std::underlying_type_t<ForEachFlags::Flag> FLAGS, typename Callable> requires (std::is_invocable_v<Callable, const Entry&>)
    constexpr void Entry::ForEach(Callable callback) const noexcept(std::is_nothrow_invocable_v<Callable, const Entry&>)
    {
        if constexpr(!ForEachFlags::IsValidForEachFlag(FLAGS))
        {
            static_assert(false, "Invalid flag");
        }
        else if constexpr(ForEachFlags::IsNotSet(FLAGS, ForEachFlags::Recursive | ForEachFlags::Group))
        {
            // Non-recursive, non-sorted
            if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                callback(*this);
            for(const Entry* child : GetChildren())
                callback(*child);
        }
        else if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::Group) && ForEachFlags::IsNotSet(FLAGS, ForEachFlags::Recursive))
        {
            // Non-recursive, sorted
            if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                callback(*this);

            if(size == 0 || !IsContainer())
                return;

            const auto children = GetChildren_INTERNAL();
            for(const Entry* e : children)
            {
                if(!e->IsContainer())
                    callback(*e);
            }
            for(const Entry* e : children)
            {
                if(e->type == Type::Array)
                    callback(*e);
            }
            for(const Entry* e : children)
            {
                if(e->type == Type::Map)
                    callback(*e);
            }
        }
        else if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::Recursive) && ForEachFlags::IsNotSet(FLAGS, ForEachFlags::Group))
        {
            // Recursive, non-sorted
            if(size == 0 || !IsContainer())
            {
                if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                    callback(*this);
                return;
            }
            
            std::stack<const Entry*> stack;
            if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
            {
                stack.push(this);
            }
            else
            {
                for(const Entry* child : std::ranges::reverse_view(GetChildren_INTERNAL()))
                    stack.push(child);
            }

            while(!stack.empty())
            {
                const Entry* current = stack.top();
                stack.pop();
                callback(*current);

                for(const Entry* child : std::ranges::reverse_view(current->GetChildren()))
                    stack.push(child);
            }
        }
        else if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::Recursive | ForEachFlags::Group))
        {
            // Recursive, sorted
            if(size == 0 || !IsContainer())
            {
                if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                    callback(*this);
                return;
            }
            
            enum class Phase : uint8_t { Pre, Leaf, Array, Map };
            struct Frame { const Entry* e; Phase phase; uint32_t idx;};
        
            std::stack<Frame> stack;
            stack.push(Frame{ this, Phase::Pre, 0 });
        
            while(!stack.empty())
            {
                Frame& f = stack.top();
                switch(f.phase)
                {
                    case Phase::Pre:
                    {
                        if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                        {
                            callback(*f.e);
                        }
                        else
                        {
                            if(f.e != this)
                                callback(*f.e);
                        }
            
                        f.phase = Phase::Leaf;
                        break;
                    }
                    case Phase::Leaf:
                    {
                        for(const Entry* c : f.e->GetChildren_INTERNAL())
                        {
                            if(!c->IsContainer())
                                callback(*c);
                        }
                        
                        f.phase = Phase::Array;
                        break;
                    }
                    case Phase::Array:
                    {
                        auto children = f.e->GetChildren_INTERNAL();
                        bool pushed = false;
                        while(f.idx < children.size())
                        {
                            const Entry* c = children[f.idx++];
                            if(c->IsContainer() && c->type == Type::Array)
                            {
                                stack.push(Frame{ c, Phase::Pre, 0 });
                                pushed = true;
                                break;
                            }
                        }
                        if(!pushed)
                        {
                            f.phase = Phase::Map;
                            f.idx = 0;
                        }
                        break;
                    }
                    case Phase::Map:
                    {
                        auto children = f.e->GetChildren_INTERNAL();
                        bool pushed = false;
                        while(f.idx < children.size())
                        {
                            const Entry* c = children[f.idx++];
                            if(c->IsContainer() && c->type == Type::Map)
                            {
                                stack.push(Frame{ c, Phase::Pre, 0 });
                                pushed = true;
                                break;
                            }
                        }
                        if(!pushed)
                            stack.pop();
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }










    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<bool>() noexcept
    {
        if(type != Type::Bool)
            return std::span<bool>();
        if consteval
            { return std::span(GetDataAs<bool>(), size); }
        return std::span(static_cast<bool*>(data), size);
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<int64_t>() noexcept
    {
        if(type != Type::Int)
            return std::span<int64_t>();
        if consteval
            { return std::span(*GetDataVector<int64_t>()); }
        return std::span(static_cast<int64_t*>(data), size);
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<int>() noexcept  { return GetValue<int64_t>(); }
    
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<uint64_t>() noexcept
    {
        if(type != Type::UInt && type != Type::Version)
            return std::span<uint64_t>();
        if consteval
            { return std::span(*GetDataVector<uint64_t>()); }
        return std::span(static_cast<uint64_t*>(data), size);
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<unsigned int>() noexcept  { return GetValue<uint64_t>(); }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<double>() noexcept
    {
        if(type != Type::Float)
            return std::span<double>();
        if consteval
            { return std::span(*GetDataVector<double>()); }
        return std::span(static_cast<double*>(data), size);
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<float>() noexcept  { return GetValue<double>(); }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<char>() noexcept
    {
        if(type != Type::String && type != Type::Hex && type != Type::Timestamp)
            return std::string_view();
        if consteval
            { return std::string_view(*GetDataAs<std::string>()); }
        return std::string_view((static_cast<char*>(data) + sizeof(uint32_t)), size);
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<std::string>() noexcept  { return GetValue<char>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<std::string_view>() noexcept  { return GetValue<char>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<char*>() noexcept  { return GetValue<char>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<const char*>() noexcept  { return GetValue<char>(); }




    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<bool>() const noexcept
    {
        if(type != Type::Bool)
            return std::span<const bool>();
        if consteval
            { return std::span(GetDataAs<bool>(), size); }
        return std::span(static_cast<const bool*>(data), size);
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<int64_t>() const noexcept
    {
        if(type != Type::Int)
            return std::span<const int64_t>();
        if consteval
            { return std::span(*GetDataVector<int64_t>()); }
        return std::span(static_cast<const int64_t*>(data), size);
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<int>() const noexcept  { return GetValue<int64_t>(); }
    
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<uint64_t>() const noexcept
    {
        if(type != Type::UInt && type != Type::Version)
            return std::span<const uint64_t>();
        if consteval
            { return std::span(*GetDataVector<uint64_t>()); }
        return std::span(static_cast<const uint64_t*>(data), size);
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<unsigned int>() const noexcept  { return GetValue<uint64_t>(); }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<double>() const noexcept
    {
        if(type != Type::Float)
            return std::span<const double>();
        if consteval
            { return std::span(*GetDataVector<double>()); }
        return std::span(static_cast<const double*>(data), size);
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<float>() const noexcept  { return GetValue<double>(); }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<char>() const noexcept
    {
        if(type != Type::String && type != Type::Hex && type != Type::Timestamp)
            return std::string_view();
        if consteval
            { return std::string_view(*GetDataAs<std::string>()); }
        return std::string_view((static_cast<const char*>(data) + sizeof(uint32_t)), size);
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<std::string>() const noexcept  { return GetValue<char>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<std::string_view>() const noexcept  { return GetValue<char>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<char*>() const noexcept  { return GetValue<char>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<const char*>() const noexcept  { return GetValue<char>(); }










#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4702)
#endif
    template<Style STYLE>
    [[nodiscard]] constexpr std::string_view Entry::DataToView(std::string& temp) const noexcept
    {
        switch(type)
        {
            case Type::Invalid: return detail::INVALID_TEXT;
            case Type::Null:    if constexpr(STYLE.bUseNilInsteadOfNull) return detail::KEYWORDS[1]; return detail::KEYWORDS[0];
            case Type::Array:   return detail::ARRAY_TEXT;
            case Type::Map:     return detail::MAP_TEXT;

            case Type::String:
            case Type::Timestamp:
            {
                const std::string_view view = GetValue<char>();
                assert(!view.empty() && "This being empty either means type was wrong or it was really empty which shouldn't happen! (If it was really empty, it would be Type::Null)");
                return view;
            }
            
            case Type::Hex:
            {
                const std::string_view view = GetValue<char>();
                assert(!view.empty() && "This being empty either means type was wrong or it was really empty which shouldn't happen! (If it was really empty, it would be Type::Null)");

                if constexpr(STYLE.bUppercaseHex)
                {
                    temp = view;
                    std::ranges::transform(temp, temp.begin(), [](char c)  { return static_cast<char>(std::toupper(c)); });
                    return temp;
                }

                return view;
            }

            case Type::Version:
            {
                const auto span = GetValue<uint64_t>();
                assert((span.size() == 3 || span.size() == 4) && "This being empty either means type was wrong or it was really empty which shouldn't happen! (If it was really empty, it would be Type::Null)");
                if(size == 3)
                    temp = std::format("{}.{}.{}", span[0], span[1], span[2]);
                else
                    temp = std::format("{}.{}.{}.{}", span[0], span[1], span[2], span[3]);
                return temp;
            }

            case Type::Bool:
            {
                const auto span = GetValue<bool>();
                assert(!span.empty() && "This being empty either means type was wrong or it was really empty which shouldn't happen! (If it was really empty, it would be Type::Null)");
                temp = std::format("{}", (span[0]? detail::KEYWORDS[2] : detail::KEYWORDS[3]));
                for(size_t i = 1; i < span.size(); i++)
                    temp = std::format("{}x{}", temp, (span[i]? detail::KEYWORDS[2] : detail::KEYWORDS[3]));
                return temp;
            }

            case Type::Int:
            {
                const auto span = GetValue<int64_t>();
                assert(!span.empty() && "This being empty either means type was wrong or it was really empty which shouldn't happen! (If it was really empty, it would be Type::Null)");
                temp = std::format("{}", span[0]);
                for(size_t i = 1; i < span.size(); i++)
                    temp = std::format("{}x{}", temp, span[i]);
                return temp;
            }

            case Type::UInt:
            {
                const auto span = GetValue<uint64_t>();
                assert(!span.empty() && "This being empty either means type was wrong or it was really empty which shouldn't happen! (If it was really empty, it would be Type::Null)");
                temp = std::format("{}", span[0]);
                for(size_t i = 1; i < span.size(); i++)
                    temp = std::format("{}x{}", temp, span[i]);
                return temp;
            }

            case Type::Float:
            {
                const auto span = GetValue<float>();
                assert(!span.empty() && "This being empty either means type was wrong or it was really empty which shouldn't happen! (If it was really empty, it would be Type::Null)");
                temp = std::format("{}", span[0]);
                for(size_t i = 1; i < span.size(); i++)
                    temp = std::format("{}x{}", temp, span[i]);
                return temp;
            }

            default:
                std::unreachable();
                return detail::UNEXPECTED_TEXT;
        }
    }
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
}










namespace fdf
{
    template <auto DIAGNOSTIC_CALLBACK>
    bool Entry::ParseCombineFile(const std::filesystem::path& filepath, CommentCombineStrategy fileCommentCombineStrategy)
    {
        if(!std::filesystem::exists(filepath) || !std::filesystem::is_regular_file(filepath))
            return false;

        std::ifstream file(filepath);
        if(!file)
            return false;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return ParseCombineBuffer<DIAGNOSTIC_CALLBACK>(content, fileCommentCombineStrategy);
    }
    
    template <auto DIAGNOSTIC_CALLBACK>
    constexpr bool Entry::ParseCombineBuffer(std::string_view content, CommentCombineStrategy fileCommentCombineStrategy) noexcept
    {
        if(type != Type::Map)
            return false;
        
        UniqueEntryPtr other = detail::Utils<DIAGNOSTIC_CALLBACK>::ParseBuffer(content);
        return Combine(other, fileCommentCombineStrategy);
    }
    
    constexpr bool Entry::Combine(UniqueEntryPtr& other, CommentCombineStrategy fileCommentCombineStrategy) noexcept
    {
        if(!IsContainer() || type != other->type)
            return false;
        
    #if !FDF_NO_COMMENTS
        switch(fileCommentCombineStrategy)
        {
        case CommentCombineStrategy::UseExisting: break;
        case CommentCombineStrategy::UseNew: SetComment(other->GetComment()); break;
        case CommentCombineStrategy::UseNewIfExistingIsEmpty: 
            if(GetComment().empty())
                SetComment(other->GetComment());
            break;
        case CommentCombineStrategy::Merge:
            if(GetComment().empty())
                SetComment(other->GetComment());
            else if(!other->GetComment().empty())
                SetComment((std::string(GetComment()) + '\n').append(other->GetComment()));  //TODO: Maybe optimize
            break;
        case CommentCombineStrategy::Clear: ReleaseComment(); break;
        default: std::unreachable();
        }
    #endif
        
        return AddChild(other);
    }
    
    
    
    
    
    template<auto DIAGNOSTIC_CALLBACK>
    UniqueEntryPtr Entry::ParseFile(const std::filesystem::path& filepath)
    {
        if(!std::filesystem::exists(filepath) || !std::filesystem::is_regular_file(filepath))
            return nullptr;

        std::ifstream file(filepath);
        if(!file)
            return nullptr;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return detail::Utils<DIAGNOSTIC_CALLBACK>::ParseBuffer(content);
    }

    template<auto DIAGNOSTIC_CALLBACK>
    constexpr UniqueEntryPtr Entry::ParseBuffer(std::string_view content) noexcept
    {
        return detail::Utils<DIAGNOSTIC_CALLBACK>::ParseBuffer(content);
    }

    template<Style STYLE>
    bool Entry::WriteFile(const Entry& e, const std::filesystem::path& filepath, bool bCreateIfNotExists)
    {
        auto parentDir = filepath.parent_path();
        if(!std::filesystem::exists(parentDir))
        {
            if(!bCreateIfNotExists || !std::filesystem::create_directories(parentDir))
                return false;
        }
        else if(!std::filesystem::is_regular_file(filepath))
        {
            return false;
        }

        std::ofstream file(filepath);
        if(!file)
            return false;

        std::string buffer;
        detail::Utils<>::WriteBuffer<STYLE>(e, buffer);

        file << buffer;
        return static_cast<bool>(file);
    }
    template<Style STYLE>
    constexpr void Entry::WriteBuffer(const Entry& root, std::string& buffer) noexcept
    {
        detail::Utils<>::WriteBuffer<STYLE>(root, buffer);
    }
}

namespace fdf::detail
{
    template<auto DIAGNOSTIC_CALLBACK>
    template<typename... Args>
    constexpr UniqueEntryPtr Utils<DIAGNOSTIC_CALLBACK>::Create(Args&&... args) noexcept
    {
        if consteval
        {
            UniqueEntryPtr p = UniqueEntryPtr(new (std::nothrow) Entry{std::forward<Args>(args)...});
            assert(p && "Allocation shouldn't fail");
            return p;
        }
        else
        {
            return UniqueEntryPtr(new(GlobalAllocator::ENTRY_ALLOCATOR.Allocate()) Entry{std::forward<Args>(args)...});
        }
    }

    template<auto DIAGNOSTIC_CALLBACK>
    constexpr void Utils<DIAGNOSTIC_CALLBACK>::Destroy(Entry* e) noexcept
    {
        if consteval
        {
            delete e;
        }
        else
        {
            if(e)
            {
                e->~Entry();
                (void)GlobalAllocator::ENTRY_ALLOCATOR.Deallocate(e);
            }
        }
    }
    
    constexpr void EntryDeleter::operator()(Entry* e) noexcept
    {
        Utils<>::Destroy(e);
    }

    template<auto DIAGNOSTIC_CALLBACK>
    constexpr UniqueEntryPtr Utils<DIAGNOSTIC_CALLBACK>::ParseBuffer(std::string_view content) noexcept
    {
        Tokenizer tokenizer(content);
        #if !FDF_NO_COMMENTS
            Token fileCommentToken = TokenType::NonExisting;
        #endif

        UniqueEntryPtr root = Create();
        if(!root)
        {
            std::unreachable();
        }
        root->type = Type::Map;
        root->depth = static_cast<uint8_t>(-1);
        
        while(true)
        {
            #if !FDF_NO_COMMENTS
                Token comment = TokenType::NonExisting;
            #endif
            Token currentToken = tokenizer.Current();
            if(currentToken.type == TokenType::Invalid)
            {
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
                            currentToken.startPosition += static_cast<uint32_t>(firstChar);
                            currentToken.count = currentToken.count - static_cast<uint32_t>(firstChar);
                            if(content[currentToken.startPosition] == '\n')
                            {
                                currentToken.startPosition++;
                                currentToken.count--;
                            }
                        }
        
                        //if(fileCommentToken.type != TokenType::NonExisting)
                        //    if(!DIAGNOSTIC_CALLBACK(Error::AlreadyHasComment, std::format("File already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", fileCommentToken.ToView(content), fileCommentToken.line, fileCommentToken.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                        //        return false;
                        fileCommentToken = currentToken;
                    }
                    else
                    {
                        //if(comment.type != TokenType::NonExisting)
                        //    if(!DIAGNOSTIC_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
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
                    return nullptr;
                }
        
                continue;
            }
            
            if(currentToken.type == TokenType::EndOfFile)
                break;
        
            return nullptr;  // First token can't be anything else
        }

        #if !FDF_NO_COMMENTS
            // Trim the whitespace from the comment (not '\n')
            if(fileCommentToken.type != TokenType::NonExisting)
            {
                std::string_view view = tokenizer.ToView(fileCommentToken);
                if consteval
                {
                    root->comment = new (std::nothrow) std::string();
                    assert(root->comment && "Allocation shouldn't fail");
                    root->GetCommentString()->reserve(view.size());
                    
                    bool bAfterNewLine = true;
                    for(char c : view)
                    {
                        if(bAfterNewLine)
                        {
                            if(constexpr_isspace(c))
                                continue;

                            root->GetCommentString()->push_back(c);
                            bAfterNewLine = false;
                        }
                        else
                        {
                            root->GetCommentString()->push_back(c);
                            bAfterNewLine = (c == '\n');
                        }
                    }
                }
                else
                {
                    root->comment = static_cast<Entry::CommentControlBlock*>(GlobalAllocator::Allocate(view.size() + sizeof(Entry::CommentControlBlock) + 1));
                    root->GetCommentControlBlock()->capacity = static_cast<uint32_t>(view.size());
                    root->GetCommentControlBlock()->size = 0;

                    char* cur = root->GetCommentData();
                    bool bAfterNewLine = true;
                    for(char c : view)
                    {
                        if(bAfterNewLine)
                        {
                            if(constexpr_isspace(c))
                                continue;

                            cur[root->GetCommentControlBlock()->size++] = c;
                            bAfterNewLine = false;
                        }
                        else
                        {
                            cur[root->GetCommentControlBlock()->size++] = c;
                            bAfterNewLine = (c == '\n');
                        }
                    }

                    cur[root->GetCommentControlBlock()->size] = '\0';
                }
            }
        #endif
        
        return root;
    }

    template<auto DIAGNOSTIC_CALLBACK>
    [[nodiscard]] constexpr bool Utils<DIAGNOSTIC_CALLBACK>::ParseVariable   (Tokenizer& tokenizer, Entry& parent   FDF_COMMENT_SWITCH(, Token comment)) noexcept
    {
        assert(parent.IsContainer() && "Sanity check!");
        assert(parent.depth + 1 != static_cast<uint8_t>(-1) && "Too much nesting check!");
        
        Token currentToken = tokenizer.Current();
        Entry* entry = parent.Emplace();
        if(!entry)
        {
            std::unreachable();
        }
        entry->depth = parent.depth + 1;

        if(parent.type != Type::Array)
        {
            assert(tokenizer.Current().type == TokenType::Identifier && "Sanity check!");
            if(!entry->SetIdentifier(tokenizer.ToView(currentToken)))
                return false; // TODO: report too long identifier and continue?
            currentToken = tokenizer.Advance();
        }

        FDF_CHECK_TOKEN(currentToken);
        FDF_CHECK_TOKEN_FOR_EOF(currentToken);


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
                //    if(!DIAGNOSTIC_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                //        return false;
                comment = currentToken;
            }
        #endif

            currentToken = tokenizer.Advance();
            FDF_CHECK_TOKEN(currentToken);
            FDF_CHECK_TOKEN_FOR_EOF(currentToken);
        }
        
        if(IsValueLiteral(currentToken.type) && (bHasEqual || parent.type == Type::Array))
            return ParseSimpleValue(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment));
        if(currentToken.type == TokenType::CurlyBraceOpen)
            return ParseMap(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment));
        if(currentToken.type == TokenType::SquareBraceOpen)
            return ParseArray(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment));
    
        return false;  // Something we didn't process yet?
    }
    template<auto DIAGNOSTIC_CALLBACK>
    [[nodiscard]] constexpr bool Utils<DIAGNOSTIC_CALLBACK>::ParseSimpleValue(Tokenizer& tokenizer, Entry& entry    FDF_COMMENT_SWITCH(, Token comment)) noexcept
    {
        assert(IsValueLiteral(tokenizer.Current().type) && "Sanity check!");
        
        Token currentToken = tokenizer.Current();
        const std::string_view view = tokenizer.ToView(currentToken);

        auto postProcess = [&]()
        {
            currentToken = tokenizer.Advance();
            if(currentToken.type == TokenType::Comment)
            {
            #if !FDF_NO_COMMENTS
                //if(comment.type != TokenType::NonExisting)
                //    if(!DIAGNOSTIC_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
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
                if consteval
                {
                    entry.data = new (std::nothrow) bool[1];
                    assert(entry.data && "Allocation shouldn't fail");
                    *entry.GetDataAs<bool>() = currentToken.extra8 == 2;
                }
                else
                {
                    entry.data = GlobalAllocator::Allocate(sizeof(bool));
                    static_cast<bool*>(entry.data)[0] = currentToken.extra8 == 2;
                }
                return postProcess();
            }

            if(currentToken.extra8 == 4)
            {
                std::string_view mdBool = view;
                
                entry.type = Type::Bool;
                entry.size = static_cast<uint32_t>(std::ranges::count(mdBool, 'x')) + 1;
                if consteval
                {
                    entry.data = new (std::nothrow) bool[entry.size];
                    assert(entry.data && "Allocation shouldn't fail");
                }
                else
                {
                    entry.data = GlobalAllocator::Allocate(entry.size * sizeof(bool));
                }
                
                size_t cur = 0;
                bool bLastWasBoolLiteral = false;
                while(!mdBool.empty())
                {
                    if(mdBool.starts_with(KEYWORDS[2]))
                    {
                        if(bLastWasBoolLiteral)
                        {
                            entry.ReleaseData();
                            return false;
                        }

                        bLastWasBoolLiteral = true;
                        if consteval
                            { entry.GetDataAs<bool>()[cur++] = true; }
                        else
                            { static_cast<bool*>(entry.data)[cur++] = true; }
                        mdBool = mdBool.substr(4);
                    }
                    else if(mdBool.starts_with(KEYWORDS[3]))
                    {
                        if(bLastWasBoolLiteral)
                        {
                            entry.ReleaseData();
                            return false;
                        }

                        bLastWasBoolLiteral = true;
                        if consteval
                            { entry.GetDataAs<bool>()[cur++] = false; }
                        else
                            { static_cast<bool*>(entry.data)[cur++] = false; }
                        mdBool = mdBool.substr(5);
                    }
                    else if(mdBool.starts_with('x'))
                    {
                        if(!bLastWasBoolLiteral)
                        {
                            entry.ReleaseData();
                            return false;
                        }

                        bLastWasBoolLiteral = false;
                        mdBool = mdBool.substr(1);
                    }
                    else
                    {
                        entry.ReleaseData();
                        return false;
                    }
                }

                return postProcess();
            }

            return false;  // Invalid keyword when expected a value
        }




        if(currentToken.type == TokenType::EvaluateLiteral)
        {
            entry.size = static_cast<uint32_t>(EVALUATE_LITERAL_TEXT.size());
            entry.type = Type::String;
            if consteval
            {
                entry.data = new (std::nothrow) std::string();
                assert(entry.data && "Allocation shouldn't fail");
                (*entry.GetDataAs<std::string>()) = EVALUATE_LITERAL_TEXT;
            }
            else
            {
                entry.data = GlobalAllocator::Allocate(entry.size + 1 + sizeof(uint32_t));
                *static_cast<uint32_t*>(entry.data) = entry.size;
                constexpr_memcpy((static_cast<char*>(entry.data) + sizeof(uint32_t)), EVALUATE_LITERAL_TEXT.data(), EVALUATE_LITERAL_TEXT.size() + 1);
                (static_cast<char*>(entry.data) + sizeof(uint32_t))[EVALUATE_LITERAL_TEXT.size()] = '\0';
            }

            //[[maybe_unused]] auto DEBUG = std::string_view(static_cast<char*>(entry.data) + sizeof(uint32_t), entry.size);
            return postProcess();
        }



        
        entry.size = currentToken.extra8;  // Dimension count or string length




        if(currentToken.type == TokenType::IntLiteral)
        {
            if consteval
            {
                entry.data = new (std::nothrow) std::vector<int64_t>();
                assert(entry.data && "Allocation shouldn't fail");
                entry.GetDataVector<int64_t>()->resize(entry.size);
            }
            else
            {
                entry.data = GlobalAllocator::Allocate(entry.size * sizeof(int64_t));
            }
            
            bool bIsUnsigned = false;
            bool bContainsAnyNegative = false;
            bool bIsFirstChar = true;
            bool bIsNegative = false;

            uint64_t result = 0;
            uint8_t currentDimension = 0;
            const uint8_t dimensionCount = static_cast<uint8_t>(entry.size);

            auto finishDimension = [&]() -> bool
            {
                if(bIsNegative)
                {
                    if(bIsUnsigned || result > INT64_MAX_VALUE)
                        return false;

                    if consteval
                        { (*entry.GetDataVector<int64_t>())[currentDimension] = -static_cast<int64_t>(result); }
                    else
                        { static_cast<int64_t*>(entry.data)[currentDimension] = -static_cast<int64_t>(result); }
                }
                else
                {
                    const bool bWasUnsigned = bIsUnsigned;
                    if(result > static_cast<uint64_t>(INT64_MAX_VALUE))
                        bIsUnsigned = true;

                    if(bIsUnsigned)
                    {
                        if(bContainsAnyNegative)
                        {
                            entry.ReleaseData();
                            return false;
                        }

                        if(!bWasUnsigned)
                        {
                            if consteval
                            {
                                auto* temp = new (std::nothrow) std::vector<uint64_t>();
                                assert(temp && "Allocation shouldn't fail");
                                auto& oldVec = *entry.GetDataVector<int64_t>();
                                temp->resize(oldVec.size());
                                
                                for(size_t i = 0; i < oldVec.size(); ++i)
                                    (*temp)[i] = static_cast<uint64_t>(oldVec[i]);

                                delete entry.GetDataVector<int64_t>();
                                entry.data = temp;
                            }
                            else
                            {
                                for(uint8_t i = 0; i < currentDimension - 1; i++)
                                {
                                    const int64_t temp = static_cast<int64_t*>(entry.data)[i];
                                    static_cast<uint64_t*>(entry.data)[i] = static_cast<uint64_t>(temp);
                                }
                            }
                        }

                        if consteval
                            { (*entry.GetDataVector<uint64_t>())[currentDimension] = result; }
                        else
                            { static_cast<uint64_t*>(entry.data)[currentDimension] = result; }
                    }
                    else
                    {
                        if consteval
                            { (*entry.GetDataVector<int64_t>())[currentDimension] = static_cast<int64_t>(result); }
                        else
                            { static_cast<int64_t*>(entry.data)[currentDimension] = static_cast<int64_t>(result); }
                    }
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
                else if(constexpr_isdigit(c))
                {
                    if(result > UINT64_MAX_VALUE / 10)
                        return false;  // Overflow

                    result *= 10;

                    const uint64_t digit = static_cast<uint64_t>(c - '0');
                    if(result > UINT64_MAX_VALUE - digit)
                        return false; // Overflow

                    result += digit;
                }
                else if(c == '.')
                {
                    while(i < view.size() && (view[i] == '.' || constexpr_isdigit(view[i])))
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
            //[[maybe_unused]] auto DEBUG = std::span<int64_t>(static_cast<int64_t*>(entry.data), entry.size);
            return postProcess();
        }



        
        if(currentToken.type == TokenType::FloatLiteral)
        {
            entry.type = Type::Float;
            if consteval
            {
                entry.data = new (std::nothrow) std::vector<double>();
                assert(entry.data && "Allocation shouldn't fail");
                entry.GetDataVector<double>()->resize(entry.size);
            }
            else
            {
                entry.data = GlobalAllocator::Allocate(entry.size * sizeof(double));
            }

            bool bIsFirstChar = true;
            bool bIsNegative = false;
            bool bAfterDot = false;

            double multiplier = 1.0;
            double result = 0.0;
            uint8_t currentDimension = 0;
            const uint8_t dimensionCount = static_cast<uint8_t>(entry.size);

            for(char c : view)
            {
                if(bIsFirstChar && c == '-')
                {
                    bIsNegative = true;
                }
                else if(constexpr_isdigit(c))
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

                    if consteval
                        { (*entry.GetDataVector<double>())[currentDimension] = bIsNegative? -result : result; }
                    else
                        { static_cast<double*>(entry.data)[currentDimension] = bIsNegative? -result : result; }

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

            if consteval
                { (*entry.GetDataVector<double>())[currentDimension] = bIsNegative? -result : result; }
            else
                { static_cast<double*>(entry.data)[currentDimension] = bIsNegative? -result : result; }
            
            //[[maybe_unused]] auto DEBUG = std::span<double>(static_cast<double*>(entry.data), entry.size);
            return postProcess();
        }
    
    
    
    
        if(currentToken.type == TokenType::VersionLiteral)
        {
            entry.type = Type::Version;
            if consteval
            {
                entry.data = new (std::nothrow) std::vector<uint64_t>();
                assert(entry.data && "Allocation shouldn't fail");
                entry.GetDataVector<uint64_t>()->resize(4);
                (*entry.GetDataVector<uint64_t>())[3] = 0;
            }
            else
            {
                entry.data = GlobalAllocator::Allocate(4 * sizeof(uint64_t));
                static_cast<uint64_t*>(entry.data)[3] = 0;
            }
    
            uint8_t currentDimension = 0;
            const uint8_t dimensionCount = static_cast<uint8_t>(entry.size);
    
            uint64_t result = 0;
            for(char c : view)
            {
                if(constexpr_isdigit(c))
                {
                    if(result > UINT64_MAX_VALUE / 10)
                        return false;  // Overflow
    
                    result *= 10;
    
                    const uint64_t digit = static_cast<uint64_t>(c - '0');
                    if(result > UINT64_MAX_VALUE - digit)
                        return false; // Overflow
    
                    result += digit;
                }
                else if(c == '.')
                {
                    if(currentDimension >= dimensionCount - 1)
                        return false;  // Too much dimensions

                    if consteval
                        { (*entry.GetDataVector<uint64_t>())[currentDimension] = result; }
                    else
                        { static_cast<uint64_t*>(entry.data)[currentDimension] = result; }
    
                    result = 0;
                    currentDimension++;
                }
                else
                    return false;  // unknown character
            }

            if consteval
                { (*entry.GetDataVector<uint64_t>())[currentDimension] = result; }
            else
                { static_cast<uint64_t*>(entry.data)[currentDimension] = result; }
            
            //[[maybe_unused]] auto DEBUG = std::span<uint64_t>(static_cast<uint64_t*>(entry.data), entry.size);
            return postProcess();
        }

        


        if consteval
        {
            entry.data = new (std::nothrow) std::string();
            assert(entry.data && "Allocation shouldn't fail");
            entry.GetDataAs<std::string>()->resize(entry.size);
        }
        else
        {
            entry.data = GlobalAllocator::Allocate(entry.size + 1 + sizeof(uint32_t));
            *static_cast<uint32_t*>(entry.data) = entry.size;
        }
        
        size_t size = 0;
        auto writeCharacter = [&](char c)
        {
            if consteval
                { (*entry.GetDataAs<std::string>())[size++] = c; }
            else
                { (static_cast<char*>(entry.data) + sizeof(uint32_t))[size++] = c; }
        };

        if(currentToken.type == TokenType::StringLiteral)
        {
            entry.type = Type::String;

            static constexpr uint32_t start = 1;
                   const     uint32_t end = static_cast<uint32_t>(view.size()) - 1U;

            auto isEscapableChar     = [](char c) -> bool  { return c == '\"' || c == '\'' || c == '\\'; };
            auto isMergeEscapeChar   = [](char c) -> bool  { return c == 'n'  || c == 'r'  || c == 't' || c == 'v' || c == 'b' || c == 'f' || c == 'a'; };
            [[maybe_unused]] auto isUnicodeEscapeChar = [](char c) -> bool  { return c == 'u'  || c == 'U'; };  // TODO: Maybe handle unicode?

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

            for(uint32_t i = start; i < end; i++)
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

            if consteval
                { } // We don't need to write \0 when dealing with std::string
            else
                { writeCharacter('\0'); }

            //[[maybe_unused]] auto DEBUG = std::string_view(static_cast<char*>(entry.data) + sizeof(uint32_t), entry.size);
            return postProcess();
        }




        if(currentToken.type == TokenType::HexLiteral || currentToken.type == TokenType::TimestampLiteral)
        {
            entry.type = currentToken.type == TokenType::HexLiteral? Type::Hex : Type::Timestamp;

            if consteval
            {
                (*entry.GetDataAs<std::string>()) = view;
            }
            else
            {
                constexpr_memcpy((static_cast<char*>(entry.data) + sizeof(uint32_t)), view.data(), entry.size);
                (static_cast<char*>(entry.data) + sizeof(uint32_t))[entry.size] = '\0';
            }
            
            //[[maybe_unused]] auto DEBUG = std::string_view(static_cast<char*>(entry.data) + sizeof(uint32_t), entry.size);
            return postProcess();
        }

        return false;  // Something we didn't process yet?
    }
    template<auto DIAGNOSTIC_CALLBACK>
    [[nodiscard]] constexpr bool Utils<DIAGNOSTIC_CALLBACK>::ParseArray      (Tokenizer& tokenizer, Entry& array   FDF_COMMENT_SWITCH(, Token comment)) noexcept
    {
        assert(tokenizer.Current().type == TokenType::SquareBraceOpen && "Sanity check!");
        
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
                    //    if(!DIAGNOSTIC_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", childComment.ToView(content), childComment.line, childComment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
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
                    //    if(!DIAGNOSTIC_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
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

                //[[maybe_unused]] auto DEBUG = std::span<Entry*>((static_cast<Entry**>(array.data) + 1), array.size);
                return true;
            }
            else
                return false;
        }
    }
    template<auto DIAGNOSTIC_CALLBACK>
    [[nodiscard]] constexpr bool Utils<DIAGNOSTIC_CALLBACK>::ParseMap        (Tokenizer& tokenizer, Entry& map   FDF_COMMENT_SWITCH(, Token comment)) noexcept
    {
        assert(tokenizer.Current().type == TokenType::CurlyBraceOpen && "Sanity check!");
        
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
                    //    if(!DIAGNOSTIC_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", childComment.ToView(content), childComment.line, childComment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
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
                    //    if(!DIAGNOSTIC_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
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

                //[[maybe_unused]] auto DEBUG = std::span<Entry*>((static_cast<Entry**>(map.data) + 1), map.size);
                return true;
            }
            else
                return false;
        }
    }










    template<auto DIAGNOSTIC_CALLBACK>
    template<Style STYLE>
    constexpr void Utils<DIAGNOSTIC_CALLBACK>::WriteBuffer(const Entry& root, std::string& buffer, const bool bOverwrite) noexcept
    {
        [[maybe_unused]] auto isShortArrayFn   = [ ](const Entry& e) -> bool  { return e.GetChildCountRecursive() <= STYLE.singleLineArrayLimit; };
        [[maybe_unused]] auto isShortMapFn     = [ ](const Entry& e) -> bool  { return e.GetChildCountRecursive() <= STYLE.singleLineMapLimit; };
        auto writeEntryNameFn = [&](const Entry& e) -> void  { buffer.append(e.GetIdentifier()); };

        auto addTabFn = [&buffer](uint32_t count) -> void
        {
            if constexpr(STYLE.bUseSpacesOverTabs)
                buffer.append(count * STYLE.tabSize, ' ');
            else
                buffer.append(count, '\t');
        };
        auto addEqualSignFn = [&buffer]() -> void
        {
            if constexpr(STYLE.bSpaceBeforeAndAfterEqualSign)
                buffer.append(" = ");
            else
                buffer.push_back('=');
        };
        [[maybe_unused]] auto addCommaFn = [&buffer]() -> void
        {
            if constexpr(STYLE.bSpaceAfterComma)
                buffer.append(", ");
            else
                buffer.push_back(',');
        };

        std::array<bool, 254> scopes; // max amount of scopes is 254 (depth 0 has no scope, depth 1 has 1 scope, ... and 255 means root, so last possible depth is 254) NOLINT(*-pro-type-member-init)
        uint8_t scopeCount = 0;
        auto addScopeFn    = [&](const bool bIsMap)   -> void { scopes[scopeCount++] = bIsMap; };
        auto removeScopeFn = [&]()                    -> void { scopeCount--; };
        auto getScopeFn    = [&](const uint8_t index) -> bool { return scopes[index]; };

        uint8_t lastDepth = root.depth;
        [[maybe_unused]] Type lastType = root.type;

        const size_t totalChildCount = root.GetChildCountRecursive();
        [[maybe_unused]] size_t writtenCount = 0; //? might be unnecessary
        if(bOverwrite)
            buffer.clear();
        buffer.reserve(buffer.size() + (totalChildCount * 50));


        #if !FDF_NO_COMMENTS
            if constexpr(STYLE.bFileComment)
            {
                if(root.depth == static_cast<uint8_t>(-1))
                {
                    const std::string_view fileComment = root.GetComment();
                    if(!fileComment.empty())
                    {
                        buffer.append("/*#\n");
                        size_t prevNewLinePos = static_cast<size_t>(-1);
                        size_t newLinePos = fileComment.find_first_of('\n');
                        while(newLinePos != std::string::npos)
                        {
                            addTabFn(1);
                            buffer.append(fileComment, prevNewLinePos + 1, newLinePos - prevNewLinePos);
                            prevNewLinePos = newLinePos;
                            newLinePos = fileComment.find_first_of('\n', newLinePos + 1);
                        }
                        addTabFn(1);
                        buffer.append(fileComment, prevNewLinePos + 1);
                        buffer.append("\n*/\n\n\n");
                    }
                }
            }
        #endif










        auto writeSimpleEntryValueFn = [&](const Entry& e) -> void
        {
            std::string temp;
            const std::string_view view = e.DataToView<STYLE>(temp);
            if(e.type != Type::String)
            {
                buffer.append(view);
            }
            else
            {
                //TODO: Check out this logic... wtf is going on here?
                if(bool bContainsQuote = view.find_first_of('\"') != std::string_view::npos) // does it contain double quote?
                {
                    if constexpr(STYLE.bAlwaysUseDoubleQuoteForStrings)
                    {
                        buffer.push_back('\"');
                        for(char c : view)
                        {
                            if(c == '\"')
                                buffer.append("\\\"");
                            else
                                buffer.push_back(c);
                        }
                        buffer.push_back('\"');
                    }
                    else
                    {
                        bContainsQuote = view.find_first_of('\'') != std::string_view::npos; // does it contain single quote?
                        if(bContainsQuote)
                        {
                            buffer.push_back('\"');
                            for(char c : view)
                            {
                                if(c == '\"')
                                    buffer.append("\\\"");
                                else
                                    buffer.push_back(c);
                            }
                            buffer.push_back('\"');
                        }
                        else
                        {
                            buffer.push_back('\'');
                            buffer.append(view);
                            buffer.push_back('\'');
                        }
                    }
                }
                else
                {
                    buffer.push_back('\"');
                    buffer.append(view);
                    buffer.push_back('\"');
                }
            }
        };
        [[maybe_unused]] auto writeSimpleEntryFn = [&](const Entry& e) -> void
        {
            writeEntryNameFn(e);
            addEqualSignFn();
            writeSimpleEntryValueFn(e);
        };










        auto writeFn = [&](const Entry& e) -> void
        {
            if(e.depth == static_cast<uint8_t>(-1))
            {
                lastDepth = 0;
                lastType = Type::Invalid;
                return;
            }

            for(int i = 0; i < lastDepth - e.depth; i++)
            {
                const bool bWasMap = getScopeFn(scopeCount - 1);
                addTabFn(static_cast<uint32_t>(lastDepth - 1 - i));
                buffer.push_back(bWasMap? '}' : ']');
                buffer.push_back('\n');
                removeScopeFn();
            }

            if(!e.IsContainer())
            {
                addTabFn(e.depth);
                if(!e.parent || e.parent->type != Type::Array)
                {
                    writeEntryNameFn(e);
                    addEqualSignFn();
                }
                writeSimpleEntryValueFn(e);
                buffer.push_back('\n');
            }
            else
            {
                buffer.push_back('\n');
                const bool bIsMap = e.type == Type::Map;
                addTabFn(e.depth);
                
                if(!e.parent || e.parent->type != Type::Array)
                {
                    writeEntryNameFn(e);
                    if constexpr(STYLE.bParenthesesOnNewLine)
                    {
                        buffer.push_back('\n');
                        addTabFn(e.depth);
                    }
                }
                
                buffer.push_back(bIsMap? '{' : '[');
                buffer.push_back('\n');
                addScopeFn(bIsMap);
            }
            //TODO: implement
            
            lastDepth = e.depth;
            lastType = e.type;
            writtenCount++;
        };


        if constexpr(STYLE.bGroupSimilarTypes)
        {
            root.ForEach<ForEachFlags::Recursive | ForEachFlags::Group | ForEachFlags::IncludeSelf>(writeFn);
        }
        else
        {
            root.ForEach<ForEachFlags::Recursive | ForEachFlags::IncludeSelf>(writeFn);
        }


        for(int i = 0; i < lastDepth; i++)
        {
            const bool bWasMap = getScopeFn(scopeCount - 1);
            addTabFn(static_cast<uint32_t>(lastDepth - 1 - i));
            buffer.push_back(bWasMap? '}' : ']');
            buffer.push_back('\n');
            removeScopeFn();
        }
    }
}










#undef FDF_EXPORT
#undef FDF_CHECK_TOKEN
#undef FDF_CHECK_TOKEN_FOR_EOF
#undef FDF_FORWARD_ERROR
#undef FDF_COMMENT_SWITCH
