
#pragma once




#if !defined(FDF_USE_CPP_MODULES)
    #define FDF_USE_CPP_MODULES false
#endif
#if !defined(FDF_NO_COMMENTS)
    #define FDF_NO_COMMENTS false
#endif
#if !defined(FDF_EXTENDED_NO_COMMENT_IDENTIFIERS)
    #define FDF_EXTENDED_NO_COMMENT_IDENTIFIERS false
#endif
#if !defined(FDF_DISABLE_SLAB_ALLOCATOR)
    #define FDF_DISABLE_SLAB_ALLOCATOR false
#endif
#if FDF_EXTENDED_NO_COMMENT_IDENTIFIERS && !FDF_NO_COMMENTS
    #warning "FDF_EXTENDED_NO_COMMENT_IDENTIFIERS has no effect unless FDF_NO_COMMENTS is enabled"
#endif

// For tests only
#if FDF_USE_CPP_MODULES && defined(FDF_TESTING)
    #define FDF_EXPORT_INTERNAL export
#else
    #define FDF_EXPORT_INTERNAL
#endif




#if !FDF_USE_CPP_MODULES
    #include <algorithm>
    #include <array>
    #include <bit>
    #include <cassert>
    #include <cctype>
    #include <charconv>
    #include <compare>
    #include <cstddef>
    #include <cstdint>
    #include <filesystem>
    #include <limits>
    #include <format>
    #include <fstream>
    #include <memory>
    #include <ranges>
    #include <span>
    #include <string>
    #include <string_view>
    #include <type_traits>
    #include <utility>
    #include <vector>

    #define FDF_EXPORT
#endif




#define FDF_CHECK_TOKEN(TOKEN)         do { if(TOKEN.type == TokenType::Invalid  ) return false; } while(false)
#define FDF_CHECK_TOKEN_FOR_EOF(TOKEN) do { if(TOKEN.type == TokenType::EndOfFile) return false; } while(false)
#define FDF_FORWARD_ERROR(Cond)        do { if(!(Cond))                            return false; } while(false)

#if FDF_NO_COMMENTS
    #define FDF_COMMENT_SWITCH(...)
#else
    #define FDF_COMMENT_SWITCH(...) __VA_ARGS__
#endif




FDF_EXPORT namespace fdf
{
    enum class Type : uint8_t
    {
        Map,
        Array,
        Null,
        Nil = Null,

        Bool,
        Int,
        Float,

        String,
        Hex,
        Version,
        Timestamp,
        Duration
    };


    struct Style
    {
        // Spacing
        bool bUseSpacesOverTabs = true;
        uint8_t tabSize = 4;
        bool bSpaceBeforeAndAfterEqualSign = false;  // 'k = v' vs 'k=v'
        bool bParenthesesOnNewLine = true;

        // Comment
        bool bFileComment = true;
        bool bEntryComment = true;       // emit per-entry comments (inline when short, leading when long)
        bool bAlignCloseComments = true; // pad consecutive inline comments to a shared column

        // Single-line limits
        uint8_t singleLineCommentLimit  = 80U;  // Characters
        uint8_t singleLineContainerLimit = 80U;  // Characters

        // Containers
        bool bCommas = true;          // trailing comma at the end of each multi-line entry (single-line always uses commas)
        bool bTopLevelCommas = false; // also comma-terminate top-level entries (off = newline-separated)

        // General
        bool bGroupSimilarTypes = false;  // off = preserve source order (stable diffs); on = group by type
        bool bUppercaseHex = true;
        uint8_t intDigitGrouping = 0; // group Int and Float integer-part digits every N
        uint8_t hexDigitGrouping = 0; // group Hex digits every N
        bool bUppercaseTimestamp = true;
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

    // how AddChild resolves an existing direct map key
    enum class DuplicateKeyPolicy : uint8_t
    {
        Reject,
        KeepFirst,
        KeepLast,
        Merge
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
        InvalidIdentifier,
        UnexpectedEndOfFile,
        UnterminatedString,
        UnterminatedComment,
        InvalidComment,
        InvalidNumber,
        InvalidPack,         // malformed pack: dangling '|' or a non-widenable component mix
        InvalidTimestamp,
        InvalidToken,        // generic lexer failure with no more specific reason
        InvalidUtf8,         // non-fatal, bytes still pass through
        InputTooLarge,       // buffer would overflow the 32-bit offsets, refused before parsing
        InvalidDuration,
        DuplicateKey,
        NestingTooDeep,
    };

    // Passed to a DIAGNOSTIC_CALLBACK for every issue found while parsing
    struct Diagnostic
    {
        DiagnosticSeverity severity = DiagnosticSeverity::None;
        DiagnosticType     type     = DiagnosticType::UnexpectedToken;
        std::string_view   message;     // offending text, or a short description
        uint32_t           line   = 0;  // 1-based line number
        uint32_t           column = 0;  // 1-based byte column
        uint32_t           offset = 0;  // byte offset into the source buffer
    };

    class Entry;
    class String;
    class Hex;
    class HexWriter;
    class HexReader;
}





namespace fdf::detail
{
    FDF_EXPORT_INTERNAL class GlobalAllocator;

    template<typename T>
    concept IsValidIDType = std::integral<std::remove_cvref_t<T>> || std::convertible_to<std::remove_cvref_t<T>, std::string_view>;

    template<typename Callable>
    constexpr bool IsValidDiagnosticCallback = std::is_invocable_v<Callable, const Diagnostic&>;

    struct NoDiagnostics
    {
        constexpr void operator()(const Diagnostic&) const noexcept {}
    };

    FDF_EXPORT_INTERNAL struct Token;
    FDF_EXPORT_INTERNAL struct Tokenizer;

    // parser declarations used by Entry and Hex friend lists
    template<typename SINK>
    constexpr void Diagnose(DiagnosticSeverity severity, DiagnosticType type, const Tokenizer& tokenizer, const Token& token, SINK& sink) noexcept;
    constexpr void SkipToNextEntry(Tokenizer& tokenizer, bool bStopAtCloseBrace) noexcept;

    template<typename SINK>
    [[nodiscard]] constexpr bool ParseVariable(Tokenizer& tokenizer, Entry& parent    FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept;
    template<typename SINK>
    [[nodiscard]] constexpr bool ParseSimpleValue(Tokenizer& tokenizer, Entry& entry     FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept;
    template<typename SINK>
    [[nodiscard]] constexpr bool ParseArray(Tokenizer& tokenizer, Entry& array     FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept;
    template<typename SINK>
    [[nodiscard]] constexpr bool ParseMap(Tokenizer& tokenizer, Entry& map       FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept;
    template<Type CONTAINER_TYPE, typename SINK>
    [[nodiscard]] constexpr bool ParseContainer(Tokenizer& tokenizer, Entry& container FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept;
    template<Type CONTAINER_TYPE, typename SINK>
    [[nodiscard]] constexpr bool ParseContainerBody(Tokenizer& tokenizer, Entry& container FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept;

    FDF_EXPORT_INTERNAL struct Token;
    FDF_EXPORT_INTERNAL struct Tokenizer;



    template<typename T>
    concept IsHexScalar = std::integral<T> || (std::floating_point<T> && (sizeof(T) == 4 || sizeof(T) == 8));

    template<typename T>
    inline constexpr size_t HexScalarWidth = std::same_as<T, bool>? 1 : sizeof(T);

    FDF_EXPORT_INTERNAL template<typename T>
    concept HasHexReader = std::is_class_v<T> && requires(HexReader& reader, T& value)
    {
        { ReadHex(reader, value) } noexcept -> std::same_as<bool>;
    };

    FDF_EXPORT_INTERNAL template<typename T>
    concept HasHexWriter = std::is_class_v<T> && requires(HexWriter& writer, const T& value)
    {
        { WriteHex(writer, value) } noexcept -> std::same_as<bool>;
    };

    inline constexpr size_t MAX_IDENTIFIER_LENGTH = FDF_NO_COMMENTS && FDF_EXTENDED_NO_COMMENT_IDENTIFIERS? 38 : 30;

    struct EntryDeleter { static constexpr void operator()(Entry* e) noexcept; };

    inline constexpr auto SIZE_T_MAX_VALUE = std::numeric_limits<  size_t>::max();
    inline constexpr auto INT64_MAX_VALUE  = std::numeric_limits< int64_t>::max();
    inline constexpr auto DOUBLE_MAX_VALUE = std::numeric_limits<  double>::max();

    inline constexpr auto UINT8_MAX_VALUE  = std::numeric_limits< uint8_t>::max();
    inline constexpr auto UINT16_MAX_VALUE = std::numeric_limits<uint16_t>::max();
    inline constexpr auto UINT32_MAX_VALUE = std::numeric_limits<uint32_t>::max();
    inline constexpr auto UINT64_MAX_VALUE = std::numeric_limits<uint64_t>::max();

    // Keep recursive parsing well below the native stack limit
    FDF_EXPORT_INTERNAL inline constexpr uint32_t MAX_PARSE_DEPTH = 256;

    [[nodiscard]] constexpr bool FitsElementCount(const size_t count) noexcept
    {
        assert(count <= UINT32_MAX_VALUE && "pack element count must fit in uint32_t");
        return count <= UINT32_MAX_VALUE;
    }

    // returns the first ill-formed byte offset or size() when valid
    // rejects overlong encodings, surrogates and code points above U+10FFFF
    FDF_EXPORT_INTERNAL [[nodiscard]] constexpr size_t Utf8FirstInvalidByte(std::string_view s) noexcept
    {
        const size_t n = s.size();
        size_t i = 0;
        while(i < n)
        {
            const uint8_t b0 = static_cast<uint8_t>(s[i]);
            if(b0 < 0x80)
            {
                i++;
                continue;
            }

            size_t extra = 0;
            uint8_t lo = 0x80, hi = 0xBF;  // second-byte bounds, narrowed by lead byte
            if(b0 >= 0xC2 && b0 <= 0xDF)       extra = 1;
            else if(b0 == 0xE0)              { extra = 2; lo = 0xA0; }  // else overlong
            else if(b0 >= 0xE1 && b0 <= 0xEC)  extra = 2;
            else if(b0 == 0xED)              { extra = 2; hi = 0x9F; }  // else surrogate
            else if(b0 >= 0xEE && b0 <= 0xEF)  extra = 2;
            else if(b0 == 0xF0)              { extra = 3; lo = 0x90; }  // else overlong
            else if(b0 >= 0xF1 && b0 <= 0xF3)  extra = 3;
            else if(b0 == 0xF4)              { extra = 3; hi = 0x8F; }  // else > U+10FFFF
            else
                return i;  // 0xC0/0xC1, 0xF5-0xFF, stray continuation

            if(i + extra >= n)
                return i;

            if(const uint8_t c1 = static_cast<uint8_t>(s[i + 1]); c1 < lo || c1 > hi)
                return i;
            for(size_t k = 2; k <= extra; k++)
            {
                const uint8_t c = static_cast<uint8_t>(s[i + k]);
                if(c < 0x80 || c > 0xBF)
                    return i;
            }
            i += extra + 1;
        }
        return n;
    }
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





    struct NullType    { consteval NullType()    noexcept = default; };
    struct NilType     { consteval NilType()     noexcept = default; };
    struct ArrayType   { consteval ArrayType()   noexcept = default; };
    struct MapType     { consteval MapType()     noexcept = default; };

    struct Version
    {
        uint32_t bHasRevision : 1 = false;
        uint32_t major        : 31 = 0;
        uint32_t minor             = 0;
        uint32_t patch             = 0;
        uint32_t revision          = 0;

        constexpr bool operator==(const Version&) const noexcept = default;
    };
    static_assert(sizeof(Version) == 16);
    static_assert(alignof(Version) == alignof(uint32_t));


    class HexWriter
    {
    public:
        HexWriter(const HexWriter&) = delete;
        HexWriter(HexWriter&&) = delete;
        HexWriter& operator=(const HexWriter&) = delete;
        HexWriter& operator=(HexWriter&&) = delete;

        template<typename T> requires(detail::IsHexScalar<T> || detail::HasHexWriter<T>)
        [[nodiscard]] constexpr bool Write(const T& value) noexcept;

    private:
        friend class Hex;
        constexpr HexWriter(Hex& target_, const size_t cursor_) noexcept
            : target(&target_), cursor(cursor_)  { }
        constexpr void Poison() noexcept  { bPoisoned = true; }

        Hex* target;
        size_t cursor;
        bool bPoisoned = false;
    };


    class HexReader
    {
    public:
        HexReader(const HexReader&) = delete;
        HexReader(HexReader&&) = delete;
        HexReader& operator=(const HexReader&) = delete;
        HexReader& operator=(HexReader&&) = delete;

        template<typename T> requires(detail::IsHexScalar<T> || detail::HasHexReader<T>)
        [[nodiscard]] constexpr bool Read(T& value) noexcept;

    private:
        friend class Hex;
        constexpr HexReader(const Hex& target_, const size_t cursor_) noexcept
            : target(&target_), cursor(cursor_)  { }
        constexpr void Poison() noexcept  { bPoisoned = true; }

        const Hex* target;
        size_t cursor;
        bool bPoisoned = false;
    };


    // big-endian byte string
    class Hex
    {
    public:
        constexpr Hex() noexcept = default;
        explicit constexpr Hex(std::span<const std::byte> bytes) noexcept  { (void)Assign(bytes); }
        constexpr Hex(const Hex& other) noexcept  { (void)Assign(other.Bytes()); }
        constexpr Hex(Hex&& other) noexcept
            : ptr(other.ptr), size(other.size), capacity(other.capacity)
        {
            other.ptr = nullptr;
            other.size = 0;
            other.capacity = 0;
        }
        constexpr ~Hex() noexcept  { Free(); }

        constexpr Hex& operator=(const Hex& other) noexcept
        {
            if(this != &other)
                (void)Assign(other.Bytes());
            return *this;
        }
        constexpr Hex& operator=(Hex&& other) noexcept
        {
            if(this != &other)
            {
                Free();
                ptr = other.ptr;
                size = other.size;
                capacity = other.capacity;
                other.ptr = nullptr;
                other.size = 0;
                other.capacity = 0;
            }
            return *this;
        }

        [[nodiscard]] static constexpr size_t MaxSize() noexcept  { return detail::UINT32_MAX_VALUE; }

        [[nodiscard]] constexpr size_t Size()    const noexcept  { return size; }
        [[nodiscard]] constexpr bool   IsEmpty() const noexcept  { return size == 0; }
        [[nodiscard]] constexpr size_t DigitCount() const noexcept  { return static_cast<size_t>(size) * 2; }

        [[nodiscard]] constexpr std::span<std::byte>       Bytes()       noexcept  { return { ptr, size }; }
        [[nodiscard]] constexpr std::span<const std::byte> Bytes() const noexcept  { return { ptr, size }; }

        [[nodiscard]] constexpr bool operator==(const Hex& other) const noexcept
        {
            if(size != other.size)
                return false;
            for(uint32_t i = 0; i < size; i++)
            {
                if(ptr[i] != other.ptr[i])
                    return false;
            }
            return true;
        }

        // integers zero-extend, byteOffset == Size() reads nothing and decodes to 0 like 0x does
        template<typename T> requires(detail::IsHexScalar<T> || detail::HasHexReader<T>)
        [[nodiscard]] constexpr bool Read(T& value, size_t byteOffset = 0) const noexcept
        {
            if constexpr(!detail::IsHexScalar<T>)
            {
                if(byteOffset > size)
                    return false;

                HexReader reader(*this, byteOffset);
                const bool bResult = ReadHex(reader, value);
                if(!bResult)
                    reader.Poison();
                return bResult && !reader.bPoisoned;
            }
            else if constexpr(std::same_as<T, bool>)
            {
                if(byteOffset > size)
                    return false;
                value = byteOffset < size && ptr[byteOffset] != std::byte{0};
                return true;
            }
            else if constexpr(std::integral<T>)
            {
                if(byteOffset > size)
                    return false;
                using U = std::make_unsigned_t<T>;
                const size_t count = std::min(sizeof(T), static_cast<size_t>(size) - byteOffset);
                U decoded = 0;
                for(size_t i = 0; i < count; i++)
                    decoded = static_cast<U>(decoded << 8 | std::to_integer<U>(ptr[byteOffset + i]));
                value = static_cast<T>(decoded);
                return true;
            }
            else if constexpr(std::floating_point<T>)
            {
                if(byteOffset > size || sizeof(T) > static_cast<size_t>(size) - byteOffset)
                    return false;
                std::array<std::byte, sizeof(T)> image = {};
                for(size_t i = 0; i < sizeof(T); i++)
                    image[i] = ptr[byteOffset + i];
                if constexpr(std::endian::native == std::endian::little)
                {
                    for(size_t i = 0; i < sizeof(T) / 2; i++)
                        std::swap(image[i], image[sizeof(T) - 1 - i]);
                }
                value = std::bit_cast<T>(image);
                return true;
            }
            else
                { static_assert(false); return false; }
        }

        template<typename T> requires(detail::IsHexScalar<T> || detail::HasHexWriter<T>)
        [[nodiscard]] constexpr bool Write(const T& value) noexcept
        {
            return Write(value, size);
        }

        template<typename T> requires(detail::IsHexScalar<T> || detail::HasHexWriter<T>)
        [[nodiscard]] constexpr bool Write(const T& value, size_t byteOffset) noexcept;

        // Assign replaces, Decode appends, Decode at an offset overwrites. Optional 0x/0X prefix,
        // an odd digit count pads a leading zero nibble, a rejected string leaves the value alone
        [[nodiscard]] constexpr bool Assign(std::span<const std::byte> bytes) noexcept;
        [[nodiscard]] constexpr bool Assign(std::string_view digits) noexcept;
        [[nodiscard]] constexpr bool Decode(std::string_view digits) noexcept;
        [[nodiscard]] constexpr bool Decode(std::string_view digits, size_t byteOffset) noexcept;

    private:
        [[nodiscard]] static constexpr bool StripAndValidate(std::string_view& digits) noexcept;

        // leaves size untouched on failure
        template<typename T>
        [[nodiscard]] constexpr bool WriteScalarAt(const T& value, size_t byteOffset) noexcept;

        [[nodiscard]] constexpr bool TryAssign(std::span<const std::byte> bytes) noexcept;
        [[nodiscard]] constexpr bool Decode_UNSAFE(std::string_view digits, uint32_t byteOffset) noexcept;
        [[nodiscard]] constexpr bool Reallocate(uint32_t byteCount) noexcept;
        constexpr void Free() noexcept;

        template<typename SINK>
        friend constexpr bool detail::ParseVariable(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        template<typename SINK>
        friend constexpr bool detail::ParseSimpleValue(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        template<typename SINK>
        friend constexpr bool detail::ParseArray(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        template<typename SINK>
        friend constexpr bool detail::ParseMap(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        template<Type CONTAINER_TYPE, typename SINK>
        friend constexpr bool detail::ParseContainer(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        template<Type CONTAINER_TYPE, typename SINK>
        friend constexpr bool detail::ParseContainerBody(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        friend class HexWriter;
        friend class HexReader;

        std::byte* ptr = nullptr;
        uint32_t size = 0;
        uint32_t capacity = 0;
    };
    static_assert(sizeof(Hex) == 16);
    static_assert(alignof(Hex) == 8);


    template<typename T> requires(detail::IsHexScalar<T> || detail::HasHexWriter<T>)
    constexpr bool HexWriter::Write(const T& value) noexcept
    {
        if(bPoisoned)
            return false;

        if constexpr(detail::IsHexScalar<T>)
        {
            if(!target->WriteScalarAt(value, cursor))
            {
                Poison();
                return false;
            }
            cursor += detail::HexScalarWidth<T>;
            return true;
        }
        else
        {
            const bool bResult = WriteHex(*this, value);
            if(!bResult)
                Poison();
            return bResult && !bPoisoned;
        }
    }


    template<typename T> requires(detail::IsHexScalar<T> || detail::HasHexReader<T>)
    constexpr bool HexReader::Read(T& value) noexcept
    {
        if(bPoisoned)
            return false;

        if constexpr(detail::IsHexScalar<T>)
        {
            constexpr size_t byteWidth = detail::HexScalarWidth<T>;
            if(cursor > target->size || byteWidth > static_cast<size_t>(target->size) - cursor)
            {
                Poison();
                return false;
            }

            const bool bResult = target->Read(value, cursor);
            if(!bResult)
            {
                Poison();
                return false;
            }
            cursor += byteWidth;
            return true;
        }
        else
        {
            const bool bResult = ReadHex(*this, value);
            if(!bResult)
                Poison();
            return bResult && !bPoisoned;
        }
    }


    template<typename T>
    constexpr bool Hex::WriteScalarAt(const T& value, const size_t byteOffset) noexcept
    {
        constexpr size_t byteWidth = detail::HexScalarWidth<T>;
        // no gaps: an offset past the end has nothing to overwrite
        if(byteOffset > size || byteWidth > MaxSize() - byteOffset)
            return false;

        const uint32_t requiredSize = static_cast<uint32_t>(byteOffset + byteWidth);
        if(requiredSize > size)
        {
            if(!Reallocate(requiredSize))
                return false;
            size = requiredSize;
        }

        if constexpr(std::same_as<T, bool>)
            ptr[byteOffset] = value? std::byte{1} : std::byte{0};
        else if constexpr(std::integral<T>)
        {
            using U = std::make_unsigned_t<T>;
            const U encoded = static_cast<U>(value);
            for(size_t i = 0; i < sizeof(T); i++)
            {
                const size_t shift = (sizeof(T) - 1 - i) * 8;
                ptr[byteOffset + i] = static_cast<std::byte>((encoded >> shift) & static_cast<U>(0xFFu));
            }
        }
        else
        {
            auto image = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
            if constexpr(std::endian::native == std::endian::little)
            {
                for(size_t i = 0; i < sizeof(T) / 2; i++)
                    std::swap(image[i], image[sizeof(T) - 1 - i]);
            }
            for(size_t i = 0; i < sizeof(T); i++)
                ptr[byteOffset + i] = image[i];
        }
        return true;
    }


    template<typename T> requires(detail::IsHexScalar<T> || detail::HasHexWriter<T>)
    constexpr bool Hex::Write(const T& value, const size_t byteOffset) noexcept
    {
        if constexpr(detail::IsHexScalar<T>)
            return WriteScalarAt(value, byteOffset);
        else
        {
            if(byteOffset > size)
                return false;

            // stage at the tail, splice down on success, shrink back on failure
            const uint32_t originalSize = size;
            HexWriter writer(*this, originalSize);
            if(!writer.Write(value))
            {
                size = originalSize;
                return false;
            }

            const size_t consumed = writer.cursor - originalSize;
            if(byteOffset < originalSize && consumed != 0)
            {
                for(size_t i = 0; i < consumed; i++)
                    ptr[byteOffset + i] = ptr[originalSize + i];
                size = static_cast<uint32_t>(std::max<size_t>(originalSize, byteOffset + consumed));
            }
            return true;
        }
    }


    // RFC 3339 timestamp profile
    struct Timestamp
    {
        // RFC 3339 §4.3: -00:00 is UTC with an unknown local offset
        enum class TzKind : uint8_t { None, Utc, Offset, UnknownOffset };

        uint16_t year        = 0;   // 0-9999
        uint8_t  month       = 0;   // 1-12
        uint8_t  day         = 0;   // 1-31
        uint8_t  hour        = 0;   // 0-23
        uint8_t  minute      = 0;   // 0-59
        uint8_t  second      = 0;   // 0-60, 60 is a leap second
        uint8_t  fracDigits  = 0;   // sub-second digits as written, 0-9
        uint32_t nanosecond  = 0;   // 0-999'999'999
        int16_t  tzOffsetMin = 0;   // signed minutes from UTC (Offset kind only)
        TzKind   tzKind      : 2 = TzKind::None;
        bool     bHasDate    : 1 = false;
        bool     bHasTime    : 1 = false;
        bool     bValid      : 1 = false;

        // mutable fields make bValid a claim, so validate the full structure
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            if(!bValid || (!bHasDate && !bHasTime))
                return false;
            if(bHasDate? (year > 9999 || month < 1 || month > 12 || day < 1 || day > DaysInMonth(year, month))
                       : (year != 0 || month != 0 || day != 0))
                return false;
            if(bHasTime? (hour > 23 || minute > 59 || second > 60 || nanosecond > 999'999'999 || fracDigits > 9)
                       : (hour != 0 || minute != 0 || second != 0 || nanosecond != 0 || fracDigits != 0))
                return false;
            // RFC 3339 profile forbids zones on time-only values
            if(tzKind != TzKind::None && !bHasDate)
                return false;
            return tzKind == TzKind::Offset? (tzOffsetMin >= -1439 && tzOffsetMin <= 1439) : tzOffsetMin == 0;
        }
        constexpr bool operator==(const Timestamp&) const noexcept = default;

        // inclusive bounds for years 0-9999
        [[nodiscard]] static constexpr int64_t MinUnixSecond() noexcept  { return DaysFromCivil(0, 1, 1) * 86400; }
        [[nodiscard]] static constexpr int64_t MaxUnixSecond() noexcept  { return DaysFromCivil(9999, 12, 31) * 86400 + 86399; }

        // --- Extract ---------------------------------------------------------------------------
        // Seconds since the Unix epoch (UTC). A None/Utc zone is treated as UTC; an Offset is
        // subtracted to normalize to UTC. With no date the result is just the time-of-day seconds
        [[nodiscard]] constexpr int64_t ToUnixSeconds() const noexcept
        {
            const int64_t days = bHasDate? DaysFromCivil(year, month, day) : 0;
            int64_t secs = days * 86400 + int64_t(hour) * 3600 + int64_t(minute) * 60 + second;
            if(tzKind == TzKind::Offset)
                secs -= int64_t(tzOffsetMin) * 60;
            return secs;
        }
        [[nodiscard]] constexpr int64_t ToUnixMillis() const noexcept  { return ToUnixSeconds() * 1'000LL + nanosecond / 1'000'000; }
        // int64 nanoseconds end around year 2262, clamp outside that range
        [[nodiscard]] constexpr int64_t ToUnixNanos()  const noexcept  { return SaturatingNanos(ToUnixSeconds(), nanosecond); }

        // nanos must be in [0, 1e9), floor bounds preserve the negative boundary second
        [[nodiscard]] static constexpr int64_t SaturatingNanos(const int64_t seconds, const int64_t nanos) noexcept
        {
            constexpr int64_t NS      = 1'000'000'000;
            constexpr int64_t MAX     = std::numeric_limits<int64_t>::max();
            constexpr int64_t MIN     = std::numeric_limits<int64_t>::min();
            constexpr int64_t MAX_SEC = MAX / NS;
            constexpr int64_t MAX_REM = MAX % NS;
            constexpr int64_t MIN_SEC = -MAX_SEC - 1;
            constexpr int64_t MIN_REM = NS - MAX_REM - 1;

            if(seconds > MAX_SEC || (seconds == MAX_SEC && nanos > MAX_REM))
                return MAX;
            if(seconds < MIN_SEC || (seconds == MIN_SEC && nanos < MIN_REM))
                return MIN;
            // avoid overflowing MIN_SEC * NS
            if(seconds == MIN_SEC)
                return MIN + (nanos - MIN_REM);
            return seconds * NS + nanos;
        }

        // --- Inject ----------------------------------------------------------------------------
        [[nodiscard]] static constexpr Timestamp Date(uint16_t y, uint8_t mo, uint8_t d) noexcept
        {
            Timestamp t;
            t.year = y; t.month = mo; t.day = d;
            t.bHasDate = t.bValid = true;
            return t;
        }
        [[nodiscard]] static constexpr Timestamp Time(uint8_t h, uint8_t mi, uint8_t s) noexcept
        {
            Timestamp t;
            t.hour = h; t.minute = mi; t.second = s;
            t.bHasTime = t.bValid = true;
            return t;
        }
        [[nodiscard]] static constexpr Timestamp DateTime(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s) noexcept
        {
            Timestamp t = Date(y, mo, d);
            t.hour = h; t.minute = mi; t.second = s;
            t.bHasTime = true;
            return t;
        }
        [[nodiscard]] static constexpr Timestamp FromUnixSeconds(int64_t s) noexcept
        {
            // CivilFromDays stores the year in uint16_t
            if(s < MinUnixSecond() || s > MaxUnixSecond())
                return Timestamp{};

            const int64_t days = FloorDiv(s, 86400);
            const int64_t rem  = s - days * 86400;
            Timestamp t;
            CivilFromDays(days, t.year, t.month, t.day);
            t.hour   = static_cast<uint8_t>(rem / 3600);
            t.minute = static_cast<uint8_t>(rem % 3600 / 60);
            t.second = static_cast<uint8_t>(rem % 60);
            t.bHasDate = t.bHasTime = t.bValid = true;
            t.tzKind = TzKind::Utc;
            return t;
        }
        [[nodiscard]] static constexpr Timestamp FromUnixNanos(int64_t ns) noexcept
        {
            Timestamp t = FromUnixSeconds(FloorDiv(ns, 1'000'000'000));
            if(!t.bValid)
                return t;
            // subtraction form overflows at INT64_MIN
            const int64_t rem = ns % 1'000'000'000;
            t.nanosecond = static_cast<uint32_t>(rem < 0? rem + 1'000'000'000 : rem);
            t.fracDigits = 9;
            return t;
        }

        // --- Parse / serialize -----------------------------------------------------------------
        [[nodiscard]] static constexpr Timestamp FromText(std::string_view ts) noexcept;
        constexpr void AppendTo(String& out, bool bUppercase = true) const noexcept;

    private:
        [[nodiscard]] static constexpr bool IsLeap(int y) noexcept  { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
        // callers must range-check the month first
        [[nodiscard]] static constexpr uint8_t DaysInMonth(uint16_t y, uint8_t m) noexcept
        {
            constexpr uint8_t DAYS[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
            return m == 2 && IsLeap(y)? uint8_t{ 29 } : DAYS[m - 1];
        }
        // avoid negating INT64_MIN, callers pass a positive divisor
        [[nodiscard]] static constexpr int64_t FloorDiv(int64_t a, int64_t b) noexcept
        {
            const int64_t q = a / b;
            return a % b != 0 && (a < 0) != (b < 0)? q - 1 : q;
        }

        // Howard Hinnant's civil <-> days-since-1970 algorithms (constexpr, no library calls)
        [[nodiscard]] static constexpr int64_t DaysFromCivil(int32_t y, uint32_t m, uint32_t d) noexcept
        {
            y -= m <= 2;
            const int32_t  era = (y >= 0? y : y - 399) / 400;
            const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
            const uint32_t doy = (153 * (m > 2? m - 3 : m + 9) + 2) / 5 + d - 1;
            const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return int64_t(era) * 146097 + int64_t(doe) - 719468;
        }
        static constexpr void CivilFromDays(int64_t z, uint16_t& y, uint8_t& m, uint8_t& d) noexcept
        {
            z += 719468;
            const int64_t  era = (z >= 0? z : z - 146096) / 146097;
            const uint64_t doe = static_cast<uint64_t>(z - era * 146097);
            const uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            const uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const uint64_t mp  = (5 * doy + 2) / 153;
            const uint64_t dd  = doy - (153 * mp + 2) / 5 + 1;
            const uint64_t mm  = mp < 10? mp + 3 : mp - 9;
            y = static_cast<uint16_t>(int64_t(yoe) + era * 400 + (mm <= 2));
            m = static_cast<uint8_t>(mm);
            d = static_cast<uint8_t>(dd);
        }
        static constexpr void AppendPadded(auto& out, uint32_t v, uint8_t width) noexcept
        {
            char buf[10];
            uint8_t n = 0;
            do { buf[n++] = static_cast<char>('0' + v % 10); v /= 10; } while(v > 0);
            for(uint8_t p = n; p < width; p++)
                out.push_back('0');
            while(n > 0)
                out.push_back(buf[--n]);
        }
    };
    static_assert(sizeof(Timestamp) == 16);
    static_assert(alignof(Timestamp) == alignof(uint32_t));
    static_assert(std::is_trivially_copyable_v<Timestamp>);


    // parses and validates in one pass
    // IsValidTimestamp delegates here and errors leave bValid false
    constexpr Timestamp Timestamp::FromText(std::string_view ts) noexcept
    {
        Timestamp t;
        if(ts.empty())
            return t;

        auto digit    = [](char c) noexcept { return c >= '0' && c <= '9'; };
        auto digitsAt = [&](size_t p, size_t n) noexcept -> bool
        {
            if(p + n > ts.size()) return false;
            for(size_t i = p; i < p + n; i++)
                if(!digit(ts[i])) return false;
            return true;
        };
        auto num = [&](size_t p, size_t n) noexcept -> int
        {
            int v = 0;
            for(size_t i = p; i < p + n; i++)
                v = v * 10 + (ts[i] - '0');
            return v;
        };

        size_t pos = 0;

        if(ts.size() >= 10 && digitsAt(0, 4) && ts[4] == '-'
            && digitsAt(5, 2) && ts[7] == '-' && digitsAt(8, 2))
        {
            t.year = static_cast<uint16_t>(num(0, 4));
            const int month = num(5, 2);
            const int day   = num(8, 2);
            if(month < 1 || month > 12)
                return t;
            if(day < 1 || day > DaysInMonth(t.year, static_cast<uint8_t>(month)))
                return t;

            t.month = static_cast<uint8_t>(month);
            t.day = static_cast<uint8_t>(day);
            t.bHasDate = true;
            pos = 10;

            if(pos == ts.size()) { t.bValid = true; return t; }
            if(ts[pos] != 'T' && ts[pos] != 't') return t;
            pos++;
        }
        else if(!digitsAt(0, 2))
            return t;

        // Time part: HH:MM:SS
        if(!digitsAt(pos, 2) || pos + 2 >= ts.size() || ts[pos + 2] != ':' ||
           !digitsAt(pos + 3, 2) || pos + 5 >= ts.size() || ts[pos + 5] != ':' ||
           !digitsAt(pos + 6, 2))
            return t;
        if(num(pos, 2) > 23 || num(pos + 3, 2) > 59 || num(pos + 6, 2) > 60)
            return t;
        t.hour    = static_cast<uint8_t>(num(pos, 2));
        t.minute  = static_cast<uint8_t>(num(pos + 3, 2));
        t.second  = static_cast<uint8_t>(num(pos + 6, 2));
        t.bHasTime = true;
        pos += 8;

        if(pos == ts.size()) { t.bValid = true; return t; }

        // Optional fractional seconds
        if(ts[pos] == '.')
        {
            pos++;
            if(pos >= ts.size() || !digit(ts[pos]))
                return t;
            uint64_t frac = 0;
            while(pos < ts.size() && digit(ts[pos]))
            {
                if(t.fracDigits == 9)
                    return t;
                frac = frac * 10 + static_cast<uint64_t>(ts[pos] - '0');
                t.fracDigits++;
                pos++;
            }
            for(uint8_t i = t.fracDigits; i < 9; i++)
                frac *= 10;
            t.nanosecond = static_cast<uint32_t>(frac);
            if(pos == ts.size()) { t.bValid = true; return t; }
        }

        // Timezone
        if(!t.bHasDate)
            return t;
        if(ts[pos] == 'Z' || ts[pos] == 'z')
        {
            if(pos + 1 != ts.size()) return t;
            t.tzKind = TzKind::Utc;
            t.bValid = true;
            return t;
        }
        if(ts[pos] == '+' || ts[pos] == '-')
        {
            const bool bNeg = ts[pos] == '-';
            pos++;
            if(!(digitsAt(pos, 2) && pos + 2 < ts.size() && ts[pos + 2] == ':' &&
                 digitsAt(pos + 3, 2) && pos + 5 == ts.size()))
                return t;
            const int offHour = num(pos, 2);
            const int offMin  = num(pos + 3, 2);
            if(offHour > 23 || offMin > 59)
                return t;
            const int total = offHour * 60 + offMin;
            t.tzKind = bNeg && total == 0? TzKind::UnknownOffset : TzKind::Offset;
            t.tzOffsetMin = static_cast<int16_t>(bNeg? -total : total);
            t.bValid = true;
            return t;
        }

        return t;
    }


    struct Duration
    {
        int64_t nanoseconds = 0;

        [[nodiscard]] static constexpr Duration Weeks(const int64_t value) noexcept   { return { value * 604'800'000'000'000LL }; }
        [[nodiscard]] static constexpr Duration Days(const int64_t value) noexcept    { return { value * 86'400'000'000'000LL }; }
        [[nodiscard]] static constexpr Duration Hours(const int64_t value) noexcept   { return { value * 3'600'000'000'000LL }; }
        [[nodiscard]] static constexpr Duration Minutes(const int64_t value) noexcept { return { value * 60'000'000'000LL }; }
        [[nodiscard]] static constexpr Duration Seconds(const int64_t value) noexcept { return { value * 1'000'000'000LL }; }
        [[nodiscard]] static constexpr Duration Millis(const int64_t value) noexcept  { return { value * 1'000'000LL }; }
        [[nodiscard]] static constexpr Duration Micros(const int64_t value) noexcept  { return { value * 1'000LL }; }
        [[nodiscard]] static constexpr Duration Nanos(const int64_t value) noexcept   { return { value }; }

        [[nodiscard]] constexpr int64_t TotalNanos() const noexcept   { return nanoseconds; }
        [[nodiscard]] constexpr int64_t TotalMicros() const noexcept  { return nanoseconds / 1'000LL; }
        [[nodiscard]] constexpr int64_t TotalMillis() const noexcept  { return nanoseconds / 1'000'000LL; }
        [[nodiscard]] constexpr int64_t TotalSeconds() const noexcept { return nanoseconds / 1'000'000'000LL; }
        [[nodiscard]] constexpr int64_t TotalMinutes() const noexcept { return nanoseconds / 60'000'000'000LL; }
        [[nodiscard]] constexpr int64_t TotalHours() const noexcept   { return nanoseconds / 3'600'000'000'000LL; }
        [[nodiscard]] constexpr int64_t TotalDays() const noexcept    { return nanoseconds / 86'400'000'000'000LL; }
        [[nodiscard]] constexpr int64_t TotalWeeks() const noexcept   { return nanoseconds / 604'800'000'000'000LL; }

        constexpr bool operator==(const Duration&) const noexcept = default;
        constexpr auto operator<=>(const Duration&) const noexcept = default;

        [[nodiscard]] constexpr Duration operator+(const Duration other) const noexcept { return { nanoseconds + other.nanoseconds }; }
        [[nodiscard]] constexpr Duration operator-(const Duration other) const noexcept { return { nanoseconds - other.nanoseconds }; }
        [[nodiscard]] constexpr Duration operator-() const noexcept                     { return { -nanoseconds }; }
        [[nodiscard]] constexpr Duration operator*(const int64_t scalar) const noexcept  { return { nanoseconds * scalar }; }

        [[nodiscard]] static constexpr Duration FromText(std::string_view text, bool& bValidOut) noexcept
        {
            bValidOut = false;
            if(text.empty())
                return {};

            auto digit = [](const char c) noexcept { return c >= '0' && c <= '9'; };

            const bool bNegative = text[0] == '-';
            size_t pos = bNegative? 1 : 0;
            if(pos == text.size())
                return {};

            struct Group
            {
                std::string_view integer;
                std::string_view fraction;
                uint64_t unitNanos;
            };
            Group groups[8];
            uint8_t groupCount = 0;
            int previousUnit = -1;

            while(pos < text.size())
            {
                const size_t integerBegin = pos;
                while(pos < text.size() && digit(text[pos]))
                    pos++;
                if(pos == integerBegin)
                    return {};

                const std::string_view integer = text.substr(integerBegin, pos - integerBegin);
                std::string_view fraction;
                if(pos < text.size() && text[pos] == '.')
                {
                    const size_t fractionBegin = ++pos;
                    while(pos < text.size() && digit(text[pos]))
                        pos++;
                    if(pos == fractionBegin)
                        return {};
                    fraction = text.substr(fractionBegin, pos - fractionBegin);
                    while(!fraction.empty() && fraction.back() == '0')
                        fraction.remove_suffix(1);
                }

                int unit = -1;
                uint64_t unitNanos = 0;
                const std::string_view remaining = text.substr(pos);
                if(remaining.starts_with("ms"))
                {
                    unit = 5;
                    unitNanos = 1'000'000ULL;
                    pos += 2;
                }
                else if(remaining.starts_with("us"))
                {
                    unit = 6;
                    unitNanos = 1'000ULL;
                    pos += 2;
                }
                else if(remaining.starts_with("ns"))
                {
                    unit = 7;
                    unitNanos = 1ULL;
                    pos += 2;
                }
                else if(pos < text.size())
                {
                    switch(text[pos])
                    {
                        case 'w': unit = 0; unitNanos = 604'800'000'000'000ULL; break;
                        case 'd': unit = 1; unitNanos = 86'400'000'000'000ULL; break;
                        case 'h': unit = 2; unitNanos = 3'600'000'000'000ULL; break;
                        case 'm': unit = 3; unitNanos = 60'000'000'000ULL; break;
                        case 's': unit = 4; unitNanos = 1'000'000'000ULL; break;
                        default: return {};
                    }
                    pos++;
                }
                else
                    return {};

                if(unit <= previousUnit || groupCount == 8)
                    return {};
                previousUnit = unit;
                groups[groupCount++] = { integer, fraction, unitNanos };
            }

            if(groupCount == 0)
                return {};

            const uint64_t limit = bNegative
                ? static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ULL
                : static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
            uint64_t magnitude = 0;

            for(uint8_t groupIndex = 0; groupIndex < groupCount; groupIndex++)
            {
                const Group& group = groups[groupIndex];
                const uint64_t available = (limit - magnitude) / group.unitNanos;
                uint64_t value = 0;
                for(const char c : group.integer)
                {
                    const uint64_t d = static_cast<uint64_t>(c - '0');
                    if(value > available / 10 || (value == available / 10 && d > available % 10))
                        return {};
                    value = value * 10 + d;
                }
                magnitude += value * group.unitNanos;
            }

            size_t maxFractionDigits = 0;
            for(uint8_t groupIndex = 0; groupIndex < groupCount; groupIndex++)
                maxFractionDigits = std::max(maxFractionDigits, groups[groupIndex].fraction.size());

            if(maxFractionDigits != 0)
            {
                uint64_t carry = 0;
                uint64_t fractionalWhole = 0;
                uint64_t place = 1;
                const size_t columnCount = maxFractionDigits + 15;

                for(size_t column = 0; column < columnCount; column++)
                {
                    uint64_t columnValue = carry;
                    for(uint8_t groupIndex = 0; groupIndex < groupCount; groupIndex++)
                    {
                        const Group& group = groups[groupIndex];
                        if(group.fraction.empty())
                            continue;

                        const size_t shift = maxFractionDigits - group.fraction.size();
                        if(column < shift)
                            continue;

                        const size_t productColumn = column - shift;
                        uint64_t unit = group.unitNanos;
                        for(size_t unitDigitIndex = 0; unit != 0; unitDigitIndex++)
                        {
                            if(productColumn >= unitDigitIndex)
                            {
                                const size_t fractionLeastIndex = productColumn - unitDigitIndex;
                                if(fractionLeastIndex < group.fraction.size())
                                {
                                    const uint64_t fractionDigit = static_cast<uint64_t>(
                                        group.fraction[group.fraction.size() - 1 - fractionLeastIndex] - '0');
                                    columnValue += fractionDigit * (unit % 10);
                                }
                            }
                            unit /= 10;
                        }
                    }

                    const uint64_t outputDigit = columnValue % 10;
                    carry = columnValue / 10;
                    if(column < maxFractionDigits)
                    {
                        if(outputDigit != 0)
                            return {};
                    }
                    else
                    {
                        fractionalWhole += outputDigit * place;
                        if(column + 1 < columnCount)
                            place *= 10;
                    }
                }

                if(carry != 0 || fractionalWhole > limit - magnitude)
                    return {};
                magnitude += fractionalWhole;
            }

            Duration result;
            if(!bNegative)
                result.nanoseconds = static_cast<int64_t>(magnitude);
            else if(magnitude == limit)
                result.nanoseconds = std::numeric_limits<int64_t>::min();
            else
                result.nanoseconds = -static_cast<int64_t>(magnitude);

            bValidOut = true;
            return result;
        }

        constexpr void AppendTo(String& out) const noexcept;
    };
    static_assert(sizeof(Duration) == 8);
    static_assert(alignof(Duration) == 8);
    static_assert(std::is_trivially_copyable_v<Duration>);

    namespace detail
    {
        // split into seconds because valid timestamps outlive int64 nanoseconds
        [[nodiscard]] constexpr Timestamp ShiftTimestamp(const Timestamp& timestamp, const int64_t deltaNanos, const bool bNegate) noexcept
        {
            if(!timestamp.IsValid())
                return Timestamp{};

            int64_t seconds = deltaNanos / 1'000'000'000;
            int64_t nanos   = deltaNanos % 1'000'000'000;
            if(bNegate)
            {
                seconds = -seconds;
                nanos = -nanos;
            }
            seconds += timestamp.ToUnixSeconds();
            nanos += int64_t(timestamp.nanosecond);
            if(nanos < 0)
            {
                nanos += 1'000'000'000;
                seconds--;
            }
            else if(nanos >= 1'000'000'000)
            {
                nanos -= 1'000'000'000;
                seconds++;
            }

            if(seconds < Timestamp::MinUnixSecond() || seconds > Timestamp::MaxUnixSecond())
                return Timestamp{};

            Timestamp result = Timestamp::FromUnixSeconds(seconds);
            result.nanosecond = static_cast<uint32_t>(nanos);
            result.fracDigits = 9;
            return result;
        }
    }

    // shifts outside years 0-9999 are invalid, oversized differences saturate
    [[nodiscard]] constexpr Timestamp operator+(const Timestamp& timestamp, const Duration duration) noexcept
    {
        return detail::ShiftTimestamp(timestamp, duration.nanoseconds, false);
    }
    [[nodiscard]] constexpr Timestamp operator-(const Timestamp& timestamp, const Duration duration) noexcept
    {
        return detail::ShiftTimestamp(timestamp, duration.nanoseconds, true);
    }
    [[nodiscard]] constexpr Duration operator-(const Timestamp& a, const Timestamp& b) noexcept
    {
        // invalid operands return zero because Duration has no invalid state
        if(!a.IsValid() || !b.IsValid())
            return {};

        int64_t seconds = a.ToUnixSeconds() - b.ToUnixSeconds();
        int64_t nanos = int64_t(a.nanosecond) - int64_t(b.nanosecond);
        if(nanos < 0)
        {
            nanos += 1'000'000'000;
            seconds--;
        }
        return Duration::Nanos(Timestamp::SaturatingNanos(seconds, nanos));
    }

    using UniqueEntryPtr = std::unique_ptr<Entry, detail::EntryDeleter>;


    // declarations used by Entry's friend list
    template<typename SINK = detail::NoDiagnostics> requires std::is_invocable_v<SINK&, const Diagnostic&>
    [[nodiscard]] constexpr UniqueEntryPtr ParseBuffer(std::string_view content, SINK&& sink = {}) noexcept;
    template<Style STYLE = {}>
    [[nodiscard]] constexpr String WriteBuffer(const Entry& root) noexcept;

    // 8-byte slab-backed mutable string, no SSO: [u32 size][u32 capacity][chars...]['\0']
    class String
    {
    public:
        using value_type = char;
        static constexpr size_t npos = std::string_view::npos;

        constexpr String() noexcept = default;
        constexpr String(std::string_view value) noexcept  { assign(value); }
        constexpr String(size_t count, char ch) noexcept  { resize(count, ch); }
        constexpr String(const String& other) noexcept  { assign(other); }
        constexpr String(String&& other) noexcept
            : ptr(other.ptr)
        {
            other.ptr = nullptr;
        }
        constexpr ~String() noexcept  { Free(); }

        constexpr String& operator=(std::string_view value) noexcept  { return assign(value); }
        constexpr String& operator=(const String& other) noexcept
        {
            if(this != &other)
                assign(other);
            return *this;
        }
        constexpr String& operator=(String&& other) noexcept
        {
            if(this != &other)
            {
                Free();
                ptr = other.ptr;
                other.ptr = nullptr;
            }
            return *this;
        }

        [[nodiscard]] constexpr operator std::string_view() const noexcept  { return View(); }
        [[nodiscard]] explicit operator std::string() const  { return std::string(View()); }

        // Reads
        [[nodiscard]] constexpr size_t      size()     const noexcept  { return View().size(); }
        [[nodiscard]] constexpr size_t      length()   const noexcept  { return View().size(); }
        [[nodiscard]] constexpr bool        empty()    const noexcept  { return View().empty(); }
        [[nodiscard]] constexpr size_t      capacity() const noexcept  { return Capacity(); }
        [[nodiscard]] static constexpr size_t max_size() noexcept  { return detail::UINT32_MAX_VALUE - HEADER_SIZE - 1; }
        [[nodiscard]] constexpr const char* data()     const noexcept  { return View().data(); }
        [[nodiscard]] constexpr char*       data()           noexcept  { return ptr? ptr + HEADER_SIZE : nullptr; }
        [[nodiscard]] constexpr const char* c_str()    const noexcept  { return ptr? ptr + HEADER_SIZE : ""; }

        [[nodiscard]] constexpr char&       operator[](size_t index)       noexcept  { assert(index < size() && "String index out of range"); return ptr[HEADER_SIZE + index]; }
        [[nodiscard]] constexpr const char& operator[](size_t index) const noexcept  { assert(index < size() && "String index out of range"); return ptr[HEADER_SIZE + index]; }
        [[nodiscard]] constexpr char&       front()       noexcept  { assert(!empty() && "front on empty String"); return (*this)[0]; }
        [[nodiscard]] constexpr const char& front() const noexcept  { assert(!empty() && "front on empty String"); return (*this)[0]; }
        [[nodiscard]] constexpr char&       back()        noexcept  { assert(!empty() && "back on empty String"); return (*this)[size() - 1]; }
        [[nodiscard]] constexpr const char& back()  const noexcept  { assert(!empty() && "back on empty String"); return (*this)[size() - 1]; }

        [[nodiscard]] constexpr char*       begin()        noexcept  { return data(); }
        [[nodiscard]] constexpr char*       end()          noexcept  { return empty()? data() : data() + size(); }
        [[nodiscard]] constexpr const char* begin()  const noexcept  { return View().data(); }
        [[nodiscard]] constexpr const char* end()    const noexcept  { const auto v = View(); return v.empty()? v.data() : v.data() + v.size(); }
        [[nodiscard]] constexpr const char* cbegin() const noexcept  { return begin(); }
        [[nodiscard]] constexpr const char* cend()   const noexcept  { return end(); }

        [[nodiscard]] constexpr size_t find(std::string_view s, size_t pos = 0)    const noexcept  { return View().find(s, pos); }
        [[nodiscard]] constexpr size_t find(char c, size_t pos = 0)                const noexcept  { return View().find(c, pos); }
        [[nodiscard]] constexpr size_t rfind(std::string_view s, size_t pos = npos) const noexcept  { return View().rfind(s, pos); }
        [[nodiscard]] constexpr size_t rfind(char c, size_t pos = npos)             const noexcept  { return View().rfind(c, pos); }
        [[nodiscard]] constexpr size_t find_first_of(std::string_view s, size_t pos = 0)     const noexcept  { return View().find_first_of(s, pos); }
        [[nodiscard]] constexpr size_t find_first_of(char c, size_t pos = 0)                 const noexcept  { return View().find_first_of(c, pos); }
        [[nodiscard]] constexpr size_t find_last_of(std::string_view s, size_t pos = npos)   const noexcept  { return View().find_last_of(s, pos); }
        [[nodiscard]] constexpr size_t find_last_of(char c, size_t pos = npos)               const noexcept  { return View().find_last_of(c, pos); }
        [[nodiscard]] constexpr size_t find_first_not_of(std::string_view s, size_t pos = 0)   const noexcept  { return View().find_first_not_of(s, pos); }
        [[nodiscard]] constexpr size_t find_first_not_of(char c, size_t pos = 0)               const noexcept  { return View().find_first_not_of(c, pos); }
        [[nodiscard]] constexpr size_t find_last_not_of(std::string_view s, size_t pos = npos) const noexcept  { return View().find_last_not_of(s, pos); }
        [[nodiscard]] constexpr size_t find_last_not_of(char c, size_t pos = npos)             const noexcept  { return View().find_last_not_of(c, pos); }
        [[nodiscard]] constexpr bool starts_with(std::string_view s) const noexcept  { return View().starts_with(s); }
        [[nodiscard]] constexpr bool starts_with(char c)             const noexcept  { return View().starts_with(c); }
        [[nodiscard]] constexpr bool ends_with(std::string_view s)   const noexcept  { return View().ends_with(s); }
        [[nodiscard]] constexpr bool ends_with(char c)               const noexcept  { return View().ends_with(c); }
        [[nodiscard]] constexpr bool contains(std::string_view s)    const noexcept  { return View().contains(s); }
        [[nodiscard]] constexpr bool contains(char c)                const noexcept  { return View().contains(c); }
        [[nodiscard]] constexpr int  compare(std::string_view s)     const noexcept  { return View().compare(s); }
        [[nodiscard]] constexpr int  compare(size_t pos, size_t count, std::string_view s) const noexcept  { return substr(pos, count).compare(s); }
        [[nodiscard]] constexpr bool IsValidUtf8()                   const noexcept  { return detail::Utf8FirstInvalidByte(View()) == size(); }

        // Returns a view into this block, dangles on the next mutation
        [[nodiscard]] constexpr std::string_view substr(size_t pos = 0, size_t count = npos) const noexcept
        {
            std::string_view v = View();
            if(pos > v.size())
            {
                assert(false && "substr pos out of range");
                pos = v.size();
            }
            return v.substr(pos, count);
        }

        // exact String overloads avoid MSVC C2666 from rewritten reversed candidates
        [[nodiscard]] constexpr bool operator==(const String& other) const noexcept  { return View() == std::string_view(other); }
        [[nodiscard]] constexpr std::strong_ordering operator<=>(const String& other) const noexcept  { return View() <=> std::string_view(other); }
        [[nodiscard]] constexpr bool operator==(std::string_view value) const noexcept  { return View() == value; }
        [[nodiscard]] constexpr std::strong_ordering operator<=>(std::string_view value) const noexcept  { return View() <=> value; }

        // Edits
        constexpr String& assign(std::string_view value) noexcept
        {
            assert(value.size() <= max_size() && "String size overflow");
            if(AliasesBlock(value))
            {
                const size_t sourceOffset = static_cast<size_t>(value.data() - data());
                const uint32_t newSize = static_cast<uint32_t>(value.size());
                for(uint32_t i = 0; i < newSize; i++)
                    ptr[HEADER_SIZE + i] = ptr[HEADER_SIZE + sourceOffset + i];
                StoreU32(0, newSize);
                ptr[HEADER_SIZE + newSize] = '\0';
                return *this;
            }
            const uint32_t newSize = static_cast<uint32_t>(value.size());
            if(newSize > Capacity())
                Grow(newSize);
            for(uint32_t i = 0; i < newSize; i++)
                ptr[HEADER_SIZE + i] = value[i];
            if(ptr)
            {
                StoreU32(0, newSize);
                ptr[HEADER_SIZE + newSize] = '\0';
            }
            return *this;
        }

        constexpr String& append(std::string_view value) noexcept
        {
            if(value.empty())
                return *this;
            const bool bAliasesBlock = AliasesBlock(value);
            const size_t sourceOffset = bAliasesBlock? static_cast<size_t>(value.data() - data()) : 0;
            const size_t oldSize = size();
            const size_t newSize = oldSize + value.size();
            assert(newSize <= max_size() && "String size overflow");
            if(newSize > Capacity())
                Grow(static_cast<uint32_t>(newSize));
            const char* const source = bAliasesBlock? data() + sourceOffset : value.data();
            for(size_t i = 0; i < value.size(); i++)
                ptr[HEADER_SIZE + oldSize + i] = source[i];
            StoreU32(0, static_cast<uint32_t>(newSize));
            ptr[HEADER_SIZE + newSize] = '\0';
            return *this;
        }

        constexpr String& append(const char* first, const char* last) noexcept
        {
            assert(first <= last);
            return append(std::string_view(first, static_cast<size_t>(last - first)));
        }

        constexpr String& append(size_t count, char ch) noexcept
        {
            return insert(size(), count, ch);
        }

        constexpr void push_back(char c) noexcept  { (void)append(std::string_view(&c, 1)); }
        constexpr void pop_back() noexcept
        {
            assert(!empty() && "pop_back on empty String");
            const uint32_t newSize = static_cast<uint32_t>(size()) - 1;
            StoreU32(0, newSize);
            ptr[HEADER_SIZE + newSize] = '\0';
        }

        constexpr String& operator+=(std::string_view value) noexcept  { return append(value); }
        constexpr String& operator+=(char c) noexcept  { push_back(c); return *this; }

        constexpr String& insert(size_t pos, std::string_view value) noexcept
        {
            assert(pos <= size() && "insert pos out of range");
            if(value.empty())
                return *this;
            if(AliasesBlock(value))
            {
                String temp(value);
                return insert(pos, std::string_view(temp));
            }
            const size_t oldSize = size();
            const size_t newSize = oldSize + value.size();
            assert(newSize <= max_size() && "String size overflow");
            if(newSize > Capacity())
                Grow(static_cast<uint32_t>(newSize));
            for(size_t i = oldSize; i > pos; i--)
                ptr[HEADER_SIZE + i - 1 + value.size()] = ptr[HEADER_SIZE + i - 1];
            for(size_t i = 0; i < value.size(); i++)
                ptr[HEADER_SIZE + pos + i] = value[i];
            StoreU32(0, static_cast<uint32_t>(newSize));
            ptr[HEADER_SIZE + newSize] = '\0';
            return *this;
        }

        constexpr String& insert(size_t pos, size_t count, char ch) noexcept
        {
            assert(pos <= size() && "insert pos out of range");
            if(count == 0)
                return *this;
            const size_t oldSize = size();
            const size_t newSize = oldSize + count;
            assert(newSize <= max_size() && "String size overflow");
            if(newSize > Capacity())
                Grow(static_cast<uint32_t>(newSize));
            for(size_t i = oldSize; i > pos; i--)
                ptr[HEADER_SIZE + i - 1 + count] = ptr[HEADER_SIZE + i - 1];
            for(size_t i = 0; i < count; i++)
                ptr[HEADER_SIZE + pos + i] = ch;
            StoreU32(0, static_cast<uint32_t>(newSize));
            ptr[HEADER_SIZE + newSize] = '\0';
            return *this;
        }

        constexpr String& erase(size_t pos = 0, size_t count = npos) noexcept
        {
            assert(pos <= size() && "erase pos out of range");
            const size_t oldSize = size();
            const size_t removed = std::min(count, oldSize - pos);
            for(size_t i = pos + removed; i < oldSize; i++)
                ptr[HEADER_SIZE + i - removed] = ptr[HEADER_SIZE + i];
            if(ptr)
            {
                const uint32_t newSize = static_cast<uint32_t>(oldSize - removed);
                StoreU32(0, newSize);
                ptr[HEADER_SIZE + newSize] = '\0';
            }
            return *this;
        }

        constexpr String& replace(size_t pos, size_t count, std::string_view value) noexcept
        {
            assert(pos <= size() && "replace pos out of range");
            if(AliasesBlock(value))
            {
                String temp(value);
                return replace(pos, count, std::string_view(temp));
            }
            const size_t oldSize = size();
            const size_t removed = std::min(count, oldSize - pos);
            const size_t tailStart = pos + removed;
            const size_t tailLen = oldSize - tailStart;
            const size_t newSize = oldSize - removed + value.size();
            assert(newSize <= max_size() && "String size overflow");
            if(newSize > Capacity())
                Grow(static_cast<uint32_t>(newSize));
            if(value.size() > removed)
            {
                for(size_t i = 0; i < tailLen; i++)
                    ptr[HEADER_SIZE + newSize - 1 - i] = ptr[HEADER_SIZE + oldSize - 1 - i];
            }
            else if(value.size() < removed)
            {
                for(size_t i = 0; i < tailLen; i++)
                    ptr[HEADER_SIZE + pos + value.size() + i] = ptr[HEADER_SIZE + tailStart + i];
            }
            for(size_t i = 0; i < value.size(); i++)
                ptr[HEADER_SIZE + pos + i] = value[i];
            if(ptr)
            {
                StoreU32(0, static_cast<uint32_t>(newSize));
                ptr[HEADER_SIZE + newSize] = '\0';
            }
            return *this;
        }

        constexpr String& replace(size_t pos, size_t count, size_t replacementCount, char ch) noexcept
        {
            assert(pos <= size() && "replace pos out of range");
            const size_t oldSize = size();
            const size_t removed = std::min(count, oldSize - pos);
            const size_t tailStart = pos + removed;
            const size_t tailLen = oldSize - tailStart;
            const size_t newSize = oldSize - removed + replacementCount;
            assert(newSize <= max_size() && "String size overflow");
            if(newSize > Capacity())
                Grow(static_cast<uint32_t>(newSize));
            if(replacementCount > removed)
            {
                for(size_t i = 0; i < tailLen; i++)
                    ptr[HEADER_SIZE + newSize - 1 - i] = ptr[HEADER_SIZE + oldSize - 1 - i];
            }
            else if(replacementCount < removed)
            {
                for(size_t i = 0; i < tailLen; i++)
                    ptr[HEADER_SIZE + pos + replacementCount + i] = ptr[HEADER_SIZE + tailStart + i];
            }
            for(size_t i = 0; i < replacementCount; i++)
                ptr[HEADER_SIZE + pos + i] = ch;
            if(ptr)
            {
                StoreU32(0, static_cast<uint32_t>(newSize));
                ptr[HEADER_SIZE + newSize] = '\0';
            }
            return *this;
        }

        constexpr void clear() noexcept
        {
            if(ptr)
            {
                StoreU32(0, 0);
                ptr[HEADER_SIZE] = '\0';
            }
        }

        constexpr void resize(size_t n, char ch = ' ') noexcept
        {
            assert(n <= max_size() && "String size overflow");
            const size_t oldSize = size();
            if(n > Capacity())
                Grow(static_cast<uint32_t>(n));
            for(size_t i = oldSize; i < n; i++)
                ptr[HEADER_SIZE + i] = ch;
            if(ptr)
            {
                StoreU32(0, static_cast<uint32_t>(n));
                ptr[HEADER_SIZE + n] = '\0';
            }
        }

        constexpr void reserve(size_t newCapacity) noexcept
        {
            assert(newCapacity <= max_size() && "String capacity overflow");
            if(newCapacity > Capacity())
                Grow(static_cast<uint32_t>(newCapacity));
        }

        constexpr void swap(String& other) noexcept
        {
            std::swap(ptr, other.ptr);
        }

    private:
        static constexpr uint32_t HEADER_SIZE = 2 * sizeof(uint32_t);

        [[nodiscard]] constexpr std::string_view View() const noexcept  { return ptr? std::string_view{ptr + HEADER_SIZE, LoadU32(0)} : std::string_view{}; }

        // detects source views that a mutation may move or free
        [[nodiscard]] constexpr bool AliasesBlock(std::string_view value) const noexcept
        {
            if(!ptr || value.empty())
                return false;
            const char* const first = ptr + HEADER_SIZE;
            const char* const last = first + Capacity() + 1;
            if consteval
            {
                // constexpr cannot order pointers from different allocations
                for(const char* p = first; p != last; ++p)
                    if(p == value.data())
                        return true;
                return false;
            }
            return value.data() >= first && value.data() < last;
        }

        [[nodiscard]] constexpr uint32_t LoadU32(uint32_t offset) const noexcept
        {
            return  static_cast<uint32_t>(static_cast<unsigned char>(ptr[offset]))
                 | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[offset + 1])) << 8)
                 | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[offset + 2])) << 16)
                 | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[offset + 3])) << 24);
        }
        constexpr void StoreU32(uint32_t offset, uint32_t value) noexcept
        {
            ptr[offset]     = static_cast<char>(value & 0xFFu);
            ptr[offset + 1] = static_cast<char>((value >> 8) & 0xFFu);
            ptr[offset + 2] = static_cast<char>((value >> 16) & 0xFFu);
            ptr[offset + 3] = static_cast<char>((value >> 24) & 0xFFu);
        }

        [[nodiscard]] constexpr uint32_t Capacity() const noexcept  { return ptr? LoadU32(sizeof(uint32_t)) : 0; }

        constexpr char* AppendUninitialized(size_t count) noexcept
        {
            assert(count != 0 && "Cannot append an empty uninitialized range");
            const size_t oldSize = size();
            const size_t newSize = oldSize + count;
            assert(newSize <= max_size() && "String size overflow");
            if(newSize > Capacity())
                Grow(static_cast<uint32_t>(newSize));
            StoreU32(0, static_cast<uint32_t>(newSize));
            ptr[HEADER_SIZE + newSize] = '\0';
            return ptr + HEADER_SIZE + oldSize;
        }

        constexpr void Grow(uint32_t minCapacity) noexcept;
        constexpr void Free() noexcept;

        friend class Entry;
        char* ptr = nullptr;
    };

    constexpr void Timestamp::AppendTo(String& out, const bool bUppercase) const noexcept
    {
        assert(IsValid() && "Writer requires a valid Timestamp");
        if(bHasDate)
        {
            AppendPadded(out, year, 4);
            out.push_back('-');
            AppendPadded(out, month, 2);
            out.push_back('-');
            AppendPadded(out, day, 2);
            if(bHasTime)
                out.push_back(bUppercase? 'T' : 't');
        }
        if(bHasTime)
        {
            AppendPadded(out, hour, 2);
            out.push_back(':');
            AppendPadded(out, minute, 2);
            out.push_back(':');
            AppendPadded(out, second, 2);
            if(fracDigits > 0)
            {
                uint32_t scale = 1;
                for(uint8_t i = fracDigits; i < 9; i++)
                    scale *= 10;
                out.push_back('.');
                AppendPadded(out, nanosecond / scale, fracDigits);
            }
        }
        if(tzKind == TzKind::Utc)
            out.push_back(bUppercase? 'Z' : 'z');
        else if(tzKind == TzKind::UnknownOffset)
            out.append("-00:00");
        else if(tzKind == TzKind::Offset)
        {
            out.push_back(tzOffsetMin < 0? '-' : '+');
            const uint32_t abs = static_cast<uint32_t>(tzOffsetMin < 0? -tzOffsetMin : tzOffsetMin);
            AppendPadded(out, abs / 60, 2);
            out.push_back(':');
            AppendPadded(out, abs % 60, 2);
        }
    }

    namespace detail
    {
        constexpr void AppendUInt(String& s, uint64_t v) noexcept;
    }

    constexpr void Duration::AppendTo(String& out) const noexcept
    {
        uint64_t remaining;
        if(nanoseconds < 0)
        {
            out.push_back('-');
            remaining = static_cast<uint64_t>(-(nanoseconds + 1)) + 1ULL;
        }
        else
            remaining = static_cast<uint64_t>(nanoseconds);

        struct Component
        {
            uint64_t unitNanos;
            std::string_view suffix;
        };
        constexpr Component COMPONENTS[] =
        {
            { 86'400'000'000'000ULL, "d" },
            { 3'600'000'000'000ULL, "h" },
            { 60'000'000'000ULL, "m" },
            { 1'000'000'000ULL, "s" },
            { 1'000'000ULL, "ms" },
            { 1'000ULL, "us" },
            { 1ULL, "ns" },
        };

        if(remaining == 0)
        {
            out.append("0s");
            return;
        }

        for(const Component& component : COMPONENTS)
        {
            const uint64_t count = remaining / component.unitNanos;
            if(count == 0)
                continue;
            detail::AppendUInt(out, count);
            out.append(component.suffix);
            remaining %= component.unitNanos;
        }
    }

    [[nodiscard]] constexpr String operator+(const String& a, const String& b) noexcept
    {
        String result;
        result.reserve(a.size() + b.size());
        result.append(a);
        result.append(b);
        return result;
    }
    [[nodiscard]] constexpr String operator+(const String& a, std::string_view b) noexcept
    {
        String result;
        result.reserve(a.size() + b.size());
        result.append(a);
        result.append(b);
        return result;
    }
    [[nodiscard]] constexpr String operator+(std::string_view a, const String& b) noexcept
    {
        String result;
        result.reserve(a.size() + b.size());
        result.append(a);
        result.append(b);
        return result;
    }
    [[nodiscard]] constexpr String operator+(const String& a, char b) noexcept
    {
        String result;
        result.reserve(a.size() + 1);
        result.append(a);
        result.push_back(b);
        return result;
    }
    [[nodiscard]] constexpr String operator+(char a, const String& b) noexcept
    {
        String result;
        result.reserve(1 + b.size());
        result.push_back(a);
        result.append(b);
        return result;
    }
    [[nodiscard]] constexpr String operator+(String&& a, const String& b) noexcept  { a.append(b); return std::move(a); }
    [[nodiscard]] constexpr String operator+(String&& a, std::string_view b) noexcept  { a.append(b); return std::move(a); }
    [[nodiscard]] constexpr String operator+(String&& a, char b) noexcept  { a.push_back(b); return std::move(a); }
    [[nodiscard]] constexpr String operator+(std::string_view a, String&& b) noexcept  { b.insert(0, a); return std::move(b); }
    [[nodiscard]] constexpr String operator+(char a, String&& b) noexcept  { b.insert(0, std::string_view(&a, 1)); return std::move(b); }

    // GetValue<uint64_t> returns this: an unsigned lens over the int64_t storage
    // per-element bit_cast keeps it constexpr-legal where a reinterpreted std::span would not be
    template<bool IS_CONST>
    class BasicUIntSpan
    {
        using INT64 = std::conditional_t<IS_CONST, const int64_t, int64_t>;

    public:
        // proxy element so writes land in the underlying signed buffer
        struct Ref
        {
            INT64* p;

            constexpr operator uint64_t() const noexcept  { return std::bit_cast<uint64_t>(*p); }

            constexpr const Ref& operator=(const uint64_t value) const noexcept requires(!IS_CONST)
            {
                *p = std::bit_cast<int64_t>(value);
                return *this;
            }
        };

        struct Iterator
        {
            using iterator_category = std::random_access_iterator_tag;
            using value_type = uint64_t;
            using difference_type = ptrdiff_t;

            INT64* p = nullptr;

            constexpr uint64_t operator*() const noexcept  { return std::bit_cast<uint64_t>(*p); }
            constexpr uint64_t operator[](const difference_type n) const noexcept  { return std::bit_cast<uint64_t>(p[n]); }

            constexpr Iterator& operator++()    noexcept  { ++p; return *this; }
            constexpr Iterator  operator++(int) noexcept  { return Iterator{ p++ }; }
            constexpr Iterator& operator--()    noexcept  { --p; return *this; }
            constexpr Iterator  operator--(int) noexcept  { return Iterator{ p-- }; }
            constexpr Iterator& operator+=(const difference_type n) noexcept  { p += n; return *this; }
            constexpr Iterator& operator-=(const difference_type n) noexcept  { p -= n; return *this; }

            friend constexpr Iterator operator+(Iterator it, const difference_type n) noexcept  { it.p += n; return it; }
            friend constexpr Iterator operator+(const difference_type n, Iterator it) noexcept  { it.p += n; return it; }
            friend constexpr Iterator operator-(Iterator it, const difference_type n) noexcept  { it.p -= n; return it; }
            friend constexpr difference_type operator-(const Iterator a, const Iterator b) noexcept  { return a.p - b.p; }

            friend constexpr bool operator==(const Iterator& a, const Iterator& b) noexcept = default;
            friend constexpr auto operator<=>(const Iterator& a, const Iterator& b) noexcept = default;
        };

        constexpr BasicUIntSpan() noexcept = default;
        constexpr BasicUIntSpan(INT64* _data, const size_t _size) noexcept : ptr(_data), count(_size)  { }

        constexpr operator BasicUIntSpan<true>() const noexcept requires(!IS_CONST)  { return { ptr, count }; }

        [[nodiscard]] constexpr size_t size()  const noexcept  { return count; }
        [[nodiscard]] constexpr bool   empty() const noexcept  { return count == 0; }

        [[nodiscard]] constexpr uint64_t operator[](const size_t i) const noexcept requires(IS_CONST)   { return std::bit_cast<uint64_t>(ptr[i]); }
        [[nodiscard]] constexpr Ref      operator[](const size_t i) const noexcept requires(!IS_CONST)  { return Ref{ ptr + i }; }
        [[nodiscard]] constexpr auto front() const noexcept  { return (*this)[0]; }
        [[nodiscard]] constexpr auto back()  const noexcept  { return (*this)[count - 1]; }

        [[nodiscard]] constexpr Iterator begin() const noexcept  { return Iterator{ ptr }; }
        [[nodiscard]] constexpr Iterator end()   const noexcept  { return Iterator{ ptr + count }; }

        [[nodiscard]] constexpr BasicUIntSpan first(const size_t n) const noexcept  { return { ptr, n }; }
        [[nodiscard]] constexpr BasicUIntSpan last(const size_t n)  const noexcept  { return { ptr + (count - n), n }; }
        [[nodiscard]] constexpr BasicUIntSpan subspan(const size_t offset, const size_t n) const noexcept  { return { ptr + offset, n }; }

        // runtime-only escape hatch, signed/unsigned aliasing is permitted so the cast is well-defined
        [[nodiscard]] const uint64_t* data() const noexcept requires(IS_CONST)   { return reinterpret_cast<const uint64_t*>(ptr); }
        [[nodiscard]] uint64_t*       data() const noexcept requires(!IS_CONST)  { return reinterpret_cast<uint64_t*>(ptr); }

    private:
        INT64* ptr = nullptr;
        size_t count = 0;
    };

    using UIntSpan = BasicUIntSpan<false>;
    using ConstUIntSpan = BasicUIntSpan<true>;

    static_assert(std::random_access_iterator<UIntSpan::Iterator>);

    class Entry
    {
    public:
        // value moves would invalidate raw parent/child links
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        Entry(Entry&&) = delete;
        Entry& operator=(Entry&&) = delete;

        // public because MSVC rejects friend access to a private ctor in constexpr new
        // the dtor stays private, construct through NewEntry/Emplace
        constexpr Entry() noexcept;

    private:
        constexpr ~Entry() noexcept;

        friend constexpr UniqueEntryPtr NewEntry() noexcept;

        template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
        friend constexpr UniqueEntryPtr ParseBuffer(std::string_view, SINK&&) noexcept;
        template<typename SINK>
        friend constexpr bool detail::ParseVariable(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        template<typename SINK>
        friend constexpr bool detail::ParseSimpleValue(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        template<typename SINK>
        friend constexpr bool detail::ParseArray(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        template<typename SINK>
        friend constexpr bool detail::ParseMap(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        template<Type CONTAINER_TYPE, typename SINK>
        friend constexpr bool detail::ParseContainer(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        template<Type CONTAINER_TYPE, typename SINK>
        friend constexpr bool detail::ParseContainerBody(detail::Tokenizer&, Entry& FDF_COMMENT_SWITCH(, detail::Token), SINK&) noexcept;
        template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
        friend constexpr UniqueEntryPtr ParseBuffer(std::string_view, SINK&&) noexcept;
        template<Style STYLE>
        friend constexpr String WriteBuffer(const Entry&) noexcept;

        friend class detail::GlobalAllocator;

    private:
        char identifier[detail::MAX_IDENTIFIER_LENGTH + 1] = {};
        Type type = Type::Map;
        uint32_t size = 0;
        uint32_t capacity = 0;  // runtime may exceed the request by slab bucket slack
        Entry* parent = nullptr;

        // Union of typed pointers, constexpr can't cast void*->T* (MSVC)
        // TODO: collapse to `void* data` once MSVC allows void*->T* in constant expressions
        union DataPtr
        {
            Entry** e = nullptr;
            bool* b;
            int64_t* i;
            Version* v;
            double* f;
            Timestamp* t;
            Duration* dur;
            String* s;
            Hex* h;
        };
        DataPtr data;
    #if !FDF_NO_COMMENTS
        String comment;
    #endif

    private:
        // read the active union member selected by type
        [[nodiscard]] constexpr bool IsDataNull() const noexcept
        {
            switch(type)
            {
                case Type::Bool:                          return !data.b;
                case Type::Int:                           return !data.i;
                case Type::Version:                       return !data.v;
                case Type::Float:                         return !data.f;
                case Type::Hex:                           return !data.h;
                case Type::Timestamp:                     return !data.t;
                case Type::Duration:                      return !data.dur;
                case Type::String:                        return !data.s;
                case Type::Array:  case Type::Map:        return !data.e;
                default:                                  return true;   // Null / Nil hold no data
            }
        }

        // match the active union member to type for constexpr reads
        constexpr void ResetDataNull() noexcept
        {
            switch(type)
            {
                case Type::Bool:                          data.b = nullptr; break;
                case Type::Int:                           data.i = nullptr; break;
                case Type::Version:                       data.v = nullptr; break;
                case Type::Float:                         data.f = nullptr; break;
                case Type::Hex:                           data.h = nullptr; break;
                case Type::Timestamp:                     data.t = nullptr; break;
                case Type::Duration:                      data.dur = nullptr; break;
                case Type::String:                        data.s = nullptr; break;
                case Type::Array:  case Type::Map:        data.e = nullptr; break;
                default:                                  data.e = nullptr; break;
            }
        }


        [[nodiscard]] constexpr uint8_t GetIdentifierSize()                    const noexcept  { return static_cast<uint8_t>(detail::MAX_IDENTIFIER_LENGTH) - static_cast<uint8_t>(identifier[detail::MAX_IDENTIFIER_LENGTH]); }
                      constexpr void    SetIdentifierSize(const uint8_t value)       noexcept  { identifier[detail::MAX_IDENTIFIER_LENGTH] = static_cast<char>(detail::MAX_IDENTIFIER_LENGTH - value); }

    public:
        [[nodiscard]] constexpr uint32_t GetChildCount() const noexcept  { return IsContainer()? size : 0; }
        [[nodiscard]] constexpr Type     GetType()       const noexcept  { return type; }
        [[nodiscard]] constexpr bool     IsNull()        const noexcept  { return type == Type::Null; }
        [[nodiscard]] constexpr bool     IsNil()         const noexcept  { return IsNull(); }
        [[nodiscard]] constexpr bool     IsContainer()   const noexcept  { return type == Type::Array || type == Type::Map; }
        [[nodiscard]] constexpr bool     HasValue()      const noexcept  { return !IsNull() && !IsContainer(); }
        [[nodiscard]] constexpr Entry*   GetParent()           noexcept  { return parent; }
        [[nodiscard]] constexpr Entry*   GetParent()     const noexcept  { return parent; }
        [[nodiscard]] constexpr uint32_t CalculateDepth() const noexcept
        {
            const Entry* e = parent;
            uint32_t depth = 0;
            while(e)
            {
                e = e->parent;
                depth++;
            }
            return depth;
        }

        [[nodiscard]] constexpr std::string_view GetIdentifier() const noexcept  { return {identifier, GetIdentifierSize()}; }
        [[nodiscard]] constexpr String GetFullIdentifier() const noexcept
        {
            String result;
            bool bHasSegment = false;
            for(const Entry* current = this; current; current = current->parent)
            {
                if(!current->parent && current->GetIdentifierSize() == 0)
                    break;

                const uint8_t identifierSize = current->GetIdentifierSize();
                if(identifierSize != 0)
                {
                    char* destination = result.AppendUninitialized(identifierSize + (bHasSegment? 1U : 0U));
                    if(bHasSegment)
                        *destination++ = '.';
                    for(uint8_t i = 0; i < identifierSize; i++)
                        destination[i] = current->identifier[identifierSize - i - 1];
                }
                else if(current->parent && current->parent->type == Type::Array)
                {
                    const uint32_t index = current->parent->FindChildIndex(*current);
                    assert(index != detail::UINT32_MAX_VALUE && "Index must be valid!");
                    uint32_t remaining = index;
                    uint8_t digitCount = 1;
                    while(remaining >= 10)
                    {
                        remaining /= 10;
                        digitCount++;
                    }

                    char* destination = result.AppendUninitialized(digitCount + (bHasSegment? 1U : 0U));
                    if(bHasSegment)
                        *destination++ = '.';
                    remaining = index;
                    for(uint8_t i = 0; i < digitCount; i++)
                    {
                        destination[i] = static_cast<char>('0' + remaining % 10);
                        remaining /= 10;
                    }
                }
                else
                {
                    assert(false && "Only array elements may have an empty identifier");
                    result.clear();
                    return result;
                }
                bHasSegment = true;
            }

            const size_t resultSize = result.size();
            for(size_t i = 0; i < resultSize / 2; i++)
                std::swap(result[i], result[resultSize - i - 1]);
            return result;
        }

        [[nodiscard]] constexpr const String& GetComment() const noexcept;
    #if !FDF_NO_COMMENTS
        [[nodiscard]] constexpr String& GetComment() noexcept;
    #endif

    private:
        constexpr void SetIdentifier_INTERNAL(std::string_view newIdentifier) noexcept;
        constexpr void AllocateStringArray(uint32_t count) noexcept;

    public:
        [[nodiscard]] constexpr bool SetIdentifier(std::string_view newIdentifier) noexcept;
        constexpr void ReleaseData() noexcept;
        constexpr void ReleaseComment() noexcept;
        constexpr void ReleaseEverything() noexcept;

    public:
        [[nodiscard]] constexpr uint32_t FindChildIndex(const Entry& e) const noexcept;
        [[nodiscard]] constexpr uint32_t FindChildIndex(std::string_view _identifier) const noexcept;
        [[nodiscard]] constexpr Entry* Emplace(std::string_view _identifier) noexcept;
        [[nodiscard]] constexpr Entry* AddChild(UniqueEntryPtr& e) noexcept;
        [[nodiscard]] constexpr Entry* AddChild(UniqueEntryPtr& e, DuplicateKeyPolicy policy) noexcept;
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
    public:
        template<detail::IsValidIDType T, detail::IsValidIDType... Args>
        [[nodiscard]] constexpr       Entry* GetChild(T&& param, Args&&... args) noexcept;
        template<detail::IsValidIDType T, detail::IsValidIDType... Args>
        [[nodiscard]] constexpr const Entry* GetChild(T&& param, Args&&... args) const noexcept;

        [[nodiscard]] constexpr       Entry* GetDirectChild(std::string_view _identifier) noexcept;
        [[nodiscard]] constexpr const Entry* GetDirectChild(std::string_view _identifier) const noexcept;
        [[nodiscard]] constexpr       Entry* GetDirectChild(uint32_t index) noexcept;
        [[nodiscard]] constexpr const Entry* GetDirectChild(uint32_t index) const noexcept;

        [[nodiscard]] constexpr std::span<Entry*>            GetChildren() noexcept;
        // prevent mutation of child slots through a const tree
        [[nodiscard]] constexpr std::span<const Entry* const> GetChildren() const noexcept;
        [[nodiscard]] constexpr std::vector<Entry*>       GetChildrenRecursive() noexcept;
        [[nodiscard]] constexpr std::vector<const Entry*> GetChildrenRecursive() const noexcept;
        [[nodiscard]] constexpr size_t                    GetChildCountRecursive() const noexcept;

    private:
        [[nodiscard]] constexpr std::span<Entry*>         GetChildren_INTERNAL() noexcept;
        [[nodiscard]] constexpr std::span<const Entry*>   GetChildren_INTERNAL() const noexcept;
        template<typename Self>
        [[nodiscard]] static constexpr auto GetChildrenRecursiveImpl(Self& self) noexcept;
        template<std::underlying_type_t<ForEachFlags::Flag> FLAGS, typename Self, typename Callable>
        static constexpr void ForEachImpl(Self& self, Callable& callback) noexcept(std::is_nothrow_invocable_v<Callable, Self&>);


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

        constexpr void SetType(Type _type) noexcept;
        constexpr void Resize(uint32_t _size) noexcept;

        constexpr void SetValue(NullType) noexcept;
        constexpr void SetValue(NilType) noexcept;
        constexpr void SetValue(ArrayType) noexcept;
        constexpr void SetValue(MapType) noexcept;
        constexpr void SetValue(bool value) noexcept;
        constexpr void SetValue(std::signed_integral   auto value) noexcept;
        constexpr void SetValue(std::unsigned_integral auto value) noexcept;
        constexpr void SetValue(std::floating_point    auto value) noexcept;
        constexpr void SetValue(std::string_view value) noexcept;
        constexpr void SetValue(std::span<const std::string_view> value) noexcept;
        constexpr void SetValue(char value) noexcept;
        constexpr void SetValue(const char* value) noexcept;
        constexpr void SetValue(const Timestamp& value) noexcept;
        constexpr void SetValue(const Duration& value) noexcept;
        constexpr void SetValue(const Version& value) noexcept;
        constexpr void SetValue(const Hex& value) noexcept;
        constexpr void SetValue(Hex&& value) noexcept;
        constexpr void SetValue(String&& value) noexcept;
        constexpr void SetValue(auto* value) = delete; // no pointer types (except char*)

        constexpr void SetValue(std::span<bool> value) noexcept;
        template<std::signed_integral T>
        constexpr void SetValue(std::span<T> value) noexcept;
        template<std::unsigned_integral T>
        constexpr void SetValue(std::span<T> value) noexcept;
        constexpr void SetValue(std::span<const Version> value) noexcept;
        constexpr void SetValue(std::span<const Timestamp> value) noexcept;
        constexpr void SetValue(std::span<const Duration> value) noexcept;
        constexpr void SetValue(std::span<const Hex> value) noexcept;
        template<std::floating_point T>
        constexpr void SetValue(std::span<T> value) noexcept;

        template<Style STYLE = {}>
        [[nodiscard]] constexpr std::string_view DataToView(String& temp) const noexcept;

    public:
        // inline sinks stay last at call sites
        [[nodiscard]] bool ParseCombineFile(const std::filesystem::path& filepath, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseNewIfExistingIsEmpty, DuplicateKeyPolicy policy = DuplicateKeyPolicy::Merge) noexcept;
        template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
        [[nodiscard]] bool ParseCombineFile(const std::filesystem::path& filepath, SINK&& sink) noexcept;
        template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
        [[nodiscard]] bool ParseCombineFile(const std::filesystem::path& filepath, CommentCombineStrategy fileCommentCombineStrategy, DuplicateKeyPolicy policy, SINK&& sink) noexcept;

        [[nodiscard]] constexpr bool ParseCombineBuffer(std::string_view content, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseNewIfExistingIsEmpty, DuplicateKeyPolicy policy = DuplicateKeyPolicy::Merge) noexcept;
        template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
        [[nodiscard]] constexpr bool ParseCombineBuffer(std::string_view content, SINK&& sink) noexcept;
        template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
        [[nodiscard]] constexpr bool ParseCombineBuffer(std::string_view content, CommentCombineStrategy fileCommentCombineStrategy, DuplicateKeyPolicy policy, SINK&& sink) noexcept;
        [[nodiscard]] constexpr bool Combine(UniqueEntryPtr& other, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseNewIfExistingIsEmpty, DuplicateKeyPolicy policy = DuplicateKeyPolicy::Merge) noexcept;
    };

    static_assert(sizeof(String) == 8, "String is a single block pointer, size/capacity live in the block header");
#if !FDF_NO_COMMENTS
    static_assert(sizeof(Entry) == 64, "Entry must stay one cache line");
#elif FDF_EXTENDED_NO_COMMENT_IDENTIFIERS
    static_assert(sizeof(Entry) == 64, "Entry must stay one cache line (extended identifiers fill the comment slot)");
#else
    static_assert(sizeof(Entry) == 56, "Entry layout drifted");
#endif





    template<typename SINK = detail::NoDiagnostics> requires std::is_invocable_v<SINK&, const Diagnostic&>
    [[nodiscard]] UniqueEntryPtr ParseFile(const std::filesystem::path& filepath, SINK&& sink = {}) noexcept;

    template<Style STYLE = {}>
    [[nodiscard]] bool WriteFile(const Entry& e, const std::filesystem::path& filepath, bool bCreateIfNotExists = true) noexcept;

    [[nodiscard]] constexpr UniqueEntryPtr NewEntry() noexcept;
}




namespace fdf::detail
{
    inline constexpr std::string_view UNEXPECTED_TEXT = "<UNEXPECTED-ERROR>";
    inline constexpr std::string_view ARRAY_TEXT   = "<ARRAY>";
    inline constexpr std::string_view MAP_TEXT     = "<MAP>";

    #if FDF_NO_COMMENTS
        inline constexpr size_t DATA_OVERHEAD_SIZE = sizeof(size_t);
    #else
        inline constexpr size_t DATA_OVERHEAD_SIZE = sizeof(size_t) + sizeof(void*);
    #endif

    inline constexpr size_t INITIAL_PARENT_DATA_SIZE = DATA_OVERHEAD_SIZE + (4 * sizeof(void*));


    inline constexpr auto KEYWORDS = std::to_array<std::string_view>
    ({
        "null", "nil",
        "true", "false"
    });

    FDF_EXPORT_INTERNAL enum class TokenType : uint8_t
    {
        NonExisting,  // Requested token doesn't exist/cannot be accessed, not necessarily an error
        Invalid,      // There is an error in the file content
        NewLine,
        EndOfFile,
        Comment,

        Equal,
        Comma,
        Pipe,  // pack separator

        CurlyBraceOpen,
        CurlyBraceClose,
        SquareBraceOpen,
        SquareBraceClose,

        StringLiteral,  // Quoted "Foo" or 'Foo'
        Atom,           // unquoted key or value; the parser validates and classifies it
    };

    FDF_EXPORT_INTERNAL struct Token
    {
        constexpr Token() noexcept = default;
        constexpr Token(TokenType type_, uint32_t startPosition_ = 0, uint32_t count_ = 0) noexcept
            : type(type_), count(count_), startPosition(startPosition_)  { }

        TokenType type = TokenType::NonExisting;
        uint8_t  extra8  = 0; // Token specific data
        uint32_t column = 0;
        uint32_t count = 0;
        uint32_t startPosition = 0;
        uint32_t line = 0;
    };

    FDF_EXPORT_INTERNAL struct Tokenizer
    {
        explicit constexpr Tokenizer(std::string_view content_) noexcept
            : content(content_), index(0), line(1), lastNewLineIndex(detail::UINT32_MAX_VALUE), currentToken(GetNextToken())  { }

        [[nodiscard]] constexpr Token Current() const noexcept  { return currentToken; }
        [[nodiscard]] constexpr Token Advance()       noexcept  { currentToken = GetNextToken(); return currentToken; }

        // EOF line comments use UINT32_MAX, outside substr's range
        [[nodiscard]] constexpr std::string_view ToView(Token token) const noexcept
        {
            if(token.startPosition >= content.size())
                return {};
            return content.substr(token.startPosition, token.count);
        }

        // Detached entries cannot report open nesting through their parent chain
        uint32_t depth = 0;

    private:
        [[nodiscard]] constexpr Token GetNextToken() noexcept;

        // Build a token at the current position, set its line/column and advance past it
        [[nodiscard]] constexpr Token MakeToken(TokenType type, uint32_t count) noexcept
        {
            Token token = Token(type, index, count);
            token.line = line;
            token.column = index - lastNewLineIndex;
            index += count;
            return token;
        }

        // Build an Invalid token tagged with the reason and the current position so the parser
        // can forward a precise diagnostic. The reason is stashed in extra8
        [[nodiscard]] constexpr Token MakeInvalid(DiagnosticType reason) const noexcept
        {
            Token token = Token(TokenType::Invalid, index);
            token.line = line;
            token.column = index - lastNewLineIndex;
            token.extra8 = static_cast<uint8_t>(reason);
            return token;
        }

    public:
        std::string_view content;
        uint32_t index;
        uint32_t line;
        uint32_t lastNewLineIndex;
        Token currentToken;
    };
}




namespace fdf::detail
{
    FDF_EXPORT_INTERNAL constexpr void constexpr_memcpy(char* dest, const char* src, size_t size) noexcept
    {
        for(size_t i = 0; i < size; i++)
            dest[i] = src[i];
    }

    // Constexpr number -> string appenders, so the writer can run at compile time (std::format isn't
    // constexpr). std::to_chars is constexpr for integers
    constexpr void AppendInt(String& s, int64_t v) noexcept
    {
        char b[24];
        const auto r = std::to_chars(b, b + sizeof(b), v);
        s.append(b, r.ptr);
    }
    constexpr void AppendUInt(String& s, uint64_t v) noexcept
    {
        char b[24];
        const auto r = std::to_chars(b, b + sizeof(b), v);
        s.append(b, r.ptr);
    }
    // Shortest-decimal double <-> string, hand-rolled to run the same at compile time and runtime
    // (to/from_chars aren't constexpr for floats). Print: Dragon4 / Burger-Dybvig. Parse: Clinger
    // fast path + AlgorithmM slow path. Both reduce to add/sub/shift/compare on one BigUint
    struct BigUint
    {
        static constexpr int CAP = 96;  // 3072 bits, ample for the full f64 range
        uint32_t limb[CAP] = {};
        int len = 0;

        constexpr void Normalize() noexcept
        {
            while(len > 0 && limb[len - 1] == 0)
                len--;
        }
        [[nodiscard]] constexpr bool IsZero() const noexcept  { return len == 0; }

        constexpr void SetU64(uint64_t v) noexcept
        {
            for(int i = 0; i < CAP; i++)
                limb[i] = 0;
            limb[0] = static_cast<uint32_t>(v);
            limb[1] = static_cast<uint32_t>(v >> 32);
            len = 2;
            Normalize();
        }

        [[nodiscard]] constexpr int BitLength() const noexcept
        {
            if(len == 0)
                return 0;
            return len * 32 - std::countl_zero(limb[len - 1]);
        }

        [[nodiscard]] static constexpr int Compare(const BigUint& a, const BigUint& b) noexcept
        {
            if(a.len != b.len)
                return a.len < b.len? -1 : 1;
            for(int i = a.len - 1; i >= 0; i--)
            {
                if(a.limb[i] != b.limb[i])
                    return a.limb[i] < b.limb[i]? -1 : 1;
            }
            return 0;
        }

        constexpr void Add(const BigUint& o) noexcept
        {
            const int n = o.len > len? o.len : len;
            uint64_t carry = 0;
            for(int i = 0; i < n; i++)
            {
                const uint64_t sum = carry + (i < len? limb[i] : 0) + (i < o.len? o.limb[i] : 0);
                limb[i] = static_cast<uint32_t>(sum);
                carry = sum >> 32;
            }
            len = n;
            if(carry)
                limb[len++] = static_cast<uint32_t>(carry);
        }

        constexpr void Sub(const BigUint& o) noexcept  // requires *this >= o
        {
            int64_t borrow = 0;
            for(int i = 0; i < len; i++)
            {
                int64_t d = static_cast<int64_t>(limb[i]) - borrow - (i < o.len? o.limb[i] : 0);
                if(d < 0)
                {
                    d += (int64_t(1) << 32);
                    borrow = 1;
                }
                else
                    borrow = 0;
                limb[i] = static_cast<uint32_t>(d);
            }
            Normalize();
        }

        constexpr void MulSmall(uint32_t m) noexcept
        {
            if(m == 0 || len == 0)
            {
                len = 0;
                return;
            }
            uint64_t carry = 0;
            for(int i = 0; i < len; i++)
            {
                const uint64_t p = static_cast<uint64_t>(limb[i]) * m + carry;
                limb[i] = static_cast<uint32_t>(p);
                carry = p >> 32;
            }
            if(carry)
                limb[len++] = static_cast<uint32_t>(carry);
        }
        constexpr void Times10() noexcept  { MulSmall(10); }

        constexpr void ShiftLeft(int bits) noexcept
        {
            if(bits <= 0 || len == 0)
                return;
            const int words = bits / 32;
            const int sh = bits % 32;
            if(sh == 0)
            {
                for(int i = len - 1; i >= 0; i--)
                    limb[i + words] = limb[i];
            }
            else
            {
                limb[len + words] = 0;
                for(int i = len - 1; i >= 0; i--)
                {
                    const uint32_t hi = limb[i] >> (32 - sh);
                    const uint32_t lo = limb[i] << sh;
                    limb[i + words + 1] |= hi;
                    limb[i + words] = lo;
                }
            }
            for(int i = 0; i < words; i++)
                limb[i] = 0;
            len += words + (sh? 1 : 0);
            Normalize();
        }

        constexpr void ShiftRight1() noexcept
        {
            uint32_t carry = 0;
            for(int i = len - 1; i >= 0; i--)
            {
                const uint32_t nc = limb[i] & 1u;
                limb[i] = (limb[i] >> 1) | (carry << 31);
                carry = nc;
            }
            Normalize();
        }
    };

    // (a + b) <=> c
    [[nodiscard]] constexpr int PlusCompare(const BigUint& a, const BigUint& b, const BigUint& c) noexcept
    {
        BigUint t = a;
        t.Add(b);
        return BigUint::Compare(t, c);
    }

    [[nodiscard]] constexpr BigUint Pow10Big(int n) noexcept
    {
        BigUint r;
        r.SetU64(1);
        while(n >= 9)
        {
            r.MulSmall(1000000000u);
            n -= 9;
        }
        constexpr uint32_t POW10_SMALL[9] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000 };
        if(n > 0)
            r.MulSmall(POW10_SMALL[n]);
        return r;
    }

    // Dragon4: shortest decimal digits of value = f * 2^e (f != 0). Writes `numDigits` digit chars
    // into `out`, sets `point` so the value is 0.<digits> * 10^point. The last digit may carry to
    // '0'+10, which the caller resolves
    constexpr void DragonDigits(uint64_t f, int e, char* out, int& numDigits, int& point) noexcept
    {
        const bool bEven = (f & 1u) == 0;
        const bool bLowerCloser = (f == (1ull << 52)) && (e != -1074);

        BigUint num, den, dPlus, dMinus;
        if(e >= 0)
        {
            num.SetU64(f);
            num.ShiftLeft(e + 1);                   // f * 2^e * 2
            den.SetU64(2);
            dPlus.SetU64(1);  dPlus.ShiftLeft(e);
            dMinus.SetU64(1); dMinus.ShiftLeft(e);
        }
        else
        {
            num.SetU64(f);
            num.ShiftLeft(1);                       // f * 2
            den.SetU64(1);  den.ShiftLeft(1 - e);   // 2 * 2^(-e)
            dPlus.SetU64(1);
            dMinus.SetU64(1);
        }
        if(bLowerCloser)
        {
            num.ShiftLeft(1);
            den.ShiftLeft(1);
            dPlus.ShiftLeft(1);
        }

        // Normalize ratio num/den into [0.1, 1), tracking the decimal exponent
        point = 0;
        while(true)
        {
            if(BigUint::Compare(num, den) >= 0)
            {
                den.Times10();
                point++;
            }
            else
            {
                BigUint t = num;
                t.Times10();
                if(BigUint::Compare(t, den) < 0)
                {
                    num.Times10();
                    dPlus.Times10();
                    dMinus.Times10();
                    point--;
                }
                else
                    break;
            }
        }

        numDigits = 0;
        while(true)
        {
            num.Times10();
            dPlus.Times10();
            dMinus.Times10();

            int digit = 0;
            while(BigUint::Compare(num, den) >= 0)
            {
                num.Sub(den);
                digit++;
            }

            const bool bLow  = bEven? BigUint::Compare(num, dMinus) <= 0
                                    : BigUint::Compare(num, dMinus) <  0;
            const bool bHigh = bEven? PlusCompare(num, dPlus, den) >= 0
                                    : PlusCompare(num, dPlus, den) >  0;

            if(!bLow && !bHigh)
            {
                out[numDigits++] = static_cast<char>('0' + digit);
                continue;
            }

            bool bRoundUp;
            if(bLow && !bHigh)
                bRoundUp = false;
            else if(bHigh && !bLow)
                bRoundUp = true;
            else
                bRoundUp = PlusCompare(num, num, den) > 0;  // 2*num > den

            out[numDigits++] = static_cast<char>('0' + digit + (bRoundUp? 1 : 0));
            break;
        }
    }

    // Shortest round-trip text for any finite double (and inf/nan sentinels). Always emits a '.' or
    // 'e' so the value re-parses as a Float, never an Int
    FDF_EXPORT_INTERNAL constexpr void AppendDouble(String& s, double v) noexcept
    {
        const uint64_t bits = std::bit_cast<uint64_t>(v);
        const bool bNeg = (bits >> 63) != 0;
        const int rawExp = static_cast<int>((bits >> 52) & 0x7FF);
        const uint64_t mantissa = bits & ((1ull << 52) - 1);

        if(rawExp == 0x7FF)
        {
            s += mantissa? "nan" : (bNeg? "-inf" : "inf");
            return;
        }
        if(bNeg)
            s.push_back('-');
        if(rawExp == 0 && mantissa == 0)
        {
            s += "0.0";
            return;
        }

        uint64_t f;
        int e;
        if(rawExp == 0)
        {
            f = mantissa;
            e = -1074;
        }
        else
        {
            f = mantissa | (1ull << 52);
            e = rawExp - 1075;
        }

        char digits[40];
        int n = 0;
        int point = 0;
        DragonDigits(f, e, digits, n, point);

        if(n > 0 && digits[n - 1] == '0' + 10)  // last digit carried 9 -> 10
        {
            int i = n - 1;
            digits[i] = '0';
            while(i > 0 && digits[i - 1] == '9')
            {
                digits[i - 1] = '0';
                i--;
            }
            if(i == 0)
            {
                for(int k = n; k > 0; k--)
                    digits[k] = digits[k - 1];
                digits[0] = '1';
                n++;
                point++;
            }
            else
                digits[i - 1]++;
        }
        while(n > 1 && digits[n - 1] == '0')
            n--;

        const bool bFixed = (point > -4 && point <= 17);
        if(bFixed)
        {
            if(point <= 0)
            {
                s += "0.";
                for(int i = 0; i < -point; i++)
                    s.push_back('0');
                s.append(digits, digits + n);
            }
            else if(point >= n)
            {
                s.append(digits, digits + n);
                for(int i = 0; i < point - n; i++)
                    s.push_back('0');
                s += ".0";
            }
            else
            {
                s.append(digits, digits + point);
                s.push_back('.');
                s.append(digits + point, digits + n);
            }
        }
        else
        {
            s.push_back(digits[0]);
            s.push_back('.');
            if(n > 1)
                s.append(digits + 1, digits + n);
            else
                s.push_back('0');
            s.push_back('e');
            int exp = point - 1;
            if(exp < 0)
            {
                s.push_back('-');
                exp = -exp;
            }
            char eb[8];
            int en = 0;
            do { eb[en++] = static_cast<char>('0' + exp % 10); exp /= 10; } while(exp);
            while(en > 0)
                s.push_back(eb[--en]);
        }
    }

    inline constexpr double POW10_D[23] =
    {
        1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
        1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
    };

    // Build a double from value = mant * 2^lsbExp. Handles normal, subnormal, carry-on-round and
    // overflow. When lsbExp == -1074 the IEEE encoding is linear in mant, so the subnormal /
    // smallest-normal boundary needs no special case
    [[nodiscard]] constexpr double Pack(uint64_t mant, int lsbExp, bool bNeg) noexcept
    {
        if(mant == 0)
            return bNeg? -0.0 : 0.0;
        uint64_t bits;
        if(lsbExp == -1074)
        {
            bits = mant;
        }
        else
        {
            int top = 63 - std::countl_zero(mant);
            const int E = top + lsbExp;  // invariant under the renormalize below
            if(top == 53)                // rounded up to 2^53
            {
                mant >>= 1;
                lsbExp++;
            }
            if(E > 1023)
                return bNeg? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
            bits = (static_cast<uint64_t>(E + 1023) << 52) | (mant & ((1ull << 52) - 1));
        }
        if(bNeg)
            bits |= (1ull << 63);
        return std::bit_cast<double>(bits);
    }

    // value = digits * 10^exp10, correctly rounded (AlgorithmM). `bInputSticky` flags input digits
    // dropped below the BigUint precision cap
    [[nodiscard]] constexpr double SlowParse(const BigUint& digits, int exp10, bool bNeg, bool bInputSticky) noexcept
    {
        if(digits.IsZero())
            return bNeg? -0.0 : 0.0;

        BigUint N = digits;
        BigUint D;
        D.SetU64(1);
        if(exp10 > 0)
        {
            const BigUint p = Pow10Big(exp10);
            BigUint r;
            r.len = 0;
            for(int i = 0; i < p.len; i++)
            {
                BigUint partial = digits;
                partial.MulSmall(p.limb[i]);
                partial.ShiftLeft(32 * i);
                r.Add(partial);
            }
            N = r;
        }
        else if(exp10 < 0)
            D = Pow10Big(-exp10);

        const int qbits = N.BitLength() - D.BitLength();
        const int f = 54 - qbits;
        BigUint Nn = N, Dn = D;
        if(f >= 0)
            Nn.ShiftLeft(f);
        else
            Dn.ShiftLeft(-f);

        int kmax = Nn.BitLength() - Dn.BitLength();
        if(kmax < 0)
            kmax = 0;
        BigUint shifted = Dn;
        shifted.ShiftLeft(kmax);

        uint64_t Q = 0;
        BigUint rem = Nn;
        for(int k = kmax; k >= 0; k--)
        {
            if(BigUint::Compare(shifted, rem) <= 0)
            {
                rem.Sub(shifted);
                Q |= (1ull << k);
            }
            shifted.ShiftRight1();
        }
        const bool bSticky = bInputSticky || !rem.IsZero();

        if(Q == 0)
            return bNeg? -0.0 : 0.0;

        // value = Q * 2^(-f). Round to result LSB exponent = max(E-52, -1074)
        const int qBits = 64 - std::countl_zero(Q);
        const int E = qBits - 1 - f;
        int lsbExp = E - 52;
        if(lsbExp < -1074)
            lsbExp = -1074;
        const int dropBits = lsbExp + f;

        uint64_t mant;
        if(dropBits <= 0)
        {
            mant = (-dropBits >= 64)? 0 : (Q << (-dropBits));
        }
        else
        {
            const uint64_t droppedMask = (dropBits >= 64)? ~0ull : ((1ull << dropBits) - 1);
            const uint64_t dropped = Q & droppedMask;
            mant = (dropBits >= 64)? 0 : (Q >> dropBits);
            const uint64_t half = 1ull << (dropBits - 1);
            bool bRoundUp;
            if(dropped > half)
                bRoundUp = true;
            else if(dropped < half)
                bRoundUp = false;
            else
                bRoundUp = bSticky? true : (mant & 1) != 0;
            if(bRoundUp)
                mant++;
        }
        return Pack(mant, lsbExp, bNeg);
    }

    // Parse one number from [s, end). Accepts: [-] digits [. digits] [(e|E)[+|-]digits]
    // Digits may contain '_' only between two digits
    // Sets *bOk false on malformed input; overflow yields +/-inf with *bOk true
    FDF_EXPORT_INTERNAL [[nodiscard]] constexpr double ParseDouble(const char* s, const char* end, bool* bOk) noexcept
    {
        *bOk = true;
        bool bNeg = false;
        const char* p = s;
        if(p < end && *p == '-')
        {
            bNeg = true;
            p++;
        }

        uint64_t fastMant = 0;
        int sigDigits = 0;
        BigUint big;
        big.SetU64(0);
        int exp10 = 0;
        bool bAnyDigit = false;
        bool bSticky = false;
        bool bFastExact = true;

        auto pushDigit = [&](int d)
        {
            bAnyDigit = true;
            if(d != 0 || sigDigits != 0)
            {
                if(sigDigits < 40)
                {
                    big.MulSmall(10);
                    if(d)
                    {
                        BigUint t;
                        t.SetU64(static_cast<uint64_t>(d));
                        big.Add(t);
                    }
                    sigDigits++;
                }
                else
                {
                    if(d)
                        bSticky = true;
                    exp10++;
                }
            }
            if(bFastExact)
            {
                if(fastMant <= (~0ull - 9) / 10)
                    fastMant = fastMant * 10 + static_cast<uint64_t>(d);
                else
                    bFastExact = false;
            }
        };

        auto consumeDigits = [&](const bool bFraction) -> bool
        {
            bool bPreviousDigit = false;
            while(p < end && ((*p >= '0' && *p <= '9') || *p == '_'))
            {
                if(*p == '_')
                {
                    if(!bPreviousDigit || p + 1 >= end || !(p[1] >= '0' && p[1] <= '9'))
                        return false;
                    bPreviousDigit = false;
                    p++;
                    continue;
                }

                pushDigit(*p - '0');
                if(bFraction && sigDigits <= 40)
                    exp10--;
                bPreviousDigit = true;
                p++;
            }
            return true;
        };

        if(!consumeDigits(false))
        {
            *bOk = false;
            return 0.0;
        }

        if(p < end && *p == '.')
        {
            p++;
            if(!consumeDigits(true))
            {
                *bOk = false;
                return 0.0;
            }
        }

        if(p < end && (*p == 'e' || *p == 'E'))
        {
            p++;
            bool bExpNeg = false;
            if(p < end && (*p == '+' || *p == '-'))
            {
                bExpNeg = (*p == '-');
                p++;
            }
            int ev = 0;
            bool bExpDigit = false;
            bool bPreviousDigit = false;
            while(p < end && ((*p >= '0' && *p <= '9') || *p == '_'))
            {
                if(*p == '_')
                {
                    if(!bPreviousDigit || p + 1 >= end || !(p[1] >= '0' && p[1] <= '9'))
                    {
                        *bOk = false;
                        return 0.0;
                    }
                    bPreviousDigit = false;
                    p++;
                    continue;
                }

                ev = ev < 100000? ev * 10 + (*p - '0') : ev;
                bExpDigit = true;
                bPreviousDigit = true;
                p++;
            }
            if(!bExpDigit)
            {
                *bOk = false;
                return 0.0;
            }
            exp10 += bExpNeg? -ev : ev;
        }

        if(p != end || !bAnyDigit)
        {
            *bOk = false;
            return 0.0;
        }

        if(big.IsZero())
            return bNeg? -0.0 : 0.0;

        // Clinger fast path: exact mantissa times an exact power of ten
        if(bFastExact && !bSticky && fastMant < (1ull << 53))
        {
            if(exp10 == 0)
                return bNeg? -static_cast<double>(fastMant) : static_cast<double>(fastMant);
            if(exp10 > 0 && exp10 <= 22)
            {
                const double r = static_cast<double>(fastMant) * POW10_D[exp10];
                return bNeg? -r : r;
            }
            if(exp10 < 0 && exp10 >= -22)
            {
                const double r = static_cast<double>(fastMant) / POW10_D[-exp10];
                return bNeg? -r : r;
            }
        }

        // Magnitude clamp keeps the bignums bounded
        const int magExp = exp10 + sigDigits;
        if(magExp > 310)
            return bNeg? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        if(magExp < -330)
            return bNeg? -0.0 : 0.0;

        return SlowParse(big, exp10, bNeg, bSticky);
    }

    [[nodiscard]] constexpr bool IsValueLiteral(TokenType type) noexcept
    {
        return type == TokenType::StringLiteral || type == TokenType::Atom;
    }

    [[nodiscard]] constexpr bool IsEscapableChar(char c) noexcept    { return c == '\"' || c == '\'' || c == '\\'; }
    [[nodiscard]] constexpr bool IsMergeEscapeChar(char c) noexcept  { return c == 'n' || c == 'r' || c == 't' || c == 'v' || c == 'b' || c == 'f' || c == 'a'; }

    [[nodiscard]] constexpr char ConvertMergedEscapeChar(char c) noexcept
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
    }

    // view includes the surrounding quotes
    constexpr void DecodeStringLiteral(std::string_view view, auto&& writeChar) noexcept
    {
        const uint32_t end = static_cast<uint32_t>(view.size()) - 1U;
        for(uint32_t i = 1; i < end; i++)
        {
            if(view[i] == '\\' && i + 1 < end && (IsEscapableChar(view[i + 1]) || IsMergeEscapeChar(view[i + 1])))
            {
                i++;
                writeChar(IsEscapableChar(view[i])? view[i] : ConvertMergedEscapeChar(view[i]));
            }
            else
            {
                writeChar(view[i]);
            }
        }
    }

    [[nodiscard]] constexpr bool constexpr_isspace(char c) noexcept
    {
        return (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r');
    }

    // Comments are stored raw; the writer streams them through this: leading whitespace
    // stripped, each newline (+ the following whitespace run) collapsed into one space
    constexpr void NormalizeComment(std::string_view raw, auto&& writeChar) noexcept
    {
        size_t i = 0;
        while(i < raw.size() && constexpr_isspace(raw[i]))
            i++;
        for(; i < raw.size(); i++)
        {
            if(raw[i] != '\n' && raw[i] != '\r')
                writeChar(raw[i]);
            else
            {
                writeChar(' ');
                while(i + 1 < raw.size() && constexpr_isspace(raw[i + 1]))
                    i++;
            }
        }
    }

    [[nodiscard]] constexpr bool constexpr_isalpha(char c) noexcept
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    [[nodiscard]] constexpr bool constexpr_isdigit(char c) noexcept
    {
        return c >= '0' && c <= '9';
    }

    [[nodiscard]] constexpr bool constexpr_ishexdigit(char c) noexcept
    {
        return constexpr_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    constexpr void GroupIntegerPart(String& s, const size_t start, const uint8_t grouping) noexcept
    {
        if(grouping == 0)
            return;

        size_t digitStart = start;
        if(digitStart < s.size() && s[digitStart] == '-')
            digitStart++;

        size_t digitEnd = digitStart;
        while(digitEnd < s.size() && constexpr_isdigit(s[digitEnd]))
            digitEnd++;

        const size_t digitCount = digitEnd - digitStart;
        if(digitCount <= grouping)
            return;

        size_t separator = digitEnd - grouping;
        while(separator > digitStart)
        {
            s.insert(separator, 1, '_');
            if(separator - digitStart <= grouping)
                break;
            separator -= grouping;
        }
    }

    // Resolve a single Atom's text into a concrete value type. Only structurally impossible input
    // returns false, content is validated by the per-type parse
    FDF_EXPORT_INTERNAL [[nodiscard]] constexpr bool ClassifyAtom(std::string_view view, Type& outType) noexcept
    {
        if(view.empty())
            return false;

        // keyword literals, resolved here
        for(size_t i = 0; i < KEYWORDS.size(); i++)
        {
            if(view == KEYWORDS[i])
            {
                outType = i <= 1? Type::Null : Type::Bool;
                return true;
            }
        }

        if(view.size() >= 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X'))
        {
            for(size_t i = 2; i < view.size(); i++)
            {
                const char c = view[i];
                if(constexpr_ishexdigit(c))
                    continue;
                if(c == '_' && i > 2 && i + 1 < view.size()
                    && constexpr_ishexdigit(view[i - 1]) && constexpr_ishexdigit(view[i + 1]))
                    continue;
                return false;
            }
            outType = Type::Hex;
            return true;
        }

        // a date '-' sits between digits (2024-12-24), an exponent '-' follows an 'e'/'E' (1.0e-05)
        bool bDateDash = false;
        for(size_t i = 1; i < view.size(); i++)
        {
            if(view[i] == '-' && constexpr_isdigit(view[i - 1]))
            {
                bDateDash = true;
                break;
            }
        }

        if(view.find(':') != std::string_view::npos || bDateDash)
        {
            outType = Type::Timestamp;  // validated by the parse below
            return true;
        }

        // separated numeric atoms are resolved before version and duration classification
        if(view.find('_') != std::string_view::npos)
        {
            outType = view.find_first_of(".eE") != std::string_view::npos? Type::Float : Type::Int;
            return true;
        }

        const size_t numericStart = view[0] == '-'? 1 : 0;
        if(numericStart < view.size() && constexpr_isdigit(view[numericStart]))
        {
            constexpr std::string_view DURATION_LETTERS = "wdhmsun";
            for(size_t i = numericStart + 1; i < view.size(); i++)
            {
                if(DURATION_LETTERS.find(view[i]) != std::string_view::npos)
                {
                    outType = Type::Duration;
                    return true;
                }
            }
        }

        uint8_t dotCount = 0;
        bool bAnyFloat = false;
        for(char c : view)
        {
            if(c == '.')
            {
                dotCount++;
                bAnyFloat = true;
            }
            else if(c == 'e' || c == 'E')
                bAnyFloat = true;
        }

        if(dotCount == 2 || dotCount == 3)  // 3-4 dotted components is a version
        {
            outType = Type::Version;
            return true;
        }
        if(dotCount > 3)
            return false;

        outType = bAnyFloat? Type::Float : Type::Int;
        return true;
    }

    [[nodiscard]] constexpr bool IsKeyword(std::string_view view) noexcept
    {
        for(const auto keyword : KEYWORDS)
        {
            if(view == keyword)
                return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool IsValidIdentifier(std::string_view identifier) noexcept
    {
        if(identifier.empty() || identifier.size() > MAX_IDENTIFIER_LENGTH || (!constexpr_isalpha(identifier[0]) && identifier[0] != '_'))
            return false;

        size_t firstNonID = 1;
        while(firstNonID < identifier.size() && (constexpr_isalpha(identifier[firstNonID]) || constexpr_isdigit(identifier[firstNonID]) || identifier[firstNonID] == '_'))
            firstNonID++;

        return firstNonID >= identifier.size()? !IsKeyword(identifier) : false;
    }

    [[nodiscard]] constexpr bool IsOpenBrace(const char c)   noexcept  { return c == '{' || c == '['; }
    [[nodiscard]] constexpr bool IsCloseBrace(const char c)  noexcept  { return c == '}' || c == ']'; }
    [[nodiscard]] constexpr bool IsCurlyBrace(const char c)  noexcept  { return c == '{' || c == '}'; }
    [[nodiscard]] constexpr bool IsSquareBrace(const char c) noexcept  { return c == '[' || c == ']'; }
    [[nodiscard]] constexpr bool IsBrace(const char c)       noexcept  { return c == '{' || c == '}' || c == '[' || c == ']'; }

    // validates ISO-8601 structure and ranges through Timestamp::FromText
    FDF_EXPORT_INTERNAL [[nodiscard]] constexpr bool IsValidTimestamp(std::string_view ts) noexcept
    {
        return Timestamp::FromText(ts).IsValid();
    }

    FDF_EXPORT_INTERNAL [[nodiscard]] constexpr bool IsValidUtf8(std::string_view s) noexcept
    {
        return detail::Utf8FirstInvalidByte(s) == s.size();
    }
}










namespace fdf::detail
{
    template<size_t BLOCK_SIZE, size_t BLOCK_ALIGNMENT = BLOCK_SIZE, size_t CHUNK_SIZE = 4096U, size_t LAZILY_DEALLOCATED_CHUNK_COUNT = 1>
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




        // a slab grant's size is the block size, known at compile time
        struct AllocationResult
        {
            constexpr AllocationResult(void* ptr_) noexcept : ptr(ptr_)  { }
            [[nodiscard]] constexpr void* Ptr() const noexcept  { return ptr; }
            [[nodiscard]] static consteval size_t Size() noexcept  { return BLOCK_SIZE; }

        private:
            void* ptr;
        };

        [[nodiscard]] AllocationResult Allocate() noexcept
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




    };










    FDF_EXPORT_INTERNAL class GlobalAllocator
    {
    #if defined(FDF_TESTING)
        inline static size_t allocationsUntilFailure = SIZE_T_MAX_VALUE;
    #endif

        // bucket ladder bounds, MIN_BUCKET leaves room for a free-list pointer
        static constexpr size_t MIN_BUCKET = 8U;
        static constexpr size_t MAX_BUCKET = 256U;

        // constinit skips the magic-static guard on the hot allocation path
        template<size_t BLOCK_SIZE, size_t BLOCK_ALIGNMENT = BLOCK_SIZE>
        inline static constinit SlabAllocator<BLOCK_SIZE, BLOCK_ALIGNMENT> SLAB{};

        inline static constinit SlabAllocator<sizeof(Entry), alignof(Entry)> ENTRY_SLAB{};

        // smallest bucket that fits size
        [[nodiscard]] static constexpr size_t BucketFor(size_t size) noexcept
        {
            return std::bit_ceil(std::max(size, MIN_BUCKET));
        }

        // runs op on the smallest bucket slab that fits size, the only spelling of the ladder
        [[nodiscard]] static decltype(auto) SlabOp(size_t size, auto&& op) noexcept
        {
            static_assert(MIN_BUCKET == 8u && MAX_BUCKET == 256u, "Bucket mismatch");  // Replace with template-for
            switch(BucketFor(size))
            {
                case 8u:            return op(SLAB<8u>);
                case 16u:           return op(SLAB<16u>);
                case 32u:           return op(SLAB<32u>);
                case 64u:           return op(SLAB<64u>);
                case 128u:          return op(SLAB<128u>);
                case 256u: default: return op(SLAB<256u>);
            }
        }

        template<typename T>
        static constexpr void ValidateStorageType() noexcept
        {
            static_assert(std::is_same_v<T, Entry> || std::is_nothrow_default_constructible_v<T>);
            static_assert(std::is_same_v<T, Entry> || std::is_nothrow_destructible_v<T>);
        }

        template<typename T>
        [[nodiscard]] static constexpr T* GetData(Entry& entry) noexcept
        {
                 if constexpr(std::is_same_v<T, Entry*  >) return entry.data.e;
            else if constexpr(std::is_same_v<T, bool    >) return entry.data.b;
            else if constexpr(std::is_same_v<T, int64_t >) return entry.data.i;
            else if constexpr(std::is_same_v<T, Version >) return entry.data.v;
            else if constexpr(std::is_same_v<T, double  >) return entry.data.f;
            else if constexpr(std::is_same_v<T, Timestamp>) return entry.data.t;
            else if constexpr(std::is_same_v<T, Duration>) return entry.data.dur;
            else if constexpr(std::is_same_v<T, String  >) return entry.data.s;
            else if constexpr(std::is_same_v<T, Hex     >) return entry.data.h;
            else static_assert(false);
        }

        template<typename T>
        static constexpr void SetData(Entry& entry, T* value) noexcept
        {
                 if constexpr(std::is_same_v<T, Entry*  >) entry.data.e = value;
            else if constexpr(std::is_same_v<T, bool    >) entry.data.b = value;
            else if constexpr(std::is_same_v<T, int64_t >) entry.data.i = value;
            else if constexpr(std::is_same_v<T, Version >) entry.data.v = value;
            else if constexpr(std::is_same_v<T, double  >) entry.data.f = value;
            else if constexpr(std::is_same_v<T, Timestamp>) entry.data.t = value;
            else if constexpr(std::is_same_v<T, Duration>) entry.data.dur = value;
            else if constexpr(std::is_same_v<T, String  >) entry.data.s = value;
            else if constexpr(std::is_same_v<T, Hex     >) entry.data.h = value;
            else static_assert(false);
        }

    public:
    #if defined(FDF_TESTING)
        static void FailAllocationAfter(const size_t successfulAllocationCount) noexcept
        {
            allocationsUntilFailure = successfulAllocationCount;
        }

        static void ResetAllocationFailure() noexcept
        {
            allocationsUntilFailure = SIZE_T_MAX_VALUE;
        }
    #endif

        struct AllocationResult
        {
            constexpr AllocationResult(void* ptr_, size_t size_) noexcept : ptr(ptr_), size(size_)  { }

            template<typename GRANT> requires requires(const GRANT& grant) { { grant.Ptr() } -> std::same_as<void*>; { GRANT::Size() } -> std::same_as<size_t>; }
            constexpr AllocationResult(const GRANT& grant) noexcept : ptr(grant.Ptr()), size(GRANT::Size())  { }

            [[nodiscard]] constexpr void*  Ptr()  const noexcept  { return ptr; }
            [[nodiscard]] constexpr size_t Size() const noexcept  { return size; }

        private:
            void* ptr;
            size_t size;
        };

        template<typename T>
        struct TypedAllocationResult
        {
            T* ptr;
            uint32_t capacity;
        };



        // size = the bucket actually granted (>= request), or the exact request for the heap fallback
        [[nodiscard]] static AllocationResult Allocate(size_t size) noexcept
        {
        #if defined(FDF_TESTING)
            if(allocationsUntilFailure != SIZE_T_MAX_VALUE)
            {
                if(allocationsUntilFailure == 0)
                {
                    allocationsUntilFailure = SIZE_T_MAX_VALUE;
                    return { nullptr, size };
                }
                allocationsUntilFailure--;
            }
        #endif

            if constexpr(FDF_DISABLE_SLAB_ALLOCATOR)
            {
                void* p = ::operator new(size, std::nothrow);
                assert(p && "Allocation shouldn't fail");
                return { p, size };
            }
            else
            {
                if(size > MAX_BUCKET)
                {
                    void* p = ::operator new(size, std::nothrow);
                    assert(p && "Allocation shouldn't fail");
                    return { p, size };
                }

                // explicit return type, each bucket grant is a distinct type and the arms must agree
                return SlabOp(size, [](auto& slab) -> AllocationResult { return slab.Allocate(); });
            }
        }

        static bool Deallocate(void* p, size_t size) noexcept
        {
            if constexpr(FDF_DISABLE_SLAB_ALLOCATOR)
            {
                ::operator delete(p);
                return true;
            }
            else
            {
                if(size > MAX_BUCKET)
                {
                    ::operator delete(p);
                    return true;
                }

                return SlabOp(size, [p](auto& slab)  { return slab.Deallocate(p); });
            }
        }




        template<size_t size>
        [[nodiscard]] static void* Allocate() noexcept
        {
            if constexpr(FDF_DISABLE_SLAB_ALLOCATOR)
            {
                void* p = ::operator new(size, std::nothrow);
                assert(p && "Allocation shouldn't fail");
                return p;
            }
            else if constexpr(BucketFor(size) <= MAX_BUCKET)
                return SLAB<BucketFor(size)>.Allocate().Ptr();
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
            if constexpr(FDF_DISABLE_SLAB_ALLOCATOR)
            {
                ::operator delete(p);
                return true;
            }
            else if constexpr(BucketFor(size) <= MAX_BUCKET)
                return SLAB<BucketFor(size)>.Deallocate(p);
            else
            {
                ::operator delete(p);
                return true;
            }
        }

        template<typename T, typename... Args>
        [[nodiscard]] static constexpr T* Create(Args&&... args) noexcept
        {
            ValidateStorageType<T>();
            T* ptr;
            if consteval
            {
                ptr = std::allocator<T>{}.allocate(1);
            }
            else
            {
                if constexpr(std::is_same_v<T, Entry> && !FDF_DISABLE_SLAB_ALLOCATOR)
                    ptr = static_cast<T*>(ENTRY_SLAB.Allocate().Ptr());
                else
                    ptr = static_cast<T*>(Allocate<sizeof(T)>());
            }
            return std::construct_at(ptr, std::forward<Args>(args)...);
        }

        template<typename T>
        static constexpr void Destroy(T* ptr) noexcept
        {
            ValidateStorageType<T>();
            if(!ptr)
                return;

            ptr->~T();
            if consteval
            {
                std::allocator<T>{}.deallocate(ptr, 1);
            }
            else
            {
                if constexpr(std::is_same_v<T, Entry> && !FDF_DISABLE_SLAB_ALLOCATOR)
                    (void)ENTRY_SLAB.Deallocate(ptr);
                else
                    (void)Deallocate<sizeof(T)>(ptr);
            }
        }

        template<typename T>
        [[nodiscard]] static constexpr TypedAllocationResult<T> Allocate(const uint32_t requestedCapacity, const uint32_t liveCount) noexcept
        {
            ValidateStorageType<T>();
            assert(liveCount <= requestedCapacity);
            if(requestedCapacity == 0)
                return { nullptr, 0 };

            if constexpr(std::is_same_v<T, Entry>)
            {
                assert(requestedCapacity == 1 && liveCount == 1);
                return { Create<T>(), 1 };
            }

            T* ptr;
            uint32_t capacity;
            if consteval
            {
                ptr = std::allocator<T>{}.allocate(requestedCapacity);
                capacity = requestedCapacity;
            }
            else
            {
                const AllocationResult allocation = Allocate(static_cast<size_t>(requestedCapacity) * sizeof(T));
                ptr = static_cast<T*>(allocation.Ptr());
                capacity = static_cast<uint32_t>(allocation.Size() / sizeof(T));
            }

            if constexpr(std::is_same_v<T, char> || std::is_same_v<T, std::byte>)
            {
                if consteval
                {
                    // Clang requires explicit char/byte lifetimes before constexpr buffer writes
                    for(uint32_t i = 0; i < capacity; i++)
                        std::construct_at(ptr + i);
                }
            }
            else
            {
                for(uint32_t i = 0; i < liveCount; i++)
                    std::construct_at(ptr + i);
            }
            return { ptr, capacity };
        }

        template<typename T>
        [[nodiscard]] static constexpr TypedAllocationResult<T> Allocate(const uint32_t count) noexcept
        {
            return Allocate<T>(count, count);
        }

        template<typename T>
        static constexpr void Release(T* ptr, const uint32_t liveCount, const uint32_t allocatedCapacity) noexcept
        {
            ValidateStorageType<T>();
            if(!ptr)
                return;

            if constexpr(std::is_same_v<T, Entry>)
            {
                assert(liveCount == 1 && allocatedCapacity == 1);
                Destroy<T>(ptr);
            }
            else
            {
                if constexpr(!std::is_same_v<T, char> && !std::is_same_v<T, std::byte>)
                {
                    for(uint32_t i = 0; i < liveCount; i++)
                        std::destroy_at(ptr + i);
                }
                if consteval
                {
                    std::allocator<T>{}.deallocate(ptr, allocatedCapacity);
                }
                else
                {
                    (void)Deallocate(static_cast<void*>(ptr),
                        static_cast<size_t>(allocatedCapacity) * sizeof(T));
                }
            }
        }

        template<typename T>
        static constexpr void Allocate(Entry& entry, const uint32_t count) noexcept
        {
            assert(entry.size == 0 && entry.capacity == 0 && "Release existing storage before allocating");
            const TypedAllocationResult<T> allocation = Allocate<T>(count);
            SetData<T>(entry, allocation.ptr);
            entry.size = count;
            entry.capacity = allocation.capacity;
        }

        template<typename T>
        static constexpr void Reserve(Entry& entry, const uint32_t minimumCapacity) noexcept
        {
            static_assert(std::is_nothrow_move_assignable_v<T>);
            if(minimumCapacity <= entry.capacity)
                return;

            uint32_t requestedCapacity = minimumCapacity;
            if(entry.capacity == 0)
                requestedCapacity = std::max(4U, minimumCapacity);
            else if(entry.capacity <= UINT32_MAX_VALUE / 2)
                requestedCapacity = std::max(entry.capacity * 2, minimumCapacity);

            T* oldData = GetData<T>(entry);
            const TypedAllocationResult<T> allocation = Allocate<T>(requestedCapacity, entry.size);

            for(uint32_t i = 0; i < entry.size; i++)
                allocation.ptr[i] = std::move(oldData[i]);

            Release<T>(oldData, entry.size, entry.capacity);
            SetData<T>(entry, allocation.ptr);
            entry.capacity = allocation.capacity;
        }

        template<typename T>
        static constexpr void Release(Entry& entry) noexcept
        {
            T* oldData = GetData<T>(entry);
            Release<T>(oldData, entry.size, entry.capacity);
            SetData<T>(entry, nullptr);
            entry.size = 0;
            entry.capacity = 0;
        }

        template<typename T>
        static constexpr void Resize(Entry& entry, const uint32_t newSize) noexcept
        {
            static_assert(std::is_nothrow_move_assignable_v<T>);
            if(entry.size == newSize)
                return;

            T* oldData = GetData<T>(entry);
            if(newSize <= entry.capacity)
            {
                for(uint32_t i = newSize; i < entry.size; i++)
                    std::destroy_at(oldData + i);
                for(uint32_t i = entry.size; i < newSize; i++)
                    std::construct_at(oldData + i);
                entry.size = newSize;
                return;
            }

            const TypedAllocationResult<T> allocation = Allocate<T>(newSize);

            for(uint32_t i = 0; i < entry.size; i++)
                allocation.ptr[i] = std::move(oldData[i]);

            Release<T>(oldData, entry.size, entry.capacity);
            SetData<T>(entry, allocation.ptr);
            entry.size = newSize;
            entry.capacity = allocation.capacity;
        }

        template<typename T>
        [[nodiscard]] static constexpr bool Stores(const Type t) noexcept
        {
                 if constexpr(std::is_same_v<T, bool   >) return t == Type::Bool;
            else if constexpr(std::is_same_v<T, int64_t>) return t == Type::Int;
            else if constexpr(std::is_same_v<T, double >) return t == Type::Float;
            else if constexpr(std::is_same_v<T, Version>) return t == Type::Version;
            else if constexpr(std::is_same_v<T, Hex    >) return t == Type::Hex;
            else if constexpr(std::is_same_v<T, Timestamp>) return t == Type::Timestamp;
            else if constexpr(std::is_same_v<T, Duration>) return t == Type::Duration;
            else if constexpr(std::is_same_v<T, String >) return t == Type::String;
            else if constexpr(std::is_same_v<T, Entry* >) return t == Type::Array || t == Type::Map;
            else return false;
        }

        template<Type TYPE>
        [[nodiscard]] static constexpr auto StorageOf() noexcept
        {
                 if constexpr(TYPE == Type::Bool)    return std::type_identity<bool>{};
            else if constexpr(TYPE == Type::Int)     return std::type_identity<int64_t>{};
            else if constexpr(TYPE == Type::Float)   return std::type_identity<double>{};
            else if constexpr(TYPE == Type::Version) return std::type_identity<Version>{};
            else if constexpr(TYPE == Type::Hex)     return std::type_identity<Hex>{};
            else if constexpr(TYPE == Type::Timestamp) return std::type_identity<Timestamp>{};
            else if constexpr(TYPE == Type::Duration) return std::type_identity<Duration>{};
            else if constexpr(TYPE == Type::String) return std::type_identity<String>{};
            else if constexpr(TYPE == Type::Array || TYPE == Type::Map)
                return std::type_identity<Entry*>{};
            else
                static_assert(false, "Type holds no storage");
        }

        // storage for a value overwrite: reuses the buffer when the entry already holds this
        // storage type with enough capacity, the caller must overwrite every element in [0, count)
        template<Type NEW_TYPE>
        static constexpr void Repurpose(Entry& entry, const uint32_t count) noexcept
        {
            using T = typename decltype(StorageOf<NEW_TYPE>())::type;

            // capacity 0 = nothing to reuse, the common first-set case skips every check
            if(entry.capacity != 0)
            {
                // children never survive an overwrite. Destroying them up front lets the
                // child-pointer block flow through both reuse paths like any other storage,
                // Resize's element destroy only sees the pointers, never the owned children
                if(entry.type == Type::Array || entry.type == Type::Map)
                    (void)entry.ClearChildren();

                if(Stores<T>(entry.type) && GetData<T>(entry) && count <= entry.capacity)
                {
                    Resize<T>(entry, count);
                    entry.type = NEW_TYPE;
                    return;
                }

                // runtime may retype a buffer in place between any storage types: every block is
                // >= 8-aligned (MIN_BUCKET), which covers the largest storage alignment, so only the
                // byte capacity matters. Constant evaluation owns typed arrays and must reallocate
                if !consteval
                {
                    size_t oldElementSize = 0;
                    void* raw = nullptr;
                    switch(entry.type)
                    {
                        case Type::Bool:     raw = entry.data.b; oldElementSize = sizeof(bool);    break;
                        case Type::Int:      raw = entry.data.i; oldElementSize = sizeof(int64_t); break;
                        case Type::Float:    raw = entry.data.f; oldElementSize = sizeof(double);  break;
                        case Type::Version:  raw = entry.data.v; oldElementSize = sizeof(Version); break;
                        case Type::Hex:      raw = entry.data.h; oldElementSize = sizeof(Hex);     break;
                        case Type::Timestamp: raw = entry.data.t; oldElementSize = sizeof(Timestamp); break;
                        case Type::Duration: raw = entry.data.dur; oldElementSize = sizeof(Duration); break;
                        case Type::String:    raw = entry.data.s; oldElementSize = sizeof(String);  break;
                        case Type::Array: case Type::Map:
                                             raw = entry.data.e; oldElementSize = sizeof(Entry*);  break;
                        default: break;
                    }

                    // the divisibility and range checks keep capacity * sizeof(T) == oldBytes, the
                    // eventual release must reproduce the exact byte count to hit the right bucket.
                    // oldBytes >= sizeof(T) keeps a retained block's capacity at least 1 even for
                    // count 0 (an emptied container target)
                    const size_t oldBytes = static_cast<size_t>(entry.capacity) * oldElementSize;
                    if(raw && oldBytes >= sizeof(T) && static_cast<size_t>(count) * sizeof(T) <= oldBytes
                        && oldBytes % sizeof(T) == 0 && oldBytes / sizeof(T) <= UINT32_MAX_VALUE)
                    {
                        if(entry.type == Type::String)
                        {
                            for(uint32_t i = 0; i < entry.size; i++)
                                std::destroy_at(entry.data.s + i);
                        }
                        else if(entry.type == Type::Hex)
                        {
                            for(uint32_t i = 0; i < entry.size; i++)
                                std::destroy_at(entry.data.h + i);
                        }

                        T* ptr = static_cast<T*>(raw);
                        for(uint32_t i = 0; i < count; i++)
                            std::construct_at(ptr + i);
                        SetData<T>(entry, ptr);
                        entry.size = count;
                        entry.capacity = static_cast<uint32_t>(oldBytes / sizeof(T));
                        entry.type = NEW_TYPE;
                        return;
                    }
                }
            }

            entry.ReleaseData();
            entry.type = NEW_TYPE;
            Allocate<T>(entry, count);
        }

    };


    FDF_EXPORT_INTERNAL
    template<typename T>
    class Vector
    {
        static_assert(std::is_nothrow_move_assignable_v<T>);

    public:
        constexpr Vector() noexcept = default;
        Vector(const Vector&) = delete;
        Vector& operator=(const Vector&) = delete;
        constexpr ~Vector() noexcept
        {
            GlobalAllocator::Release<T>(data, size_, capacity_);
        }

        [[nodiscard]] constexpr bool empty() const noexcept  { return size_ == 0; }
        [[nodiscard]] constexpr size_t size() const noexcept  { return size_; }
        [[nodiscard]] constexpr T& back() noexcept  { assert(size_ > 0); return data[size_ - 1]; }
        [[nodiscard]] constexpr const T& back() const noexcept  { assert(size_ > 0); return data[size_ - 1]; }
        [[nodiscard]] constexpr T& operator[](size_t index) noexcept  { assert(index < size_); return data[index]; }
        [[nodiscard]] constexpr const T& operator[](size_t index) const noexcept  { assert(index < size_); return data[index]; }

        template<typename... Args>
        constexpr T& emplace_back(Args&&... args) noexcept
        {
            if(size_ == capacity_)
                Grow();

            std::construct_at(data + size_, std::forward<Args>(args)...);
            return data[size_++];
        }

        constexpr void push_back(const T& value) noexcept  { (void)emplace_back(value); }
        constexpr void push_back(T&& value) noexcept  { (void)emplace_back(std::move(value)); }

        constexpr void pop_back() noexcept
        {
            assert(size_ > 0);
            size_--;
            std::destroy_at(data + size_);
        }

        constexpr void erase(size_t index) noexcept
        {
            assert(index < size_);
            for(size_t i = index; i + 1 < size_; i++)
                data[i] = std::move(data[i + 1]);
            pop_back();
        }

    private:
        constexpr void Grow() noexcept
        {
            const uint32_t requestedCapacity = capacity_ == 0? 8
                : capacity_ <= detail::UINT32_MAX_VALUE / 2? capacity_ * 2
                : detail::UINT32_MAX_VALUE;
            const auto allocation = GlobalAllocator::Allocate<T>(requestedCapacity, size_);
            for(uint32_t i = 0; i < size_; i++)
                allocation.ptr[i] = std::move(data[i]);
            GlobalAllocator::Release<T>(data, size_, capacity_);
            data = allocation.ptr;
            capacity_ = allocation.capacity;
        }

        T* data = nullptr;
        uint32_t size_ = 0;
        uint32_t capacity_ = 0;
    };
}












namespace fdf::detail
{
    constexpr Token Tokenizer::GetNextToken() noexcept
    {
        if(index >= content.size())
            return MakeToken(TokenType::EndOfFile, 0);

        while(constexpr_isspace(content[index]))
        {
            if(content[index] == '\n')
            {
                Token token = Token(TokenType::NewLine, index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
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

            index++;
            if(index >= content.size())
                return MakeToken(TokenType::EndOfFile, 0);
        }



        if(content[index] == '\"' || content[index] == '\'')
        {
            const char quote = content[index];
            size_t nextQuote = index + 1;
            bool bEscaped = false;
            for(; nextQuote < content.size(); nextQuote++)
            {
                const char c = content[nextQuote];
                if(c == quote && !bEscaped)
                    break;
                bEscaped = c == '\\'? !bEscaped : false;
            }
            if(nextQuote == content.size())
                return MakeInvalid(DiagnosticType::UnterminatedString);

            Token token = Token(TokenType::StringLiteral, index, static_cast<uint32_t>(nextQuote) + 1 - index);
            token.line = line;
            token.column = token.startPosition - lastNewLineIndex;
            for(size_t i = index; i < nextQuote; i++)
            {
                if(content[i] == '\n')
                {
                    line++;
                    lastNewLineIndex = static_cast<uint32_t>(i);
                }
            }
            index = static_cast<uint32_t>(nextQuote + 1);
            token.extra8 = static_cast<uint8_t>(token.count - 2);
            return token;
        }



        if(content[index] == '/')
        {
            if(index + 1 >= content.size())
                return MakeInvalid(DiagnosticType::InvalidComment); // not enough space for a comment

            if(content[index + 1] == '/') // single line comment
            {
                size_t newLinePos = content.find_first_of('\n', index + 2);
                const uint32_t bodyStart = index + 2 < content.size() && content[index + 2] == ' '? index + 3 : index + 2;
                Token token = Token(TokenType::Comment, bodyStart);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;

                if(newLinePos != std::string_view::npos)
                {
                    token.count = static_cast<uint32_t>(newLinePos - token.startPosition);
                    index = static_cast<uint32_t>(newLinePos);
                }
                else
                {
                    // Comment runs to the end of the file
                    token.count = static_cast<uint32_t>(content.size() - token.startPosition);
                    index = detail::UINT32_MAX_VALUE;
                }
                if(token.count > 0 && content[token.startPosition + token.count - 1] == '\r')
                    token.count--;   // the '\r' of a CRLF line ending isn't comment text
                return token;
            }

            if(content[index + 1] == '*') // multi line comment
            {
                // from index + 3: "/*/" must not close on its own '/', which would underflow count
                size_t slashPos = content.find_first_of('/', index + 3);
                while(true)
                {
                    if(slashPos == std::string_view::npos)
                        return MakeInvalid(DiagnosticType::UnterminatedComment); // missing "*/"

                    if(content[slashPos - 1] == '*')
                    {
                        Token token = Token(TokenType::Comment, index + 2);
                        token.line = line;
                        token.column = token.startPosition - lastNewLineIndex;
                        token.extra8 = 1;  // Means multi line
                        token.count = static_cast<uint32_t>((slashPos - 1) - token.startPosition);
                        while(token.count > 0 && constexpr_isspace(content[token.startPosition + token.count - 1]))
                            token.count--;

                        for(size_t i = index + 2; i < slashPos - 1; i++)
                        {
                            if(content[i] == '\n')
                            {
                                line++;
                                lastNewLineIndex = static_cast<uint32_t>(i);
                            }
                        }

                        index = static_cast<uint32_t>(slashPos + 1);
                        if(index + 1 < content.size() && content[index] == '\n')
                        {
                            lastNewLineIndex = index;
                            line++;
                            index++;
                        }

                        return token;
                    }

                    slashPos = content.find_first_of('/', slashPos + 2);
                }
            }

            return MakeInvalid(DiagnosticType::InvalidComment);  // slash "/" without a comment
        }



        if(content[index] == '{')
            return MakeToken(TokenType::CurlyBraceOpen, 1);
        if(content[index] == '}')
            return MakeToken(TokenType::CurlyBraceClose, 1);
        if(content[index] == '[')
            return MakeToken(TokenType::SquareBraceOpen, 1);
        if(content[index] == ']')
            return MakeToken(TokenType::SquareBraceClose, 1);

        if(content[index] == '=')
            return MakeToken(TokenType::Equal, 1);
        if(content[index] == ',')
            return MakeToken(TokenType::Comma, 1);
        if(content[index] == '|')
            return MakeToken(TokenType::Pipe, 1);



        if(constexpr_isalpha(content[index]) || content[index] == '_' || constexpr_isdigit(content[index]) || content[index] == '-')
        {
            // read an unquoted key or value through the next structural delimiter
            // the parser validates keys and classifies values
            Token token = Token(TokenType::Atom, index);
            token.line = line;
            token.column = token.startPosition - lastNewLineIndex;

            size_t end = index + 1;
            while(end < content.size())
            {
                const char c = content[end];
                if(constexpr_isspace(c) || c == ',' || c == '=' || c == '|' || c == '/' || c == '"' || c == '\''
                   || c == '{' || c == '}' || c == '[' || c == ']')
                    break;
                end++;
            }

            token.count = static_cast<uint32_t>(end) - token.startPosition;
            index = end >= content.size()? detail::UINT32_MAX_VALUE : static_cast<uint32_t>(end);
            return token;
        }

        return MakeInvalid(DiagnosticType::InvalidToken);
    }
}











namespace fdf::detail
{
}










namespace fdf
{
    constexpr bool Hex::Assign(const std::span<const std::byte> bytes) noexcept
    {
        return TryAssign(bytes);
    }

    constexpr bool Hex::Assign(std::string_view digits) noexcept
    {
        // validate first so a rejected string leaves the old bytes alone
        if(!StripAndValidate(digits))
            return false;

        // sound only while Decode_UNSAFE bails before reallocating, a later failure would
        // restore size over a fresh block of uninitialized bytes
        const uint32_t originalSize = size;
        size = 0;
        if(Decode_UNSAFE(digits, 0))
            return true;

        size = originalSize;
        return false;
    }

    constexpr bool Hex::TryAssign(const std::span<const std::byte> bytes) noexcept
    {
        if(bytes.size() > MaxSize())
            return false;

        const uint32_t count = static_cast<uint32_t>(bytes.size());
        if(!Reallocate(count))
            return false;
        for(uint32_t i = 0; i < count; i++)
            ptr[i] = bytes[i];
        size = count;
        return true;
    }

    // caller validates the digits and strips the 0x prefix
    constexpr bool Hex::Decode_UNSAFE(const std::string_view digits, const uint32_t byteOffset) noexcept
    {
        size_t digitCount = 0;
        for(const char c : digits)
        {
            if(c != '_')
                digitCount++;
        }
        if(digitCount > 2 * static_cast<size_t>(detail::UINT32_MAX_VALUE))
            return false;

        const bool bOdd = digitCount % 2 != 0;
        const uint32_t byteCount = static_cast<uint32_t>((digitCount + 1) / 2);
        if(byteCount > MaxSize() - byteOffset || !Reallocate(byteOffset + byteCount))
            return false;

        auto nibble = [](const char c) noexcept -> int
        {
            if(c >= '0' && c <= '9')
                return c - '0';
            return (c >= 'a'? c - 'a' : c - 'A') + 10;
        };

        size_t digitIndex = 0;
        auto nextNibble = [&]() -> int
        {
            while(digits[digitIndex] == '_')
                digitIndex++;
            return nibble(digits[digitIndex++]);
        };
        for(uint32_t i = 0; i < byteCount; i++)
        {
            const int high = i == 0 && bOdd? 0 : nextNibble();
            const int low = nextNibble();
            ptr[byteOffset + i] = static_cast<std::byte>(high << 4 | low);
        }
        size = std::max(size, byteOffset + byteCount);
        return true;
    }

    constexpr bool Hex::StripAndValidate(std::string_view& digits) noexcept
    {
        if(digits.size() >= 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
            digits.remove_prefix(2);

        for(size_t i = 0; i < digits.size(); i++)
        {
            if(detail::constexpr_ishexdigit(digits[i]))
                continue;
            if(digits[i] == '_' && i > 0 && i + 1 < digits.size()
                && detail::constexpr_ishexdigit(digits[i - 1]) && detail::constexpr_ishexdigit(digits[i + 1]))
                continue;
            return false;
        }
        return true;
    }

    constexpr bool Hex::Decode(std::string_view digits, const size_t byteOffset) noexcept
    {
        if(!StripAndValidate(digits) || byteOffset > size)
            return false;
        return Decode_UNSAFE(digits, static_cast<uint32_t>(byteOffset));
    }

    constexpr bool Hex::Decode(const std::string_view digits) noexcept
    {
        return Decode(digits, size);
    }

    constexpr bool Hex::Reallocate(const uint32_t byteCount) noexcept
    {
        if(byteCount <= capacity)
            return true;

        // double like GlobalAllocator::Reserve
        uint32_t requestedCapacity = byteCount;
        if(capacity != 0)
        {
            const uint64_t doubled = std::min<uint64_t>(static_cast<uint64_t>(capacity) * 2, detail::UINT32_MAX_VALUE);
            requestedCapacity = std::max(static_cast<uint32_t>(doubled), byteCount);
        }

        const auto allocation = detail::GlobalAllocator::Allocate<std::byte>(requestedCapacity, 0);
        if(!allocation.ptr)
            return false;

        std::byte* oldPtr = ptr;
        const uint32_t oldSize = size;
        const uint32_t oldCapacity = capacity;
        ptr = allocation.ptr;
        capacity = allocation.capacity;
        for(uint32_t i = 0; i < oldSize; i++)
            ptr[i] = oldPtr[i];
        detail::GlobalAllocator::Release<std::byte>(oldPtr, 0, oldCapacity);
        return true;
    }

    constexpr void Hex::Free() noexcept
    {
        if(!ptr)
            return;
        detail::GlobalAllocator::Release<std::byte>(ptr, 0, capacity);
        ptr = nullptr;
        size = 0;
        capacity = 0;
    }


    constexpr void String::Grow(uint32_t minCapacity) noexcept
    {
        assert(minCapacity <= max_size() && "String capacity overflow");
        uint32_t newCapacity = Capacity() * 2;
        if(newCapacity < minCapacity)
            newCapacity = minCapacity;

        const uint32_t allocationSize = static_cast<uint32_t>(HEADER_SIZE + static_cast<size_t>(newCapacity) + 1);
        const auto allocation = detail::GlobalAllocator::Allocate<char>(allocationSize, 0);
        char* newPtr = allocation.ptr;
        // capacity excludes the header and terminator
        newCapacity = allocation.capacity - HEADER_SIZE - 1;

        const uint32_t oldSize = static_cast<uint32_t>(size());
        for(uint32_t i = 0; i < oldSize; i++)
            newPtr[HEADER_SIZE + i] = ptr[HEADER_SIZE + i];

        Free();
        ptr = newPtr;
        StoreU32(0, oldSize);
        StoreU32(sizeof(uint32_t), newCapacity);
        ptr[HEADER_SIZE + oldSize] = '\0';
    }

    constexpr void String::Free() noexcept
    {
        if(!ptr)
            return;
        detail::GlobalAllocator::Release<char>(ptr, 0, LoadU32(sizeof(uint32_t)) + HEADER_SIZE + 1);
        ptr = nullptr;
    }


    constexpr Entry::Entry() noexcept
    {
        SetIdentifierSize(0);
        data.e = nullptr;   // default type is Map -> keep e active
    }
    constexpr Entry::~Entry() noexcept
    {
        if(parent)
            (void)parent->OrphanChild_INTERNAL(*this);
        ReleaseEverything();
    }


    constexpr void Entry::SetIdentifier_INTERNAL(std::string_view newIdentifier) noexcept
    {
        SetIdentifierSize(static_cast<uint8_t>(std::min(newIdentifier.size(), detail::MAX_IDENTIFIER_LENGTH)));
        detail::constexpr_memcpy(identifier, newIdentifier.data(), GetIdentifierSize());
        identifier[GetIdentifierSize()] = '\0';
    }

    constexpr bool Entry::SetIdentifier(std::string_view newIdentifier) noexcept
    {
        if(!detail::IsValidIdentifier(newIdentifier))
            return false;
        SetIdentifierSize(static_cast<uint8_t>(newIdentifier.size()));
        detail::constexpr_memcpy(identifier, newIdentifier.data(), GetIdentifierSize());
        identifier[GetIdentifierSize()] = '\0';
        return true;
    }

    constexpr const String& Entry::GetComment() const noexcept
    {
    #if !FDF_NO_COMMENTS
        return comment;
    #else
        static constexpr String EMPTY;
        return EMPTY;
    #endif
    }

#if !FDF_NO_COMMENTS
    constexpr String& Entry::GetComment() noexcept
    {
        return comment;
    }
#endif

    constexpr void Entry::ReleaseData() noexcept
    {
        if(IsDataNull())
            return;

        switch(type)
        {
        case Type::Bool:
            detail::GlobalAllocator::Release<bool>(*this);
            break;
        case Type::Int:
            detail::GlobalAllocator::Release<int64_t>(*this);
            break;
        case Type::Float:
            detail::GlobalAllocator::Release<double>(*this);
            break;
        case Type::String:
            detail::GlobalAllocator::Release<String>(*this);
            break;
        case Type::Timestamp:
            detail::GlobalAllocator::Release<Timestamp>(*this);
            break;
        case Type::Duration:
            detail::GlobalAllocator::Release<Duration>(*this);
            break;
        case Type::Hex:
            detail::GlobalAllocator::Release<Hex>(*this);
            break;
        case Type::Version:
            detail::GlobalAllocator::Release<Version>(*this);
            break;
        case Type::Array:
        case Type::Map:
        {
            (void)ClearChildren();
            detail::GlobalAllocator::Release<Entry*>(*this);
            break;
        }
        case Type::Null:
        default:
            return;
        }

    }

    constexpr void Entry::ReleaseComment() noexcept
    {
    #if !FDF_NO_COMMENTS
        comment = String();   // move-assign frees the old buffer
    #endif
    }

    constexpr void Entry::ReleaseEverything() noexcept
    {
        ReleaseData();
        ReleaseComment();
    }

    constexpr void Entry::AllocateStringArray(uint32_t count) noexcept
    {
        type = Type::String;
        detail::GlobalAllocator::Allocate<String>(*this, count);
    }

    constexpr uint32_t Entry::FindChildIndex(const Entry& e) const noexcept
    {
        assert(IsContainer() && "You can only find index, if it's a container!");
        Entry* const* children = data.e;
        for(uint32_t i = 0; i < size; i++)
        {
            if(children[i] == &e)
                return i;
        }
        return detail::UINT32_MAX_VALUE;
    }

    constexpr uint32_t Entry::FindChildIndex(const std::string_view _identifier) const noexcept
    {
        assert(IsContainer() && "You can only find index, if it's a container!");
        Entry* const* children = data.e;
        for(uint32_t i = 0; i < size; i++)
        {
            if(children[i]->GetIdentifier() == _identifier)
                return i;
        }
        return detail::UINT32_MAX_VALUE;
    }

    constexpr Entry* Entry::Emplace(std::string_view _identifier) noexcept
    {
        assert(IsContainer() && "Sanity check!");

        if(type == Type::Map && !detail::IsValidIdentifier(_identifier))
            return nullptr;

        UniqueEntryPtr e(detail::GlobalAllocator::Create<Entry>());
        if(!e)
            return nullptr;
        if(type == Type::Map)
            e->SetIdentifier_INTERNAL(_identifier);

        return AddChild(e);
    }

    constexpr Entry* Entry::AddChild(UniqueEntryPtr& e) noexcept
    {
        return AddChild(e, DuplicateKeyPolicy::KeepLast);
    }

    constexpr Entry* Entry::AddChild(UniqueEntryPtr& e, DuplicateKeyPolicy policy) noexcept
    {
        if(!e || !IsContainer() || e->parent)
            return nullptr;

        // reject ancestor adoption to prevent ownership cycles
        for(const Entry* ancestor = this; ancestor; ancestor = ancestor->parent)
        {
            if(ancestor == e.get())
                return nullptr;
        }

        if(type == Type::Map)
        {
            if(Entry* found = GetDirectChild(e->GetIdentifier()))
            {
                switch(policy)
                {
                case DuplicateKeyPolicy::Reject:
                    return nullptr;
                case DuplicateKeyPolicy::KeepFirst:
                    e.reset();
                    return found;
                case DuplicateKeyPolicy::Merge:
                    if(found->IsContainer() && e->IsContainer() && found->type == e->type)
                    {
                        while(e->GetChildCount() > 0)
                        {
                            UniqueEntryPtr child = e->OrphanChild(0U);
                            assert(child && "a non-empty container must orphan its first child");
                            [[maybe_unused]] const Entry* added = found->AddChild(child, policy);
                            assert(added && "an orphaned child must be addable during a recursive merge");
                        }
                        e.reset();
                        return found;
                    }
                    break;   // nothing to merge, replace like KeepLast
                case DuplicateKeyPolicy::KeepLast:
                    break;
                default:
                    std::unreachable();
                }

                std::swap(found->type, e->type);
                std::swap(found->size, e->size);
                std::swap(found->capacity, e->capacity);
                std::swap(found->data, e->data);
            #if !FDF_NO_COMMENTS
                std::swap(found->comment, e->comment);
            #endif
                if(found->IsContainer())
                {
                    for(Entry* child : found->GetChildren())
                        child->parent = found;
                }
                e.reset();
                return found;
            }
        }

        // parent only on the append path, a parented duplicate would leave ~Entry chasing an orphan
        e->parent = this;

        assert(size <= capacity && "size must never exceed capacity");
        if(size == capacity)
            detail::GlobalAllocator::Reserve<Entry*>(*this, size + 1);

        const uint32_t index = size;
        detail::GlobalAllocator::Resize<Entry*>(*this, size + 1);
        data.e[index] = e.get();
        return e.release();
    }

    // ReSharper disable once CppParameterMayBeConstPtrOrRef (technically we don't modify provided object, but it's modified through another mechanism)
    constexpr bool Entry::RemoveChild(Entry& e) noexcept
    {
        if(size == 0 || !IsContainer())
            return false;

        const uint32_t index = FindChildIndex(e);
        return index != detail::UINT32_MAX_VALUE? RemoveChild(index) : false;
    }

    constexpr bool Entry::RemoveChild(std::string_view _identifier) noexcept
    {
        if(size == 0 || type != Type::Map)
            return false;

        const uint32_t index = FindChildIndex(_identifier);
        return index != detail::UINT32_MAX_VALUE? RemoveChild(index) : false;
    }

    constexpr bool Entry::RemoveChild(uint32_t index) noexcept
    {
        if(index >= size || !IsContainer())
            return false;

        Entry** children = data.e;
        Entry* child = children[index];
        child->parent = nullptr;  // prevent the destructor from self-orphaning (double removal)
        for(; index + 1 < size; index++)
            children[index] = children[index + 1];
        detail::GlobalAllocator::Resize<Entry*>(*this, size - 1);
        detail::GlobalAllocator::Destroy<Entry>(child);
        return true;
    }

    constexpr bool Entry::ClearChildren() noexcept
    {
        if(!IsContainer() || !data.e)
            return false;

        Entry** children = data.e;
        for(uint32_t i = 0; i < size; i++)
        {
            children[i]->parent = nullptr;  // stop the dtor self-orphaning, which would mutate children mid-loop
            detail::GlobalAllocator::Destroy<Entry>(children[i]);
        }

        detail::GlobalAllocator::Resize<Entry*>(*this, 0);
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
        if(!IsContainer() || !data.e)
            return {};

        std::vector<UniqueEntryPtr> vec;
        vec.reserve(size);
        for(Entry* e : GetChildren())
        {
            e->parent = nullptr;
            vec.emplace_back(e);
        }
        detail::GlobalAllocator::Resize<Entry*>(*this, 0);
        return vec;
    }




    // ReSharper disable once CppParameterMayBeConstPtrOrRef (technically we don't modify provided object, but it's modified through another mechanism)
    constexpr Entry* Entry::OrphanChild_INTERNAL(Entry& e) noexcept
    {
        if(size == 0 || !IsContainer())
            return nullptr;

        const uint32_t index = FindChildIndex(e);
        return index != detail::UINT32_MAX_VALUE? OrphanChild_INTERNAL(index) : nullptr;
    }

    constexpr Entry* Entry::OrphanChild_INTERNAL(std::string_view _identifier) noexcept
    {
        if(size == 0 || !IsContainer())
            return nullptr;

        const uint32_t index = FindChildIndex(_identifier);
        return index != detail::UINT32_MAX_VALUE? OrphanChild_INTERNAL(index) : nullptr;
    }

    constexpr Entry* Entry::OrphanChild_INTERNAL(uint32_t index) noexcept
    {
        if(index >= size || !IsContainer())
            return nullptr;

        Entry** children = data.e;
        Entry* original = children[index];
        for(; index + 1 < size; index++)
            children[index] = children[index + 1];
        original->parent = nullptr;
        detail::GlobalAllocator::Resize<Entry*>(*this, size - 1);
        return original;
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
            while(currentEntry)
            {
                const size_t dotPos = _identifier.find('.');
                const std::string_view cur = _identifier.substr(0, dotPos);

                uint32_t value;
                const auto parsed = std::from_chars(cur.data(), cur.data() + cur.size(), value);
                if(parsed.ptr == cur.data() + cur.size())
                {
                    if(parsed.ec != std::errc())
                        return nullptr;
                    currentEntry = currentEntry->GetDirectChild(value);
                }
                else
                    currentEntry = currentEntry->GetDirectChild(cur);

                if(dotPos == std::string_view::npos)
                    break;
                _identifier.remove_prefix(dotPos + 1);
            }
        }

        if constexpr(sizeof...(args) > 0)
            return currentEntry? currentEntry->GetChild(std::forward<Args>(args)...) : nullptr;
        return currentEntry == this? nullptr : currentEntry;
    }




    constexpr Entry* Entry::GetDirectChild(std::string_view _identifier) noexcept
    {
        return const_cast<Entry*>(std::as_const(*this).GetDirectChild(_identifier));
    }
    constexpr const Entry* Entry::GetDirectChild(std::string_view _identifier) const noexcept
    {
        if(size == 0 || type != Type::Map)
            return nullptr;
        return GetDirectChild(FindChildIndex(_identifier));
    }

    constexpr Entry* Entry::GetDirectChild(uint32_t index) noexcept
    {
        return const_cast<Entry*>(std::as_const(*this).GetDirectChild(index));
    }
    constexpr const Entry* Entry::GetDirectChild(uint32_t index) const noexcept
    {
        if(index >= size || !IsContainer())
            return nullptr;

        return data.e[index];
    }




    constexpr std::span<Entry*> Entry::GetChildren() noexcept
    {
        if(size == 0 || !IsContainer())
            return {};

        return {data.e, size};
    }
    constexpr std::span<const Entry* const> Entry::GetChildren() const noexcept
    {
        if(size == 0 || !IsContainer())
            return {};

        return {const_cast<const Entry* const*>(const_cast<Entry*>(this)->data.e), size};
    }

    constexpr std::span<Entry*> Entry::GetChildren_INTERNAL() noexcept
    {
        assert(IsContainer() && "Unchecked child access requires a container");

        return {data.e, size};
    }
    constexpr std::span<const Entry*> Entry::GetChildren_INTERNAL() const noexcept
    {
        assert(IsContainer() && "Unchecked child access requires a container");

        return {const_cast<const Entry**>(const_cast<Entry*>(this)->data.e), size};
    }










    constexpr size_t Entry::GetChildCountRecursive() const noexcept
    {
        if(size == 0 || !IsContainer())
            return 0;

        size_t total = 0;
        detail::Vector<const Entry*> stack;
        stack.push_back(this);

        while(!stack.empty())
        {
            const Entry* current = stack.back();
            stack.pop_back();

            if((current->type == Type::Array || current->type == Type::Map) && current->size > 0)
            {
                total += current->size;
                {
                    const auto reverseChildren = current->GetChildren_INTERNAL();
                    for(size_t ci = reverseChildren.size(); ci-- > 0; )
                        stack.push_back(reverseChildren[ci]);
                }
            }
        }

        return total;
    }

    template<typename Self>
    constexpr auto Entry::GetChildrenRecursiveImpl(Self& self) noexcept
    {
        using Pointer = Self*;
        if(self.size == 0 || !self.IsContainer())
            return std::vector<Pointer>{};

        detail::Vector<Pointer> stack;
        std::vector<Pointer> result;
        result.reserve(static_cast<size_t>(self.size) + 1);
        stack.push_back(&self);

        while(!stack.empty())
        {
            Pointer current = stack.back();
            stack.pop_back();
            result.push_back(current);

            if((current->type == Type::Array || current->type == Type::Map) && current->size > 0)
            {
                {
                    const auto reverseChildren = current->GetChildren_INTERNAL();
                    for(size_t ci = reverseChildren.size(); ci-- > 0; )
                        stack.push_back(reverseChildren[ci]);
                }
            }
        }

        return result;
    }

    constexpr std::vector<Entry*> Entry::GetChildrenRecursive() noexcept
    {
        return GetChildrenRecursiveImpl(*this);
    }

    constexpr std::vector<const Entry*> Entry::GetChildrenRecursive() const noexcept
    {
        return GetChildrenRecursiveImpl(*this);
    }




    template<std::underlying_type_t<ForEachFlags::Flag> FLAGS, typename Self, typename Callable>
    constexpr void Entry::ForEachImpl(Self& self, Callable& callback) noexcept(std::is_nothrow_invocable_v<Callable, Self&>)
    {
        if constexpr(!ForEachFlags::IsValidForEachFlag(FLAGS))
        {
            static_assert(false, "Invalid flag");
        }
        else if constexpr(ForEachFlags::IsNotSet(FLAGS, ForEachFlags::Recursive | ForEachFlags::Group))
        {
            // Non-recursive, non-sorted
            if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                callback(self);
            for(auto* child : self.GetChildren())
                callback(*child);
        }
        else if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::Group) && ForEachFlags::IsNotSet(FLAGS, ForEachFlags::Recursive))
        {
            // Non-recursive, sorted
            if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                callback(self);

            if(self.size == 0 || !self.IsContainer())
                return;

            const auto children = self.GetChildren_INTERNAL();
            for(auto* e : children)
            {
                if(!e->IsContainer())
                    callback(*e);
            }
            for(auto* e : children)
            {
                if(e->type == Type::Array)
                    callback(*e);
            }
            for(auto* e : children)
            {
                if(e->type == Type::Map)
                    callback(*e);
            }
        }
        else if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::Recursive) && ForEachFlags::IsNotSet(FLAGS, ForEachFlags::Group))
        {
            // Recursive, non-sorted
            if(self.size == 0 || !self.IsContainer())
            {
                if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                    callback(self);
                return;
            }

            using Pointer = Self*;
            detail::Vector<Pointer> stack;
            if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
            {
                stack.push_back(&self);
            }
            else
            {
                {
                    const auto reverseChildren = self.GetChildren_INTERNAL();
                    for(size_t ci = reverseChildren.size(); ci-- > 0; )
                        stack.push_back(reverseChildren[ci]);
                }
            }

            while(!stack.empty())
            {
                Pointer current = stack.back();
                stack.pop_back();
                callback(*current);

                {
                    const auto reverseChildren = current->GetChildren();
                    for(size_t ci = reverseChildren.size(); ci-- > 0; )
                        stack.push_back(reverseChildren[ci]);
                }
            }
        }
        else if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::Recursive | ForEachFlags::Group))
        {
            // Recursive, sorted
            if(self.size == 0 || !self.IsContainer())
            {
                if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                    callback(self);
                return;
            }

            enum class Phase : uint8_t { Pre, InOrder, Leaf, Array, Map };
            struct Frame { Self* e; Phase phase; uint32_t idx;};

            detail::Vector<Frame> stack;
            stack.push_back(Frame{ &self, Phase::Pre, 0 });

            while(!stack.empty())
            {
                Frame& f = stack.back();
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
                            if(f.e != &self)
                                callback(*f.e);
                        }

                        // Arrays are positional: emit children in order. Maps group by type
                        f.phase = f.e->type == Type::Array? Phase::InOrder : Phase::Leaf;
                        break;
                    }
                    case Phase::InOrder:
                    {
                        auto children = f.e->GetChildren_INTERNAL();
                        bool pushed = false;
                        while(f.idx < children.size())
                        {
                            auto* c = children[f.idx++];
                            if(!c->IsContainer())
                            {
                                callback(*c);
                                continue;
                            }
                            stack.push_back(Frame{ c, Phase::Pre, 0 });
                            pushed = true;
                            break;
                        }
                        if(!pushed)
                            stack.pop_back();
                        break;
                    }
                    case Phase::Leaf:
                    {
                        for(auto* c : f.e->GetChildren_INTERNAL())
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
                            auto* c = children[f.idx++];
                            if(c->IsContainer() && c->type == Type::Array)
                            {
                                stack.push_back(Frame{ c, Phase::Pre, 0 });
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
                            auto* c = children[f.idx++];
                            if(c->IsContainer() && c->type == Type::Map)
                            {
                                stack.push_back(Frame{ c, Phase::Pre, 0 });
                                pushed = true;
                                break;
                            }
                        }
                        if(!pushed)
                            stack.pop_back();
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
        ForEachImpl<FLAGS>(*this, callback);
    }

    template<std::underlying_type_t<ForEachFlags::Flag> FLAGS, typename Callable> requires (std::is_invocable_v<Callable, Entry&>)
    constexpr void Entry::ForEach(Callable callback) noexcept(std::is_nothrow_invocable_v<Callable, Entry&>)
    {
        ForEachImpl<FLAGS>(*this, callback);
    }










    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<bool>() noexcept
    {
        return type == Type::Bool? std::span<bool>(data.b, size) : std::span<bool>();
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<int64_t>() noexcept
    {
        return type == Type::Int? std::span<int64_t>(data.i, size) : std::span<int64_t>();
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<int>() noexcept  { return GetValue<int64_t>(); }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<uint64_t>() noexcept
    {
        return type == Type::Int? UIntSpan(data.i, size) : UIntSpan();
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<unsigned int>() noexcept  { return GetValue<uint64_t>(); }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<Version>() noexcept
    {
        return type == Type::Version? std::span<Version>(data.v, size) : std::span<Version>();
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<Timestamp>() noexcept
    {
        return type == Type::Timestamp? std::span<Timestamp>(data.t, size) : std::span<Timestamp>();
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<Duration>() noexcept
    {
        return type == Type::Duration? std::span<Duration>(data.dur, size) : std::span<Duration>();
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<Hex>() noexcept
    {
        return type == Type::Hex? std::span<Hex>(data.h, size) : std::span<Hex>();
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<double>() noexcept
    {
        return type == Type::Float? std::span<double>(data.f, size) : std::span<double>();
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<float>() noexcept  { return GetValue<double>(); }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<String>() noexcept
    {
        return type == Type::String && !IsDataNull()? std::span<String>(data.s, size) : std::span<String>();
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<char>() noexcept  { return GetValue<String>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<std::string>() noexcept  { return GetValue<String>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<std::string_view>() noexcept  { return GetValue<String>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<char*>() noexcept  { return GetValue<String>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<const char*>() noexcept  { return GetValue<String>(); }




    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<bool>() const noexcept
    {
        return type == Type::Bool? std::span<const bool>(data.b, size) : std::span<const bool>();
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<int64_t>() const noexcept
    {
        return type == Type::Int? std::span<const int64_t>(data.i, size) : std::span<const int64_t>();
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<int>() const noexcept  { return GetValue<int64_t>(); }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<uint64_t>() const noexcept
    {
        return type == Type::Int? ConstUIntSpan(data.i, size) : ConstUIntSpan();
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<unsigned int>() const noexcept  { return GetValue<uint64_t>(); }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<Version>() const noexcept
    {
        return type == Type::Version? std::span<const Version>(data.v, size) : std::span<const Version>();
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<Timestamp>() const noexcept
    {
        return type == Type::Timestamp? std::span<const Timestamp>(data.t, size) : std::span<const Timestamp>();
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<Duration>() const noexcept
    {
        return type == Type::Duration? std::span<const Duration>(data.dur, size) : std::span<const Duration>();
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<Hex>() const noexcept
    {
        return type == Type::Hex? std::span<const Hex>(data.h, size) : std::span<const Hex>();
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<double>() const noexcept
    {
        return type == Type::Float? std::span<const double>(data.f, size) : std::span<const double>();
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<float>() const noexcept  { return GetValue<double>(); }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<String>() const noexcept
    {
        return type == Type::String && !IsDataNull()? std::span<const String>(data.s, size) : std::span<const String>();
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<char>() const noexcept  { return GetValue<String>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<std::string>() const noexcept  { return GetValue<String>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<std::string_view>() const noexcept  { return GetValue<String>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<char*>() const noexcept  { return GetValue<String>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<const char*>() const noexcept  { return GetValue<String>(); }










    constexpr void Entry::SetType(Type _type) noexcept
    {
        if(_type == type)
            return;
        ReleaseData();
        type = _type;
        ResetDataNull();
    }

    constexpr void Entry::Resize(const uint32_t _size) noexcept
    {
        switch(type)
        {
        case Type::Bool:    detail::GlobalAllocator::Resize<bool>(*this, _size);     break;
        case Type::Int:     detail::GlobalAllocator::Resize<int64_t>(*this, _size);  break;
        case Type::Float:   detail::GlobalAllocator::Resize<double>(*this, _size);   break;
        case Type::String:  detail::GlobalAllocator::Resize<String>(*this, _size);   break;
        case Type::Version: detail::GlobalAllocator::Resize<Version>(*this, _size);  break;
        case Type::Timestamp: detail::GlobalAllocator::Resize<Timestamp>(*this, _size); break;
        case Type::Duration: detail::GlobalAllocator::Resize<Duration>(*this, _size); break;
        case Type::Hex:     detail::GlobalAllocator::Resize<Hex>(*this, _size);      break;
        default:            return;
        }
    }





    constexpr void Entry::SetValue(NullType) noexcept
    {
        ReleaseData();
        type = Type::Null;
        ResetDataNull();
    }
    constexpr void Entry::SetValue(NilType) noexcept
    {
        SetValue(NullType{});
    }

    constexpr void Entry::SetValue(ArrayType) noexcept
    {
        detail::GlobalAllocator::Repurpose<Type::Array>(*this, 0);
    }
    constexpr void Entry::SetValue(MapType) noexcept
    {
        detail::GlobalAllocator::Repurpose<Type::Map>(*this, 0);
    }

    constexpr void Entry::SetValue(const bool value) noexcept
    {
        detail::GlobalAllocator::Repurpose<Type::Bool>(*this, 1);
        data.b[0] = value;
    }

    constexpr void Entry::SetValue(const std::signed_integral auto value) noexcept
    {
        detail::GlobalAllocator::Repurpose<Type::Int>(*this, 1);
        data.i[0] = static_cast<int64_t>(value);
    }

    constexpr void Entry::SetValue(const std::unsigned_integral auto value) noexcept
    {
        detail::GlobalAllocator::Repurpose<Type::Int>(*this, 1);
        data.i[0] = std::bit_cast<int64_t>(static_cast<uint64_t>(value));
    }

    constexpr void Entry::SetValue(const std::floating_point auto value) noexcept
    {
        detail::GlobalAllocator::Repurpose<Type::Float>(*this, 1);
        data.f[0] = static_cast<double>(value);
    }

    constexpr void Entry::SetValue(const std::string_view value) noexcept
    {
        SetValue(std::span<const std::string_view>(&value, 1));
    }

    constexpr void Entry::SetValue(const std::span<const std::string_view> value) noexcept
    {
        if(!detail::FitsElementCount(value.size()))
            return;

        // A string value is at least one component, so an empty span becomes a single empty string
        const uint32_t count = value.empty()? 1U : static_cast<uint32_t>(value.size());
        detail::GlobalAllocator::Repurpose<Type::String>(*this, count);
        if(value.empty())
        {
            data.s[0].clear();  // a reused component may hold stale text
            return;
        }

        String* arr = data.s;
        for(uint32_t i = 0; i < count; i++)
            arr[i] = value[i];
    }

    constexpr void Entry::SetValue(const char value) noexcept
    {
        SetValue(std::string_view(&value, 1));
    }

    constexpr void Entry::SetValue(const char* value) noexcept
    {
        SetValue(std::string_view(value));
    }

    constexpr void Entry::SetValue(const Timestamp& value) noexcept
    {
        SetValue(std::span<const Timestamp>(&value, 1));
    }

    constexpr void Entry::SetValue(const Duration& value) noexcept
    {
        SetValue(std::span<const Duration>(&value, 1));
    }





    constexpr void Entry::SetValue(std::span<bool> value) noexcept
    {
        if(!detail::FitsElementCount(value.size()))
            return;

        detail::GlobalAllocator::Repurpose<Type::Bool>(*this, static_cast<uint32_t>(value.size()));
        for(size_t i = 0; i < size; i++)
            data.b[i] = value[i];
    }

    template <std::signed_integral T>
    constexpr void Entry::SetValue(std::span<T> value) noexcept
    {
        if(!detail::FitsElementCount(value.size()))
            return;

        detail::GlobalAllocator::Repurpose<Type::Int>(*this, static_cast<uint32_t>(value.size()));
        for(size_t i = 0; i < size; i++)
            data.i[i] = static_cast<int64_t>(value[i]);
    }

    template <std::unsigned_integral T>
    constexpr void Entry::SetValue(std::span<T> value) noexcept
    {
        if(!detail::FitsElementCount(value.size()))
            return;

        detail::GlobalAllocator::Repurpose<Type::Int>(*this, static_cast<uint32_t>(value.size()));
        for(size_t i = 0; i < size; i++)
            data.i[i] = std::bit_cast<int64_t>(static_cast<uint64_t>(value[i]));
    }

    constexpr void Entry::SetValue(const Version& value) noexcept
    {
        SetValue(std::span<const Version>(&value, 1));
    }

    constexpr void Entry::SetValue(const std::span<const Version> value) noexcept
    {
        if(!detail::FitsElementCount(value.size()))
            return;

        detail::GlobalAllocator::Repurpose<Type::Version>(*this, static_cast<uint32_t>(value.size()));
        // a missing revision flag normalizes revision to zero
        auto normalized = [](Version v) noexcept { if(!v.bHasRevision) v.revision = 0; return v; };
        for(size_t i = 0; i < size; i++)
            data.v[i] = normalized(value[i]);
    }

    constexpr void Entry::SetValue(const std::span<const Timestamp> value) noexcept
    {
        if(!detail::FitsElementCount(value.size()))
            return;

        detail::GlobalAllocator::Repurpose<Type::Timestamp>(*this, static_cast<uint32_t>(value.size()));
        for(size_t i = 0; i < size; i++)
            data.t[i] = value[i];
    }

    constexpr void Entry::SetValue(const std::span<const Duration> value) noexcept
    {
        if(!detail::FitsElementCount(value.size()))
            return;

        detail::GlobalAllocator::Repurpose<Type::Duration>(*this, static_cast<uint32_t>(value.size()));
        for(size_t i = 0; i < size; i++)
            data.dur[i] = value[i];
    }

    constexpr void Entry::SetValue(const Hex& value) noexcept
    {
        SetValue(std::span<const Hex>(&value, 1));
    }

    constexpr void Entry::SetValue(const std::span<const Hex> value) noexcept
    {
        if(!detail::FitsElementCount(value.size()))
            return;

        if(type == Type::Hex && value.data())
        {
            for(uint32_t sourceOffset = 0; sourceOffset < size; sourceOffset++)
            {
                if(value.data() != data.h + sourceOffset)
                    continue;

                assert(value.size() <= size - sourceOffset);
                const uint32_t count = static_cast<uint32_t>(value.size());
                for(uint32_t i = 0; i < count; i++)
                    data.h[i] = std::move(data.h[sourceOffset + i]);
                detail::GlobalAllocator::Resize<Hex>(*this, count);
                return;
            }
        }

        detail::GlobalAllocator::Repurpose<Type::Hex>(*this, static_cast<uint32_t>(value.size()));
        for(size_t i = 0; i < size; i++)
            data.h[i] = value[i];
    }

    // steal first: value may live inside this entry's own payload, which Repurpose destroys
    constexpr void Entry::SetValue(Hex&& value) noexcept
    {
        Hex owned = std::move(value);
        detail::GlobalAllocator::Repurpose<Type::Hex>(*this, 1);
        data.h[0] = std::move(owned);
    }

    constexpr void Entry::SetValue(String&& value) noexcept
    {
        String owned = std::move(value);
        detail::GlobalAllocator::Repurpose<Type::String>(*this, 1);
        data.s[0] = std::move(owned);
    }

    template <std::floating_point T>
    constexpr void Entry::SetValue(std::span<T> value) noexcept
    {
        if(!detail::FitsElementCount(value.size()))
            return;

        detail::GlobalAllocator::Repurpose<Type::Float>(*this, static_cast<uint32_t>(value.size()));
        for(size_t i = 0; i < size; i++)
            data.f[i] = static_cast<double>(value[i]);
    }










#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4702)
#endif
    template<Style STYLE>
    [[nodiscard]] constexpr std::string_view Entry::DataToView(String& temp) const noexcept
    {
        switch(type)
        {
            case Type::Null:    if constexpr(STYLE.bUseNilInsteadOfNull) return detail::KEYWORDS[1]; return detail::KEYWORDS[0];
            case Type::Array:   return detail::ARRAY_TEXT;
            case Type::Map:     return detail::MAP_TEXT;

            case Type::String:
            {
                // the writer quotes string components itself, this view only serves component 0
                const std::span<const String> text = GetValue<String>();
                return text.empty()? std::string_view() : std::string_view(text[0]);
            }

            case Type::Timestamp:
            {
                const std::span<const Timestamp> span = GetValue<Timestamp>();
                if(span.empty())
                    return {};
                // any invalid component sinks the whole value to null, a pack is one atomic value
                for(const Timestamp& component : span)
                {
                    if(!component.IsValid())
                        return STYLE.bUseNilInsteadOfNull? detail::KEYWORDS[1] : detail::KEYWORDS[0];
                }
                temp.clear();
                for(size_t i = 0; i < span.size(); i++)
                {
                    if(i) temp.push_back('|');
                    span[i].AppendTo(temp, STYLE.bUppercaseTimestamp);
                }
                return temp;
            }

            case Type::Duration:
            {
                const std::span<const Duration> span = GetValue<Duration>();
                if(span.empty())
                    return {};
                temp.clear();
                for(size_t i = 0; i < span.size(); i++)
                {
                    if(i) temp.push_back('|');
                    span[i].AppendTo(temp);
                }
                return temp;
            }

            case Type::Hex:
            {
                const std::span<const Hex> span = GetValue<Hex>();
                if(span.empty())
                    return {};
                constexpr std::string_view HEX_DIGITS = STYLE.bUppercaseHex? "0123456789ABCDEF" : "0123456789abcdef";
                temp.clear();
                for(size_t c = 0; c < span.size(); c++)
                {
                    if(c) temp.push_back('|');
                    temp.append("0x");
                    const std::span<const std::byte> bytes = span[c].Bytes();
                    const size_t digitCount = bytes.size() * 2;
                    size_t digitIndex = 0;
                    auto appendHexDigit = [&](const char digit)
                    {
                        if constexpr(STYLE.hexDigitGrouping != 0)
                        {
                            if(digitIndex != 0 && (digitCount - digitIndex) % STYLE.hexDigitGrouping == 0)
                                temp.push_back('_');
                        }
                        temp.push_back(digit);
                        digitIndex++;
                    };
                    for(size_t i = 0; i < bytes.size(); i++)
                    {
                        const uint8_t b = std::to_integer<uint8_t>(bytes[i]);
                        appendHexDigit(HEX_DIGITS[b >> 4]);
                        appendHexDigit(HEX_DIGITS[b & 0xF]);
                    }
                }
                return temp;
            }

            case Type::Version:
            {
                const std::span<const Version> span = GetValue<Version>();
                if(span.empty())
                    return {};
                temp.clear();
                for(size_t i = 0; i < span.size(); i++)
                {
                    if(i) temp.push_back('|');
                    detail::AppendUInt(temp, span[i].major);
                    temp.push_back('.');
                    detail::AppendUInt(temp, span[i].minor);
                    temp.push_back('.');
                    detail::AppendUInt(temp, span[i].patch);
                    if(span[i].bHasRevision)
                    {
                        temp.push_back('.');
                        detail::AppendUInt(temp, span[i].revision);
                    }
                }
                return temp;
            }

            case Type::Bool:
            {
                const auto span = GetValue<bool>();
                if(span.empty())
                    return {};
                temp.clear();
                for(size_t i = 0; i < span.size(); i++)
                {
                    if(i) temp.push_back('|');
                    temp.append(span[i]? detail::KEYWORDS[2] : detail::KEYWORDS[3]);
                }
                return temp;
            }

            case Type::Int:
            {
                const auto span = GetValue<int64_t>();
                if(span.empty())
                    return {};
                temp.clear();
                for(size_t i = 0; i < span.size(); i++)
                {
                    if(i) temp.push_back('|');
                    const size_t start = temp.size();
                    detail::AppendInt(temp, span[i]);
                    detail::GroupIntegerPart(temp, start, STYLE.intDigitGrouping);
                }
                return temp;
            }

            case Type::Float:
            {
                const auto span = GetValue<float>();
                if(span.empty())
                    return {};
                // an all-ones exponent is inf/nan, neither has text syntax
                for(const double component : span)
                {
                    if((std::bit_cast<uint64_t>(component) >> 52 & 0x7FF) == 0x7FF)
                        return STYLE.bUseNilInsteadOfNull? detail::KEYWORDS[1] : detail::KEYWORDS[0];
                }
                temp.clear();
                for(size_t i = 0; i < span.size(); i++)
                {
                    if(i) temp.push_back('|');
                    const size_t start = temp.size();
                    detail::AppendDouble(temp, span[i]);
                    detail::GroupIntegerPart(temp, start, STYLE.intDigitGrouping);
                }
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
    inline bool Entry::ParseCombineFile(const std::filesystem::path& filepath, CommentCombineStrategy fileCommentCombineStrategy, DuplicateKeyPolicy policy) noexcept
    {
        detail::NoDiagnostics sink;
        return ParseCombineFile(filepath, fileCommentCombineStrategy, policy, sink);
    }

    template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
    bool Entry::ParseCombineFile(const std::filesystem::path& filepath, SINK&& sink) noexcept
    {
        return ParseCombineFile(filepath, CommentCombineStrategy::UseNewIfExistingIsEmpty, DuplicateKeyPolicy::Merge, std::forward<SINK>(sink));
    }

    template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
    bool Entry::ParseCombineFile(const std::filesystem::path& filepath, CommentCombineStrategy fileCommentCombineStrategy, DuplicateKeyPolicy policy, SINK&& sink) noexcept
    {
        std::error_code ec;
        if(!std::filesystem::is_regular_file(filepath, ec) || ec)
            return false;

        const auto fileSize = std::filesystem::file_size(filepath, ec);
        if(ec)
            return false;
        if(fileSize > String::max_size())
        {
            if constexpr(!std::is_same_v<std::remove_cvref_t<SINK>, detail::NoDiagnostics>)
                sink(Diagnostic{ DiagnosticSeverity::Fatal, DiagnosticType::InputTooLarge, {}, 0, 0, 0 });
            return false;
        }

        std::ifstream file(filepath, std::ios::binary);
        if(!file)
            return false;

        String content(static_cast<size_t>(fileSize), '\0');
        if(fileSize > 0 && !file.read(content.data(), static_cast<std::streamsize>(fileSize)))
            return false;
        return ParseCombineBuffer(std::string_view(content), fileCommentCombineStrategy, policy, std::forward<SINK>(sink));
    }

    constexpr bool Entry::ParseCombineBuffer(std::string_view content, CommentCombineStrategy fileCommentCombineStrategy, DuplicateKeyPolicy policy) noexcept
    {
        detail::NoDiagnostics sink;
        return ParseCombineBuffer(content, fileCommentCombineStrategy, policy, sink);
    }

    template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
    constexpr bool Entry::ParseCombineBuffer(std::string_view content, SINK&& sink) noexcept
    {
        return ParseCombineBuffer(content, CommentCombineStrategy::UseNewIfExistingIsEmpty, DuplicateKeyPolicy::Merge, std::forward<SINK>(sink));
    }

    template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
    constexpr bool Entry::ParseCombineBuffer(std::string_view content, CommentCombineStrategy fileCommentCombineStrategy, DuplicateKeyPolicy policy, SINK&& sink) noexcept
    {
        if(type != Type::Map)
            return false;

        UniqueEntryPtr other = ParseBuffer(content, sink);
        return Combine(other, fileCommentCombineStrategy, policy);
    }

    constexpr bool Entry::Combine(UniqueEntryPtr& other, [[maybe_unused]] CommentCombineStrategy fileCommentCombineStrategy, DuplicateKeyPolicy policy) noexcept
    {
        if(!other || !IsContainer() || type != other->type || other.get() == this)
            return false;

        if(other->size > detail::UINT32_MAX_VALUE - size)
            return false;
        detail::GlobalAllocator::Reserve<Entry*>(*this, size + other->size);

    #if !FDF_NO_COMMENTS
        switch(fileCommentCombineStrategy)
        {
        case CommentCombineStrategy::UseExisting: break;
        case CommentCombineStrategy::UseNew: GetComment() = other->GetComment(); break;
        case CommentCombineStrategy::UseNewIfExistingIsEmpty:
            if(GetComment().empty())
                GetComment() = other->GetComment();
            break;
        case CommentCombineStrategy::Merge:
            if(GetComment().empty())
                GetComment() = other->GetComment();
            else if(!other->GetComment().empty())
            {
                // the writer collapses the '\n' into a space at emit
                comment.push_back('\n');
                comment.append(other->GetComment());
            }
            break;
        case CommentCombineStrategy::Clear: ReleaseComment(); break;
        default: std::unreachable();
        }
    #endif

        // policy rejection drops only the incoming child
        while(other->GetChildCount() > 0)
        {
            UniqueEntryPtr child = other->OrphanChild(0U);
            assert(child && "a non-empty container must orphan its first child");
            [[maybe_unused]] const Entry* added = AddChild(child, policy);
            assert((added || policy == DuplicateKeyPolicy::Reject)
                && "an orphaned child must be addable unless Reject refused it");
        }
        other.reset();
        return true;
    }





    template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
    UniqueEntryPtr ParseFile(const std::filesystem::path& filepath, SINK&& sink) noexcept
    {
        std::error_code ec;
        if(!std::filesystem::is_regular_file(filepath, ec) || ec)
            return nullptr;

        // Reject an oversized file before reading it into memory, offsets are 32-bit
        const auto fileSize = std::filesystem::file_size(filepath, ec);
        if(ec)
            return nullptr;
        if(fileSize > String::max_size())
        {
            if constexpr(!std::is_same_v<std::remove_cvref_t<SINK>, detail::NoDiagnostics>)
                sink(Diagnostic{ DiagnosticSeverity::Fatal, DiagnosticType::InputTooLarge, {}, 0, 0, 0 });
            return nullptr;
        }

        std::ifstream file(filepath, std::ios::binary);
        if(!file)
            return nullptr;

        String content(static_cast<size_t>(fileSize), '\0');
        if(fileSize > 0 && !file.read(content.data(), static_cast<std::streamsize>(fileSize)))
            return nullptr;
        return ParseBuffer(std::string_view(content), sink);
    }

    template<typename SINK> requires std::is_invocable_v<SINK&, const Diagnostic&>
    constexpr UniqueEntryPtr ParseBuffer(std::string_view content, SINK&& sink) noexcept
    {
        using namespace detail;

        // UINT32_MAX_VALUE is both the offset limit and eof sentinel
        if(content.size() >= detail::UINT32_MAX_VALUE)
        {
            if constexpr(!std::is_same_v<std::remove_cvref_t<SINK>, detail::NoDiagnostics>)
                sink(Diagnostic{ DiagnosticSeverity::Fatal, DiagnosticType::InputTooLarge, {}, 0, 0, 0 });
            return nullptr;
        }

        if(content.size() >= 3 &&
           static_cast<uint8_t>(content[0]) == 0xEF &&
           static_cast<uint8_t>(content[1]) == 0xBB &&
           static_cast<uint8_t>(content[2]) == 0xBF)
            content = content.substr(3);

        if constexpr(!std::is_same_v<std::remove_cvref_t<SINK>, NoDiagnostics>)
        {
            if(const size_t badAt = detail::Utf8FirstInvalidByte(content); badAt != content.size())
                sink(Diagnostic{ DiagnosticSeverity::Warning, DiagnosticType::InvalidUtf8, {}, 0, 0, static_cast<uint32_t>(badAt) });
        }

        Tokenizer tokenizer(content);
        #if !FDF_NO_COMMENTS
            Token fileCommentToken = TokenType::NonExisting;
        #endif

        UniqueEntryPtr root(GlobalAllocator::Create<Entry>());
        if(!root)
            return nullptr;
        root->type = Type::Map;

        while(true)
        {
            #if !FDF_NO_COMMENTS
                Token comment = TokenType::NonExisting;
            #endif
            Token currentToken = tokenizer.Current();
            if(currentToken.type == TokenType::Invalid)
            {
                Diagnose(DiagnosticSeverity::Fatal, static_cast<DiagnosticType>(currentToken.extra8), tokenizer, currentToken, sink);
                return nullptr;
            }

            while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
            {
            #if !FDF_NO_COMMENTS
                if(currentToken.type == TokenType::Comment)
                {
                    // only a block comment can be the file comment. A '//' line starting with '#'
                    // is an ordinary entry comment, and claiming it here stole it from the entry
                    if(root->size == 0 && root->comment.empty() && currentToken.extra8 == 1
                       && currentToken.count > 0 && content[currentToken.startPosition] == '#')
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

                        if(fileCommentToken.type != TokenType::NonExisting)
                            Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken, sink);
                        fileCommentToken = currentToken;
                    }
                    else
                    {
                        if(comment.type != TokenType::NonExisting)
                            Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken, sink);
                        comment = currentToken;
                    }
                }
            #endif

                currentToken = tokenizer.Advance();
            }

            if(currentToken.type == TokenType::Atom)
            {
                const uint32_t childCountBefore = root->GetChildCount();
                if(!ParseVariable(tokenizer, *root   FDF_COMMENT_SWITCH(,comment), sink))
                {
                    // remove the partial entry before recovering at the next line
                    while(root->GetChildCount() > childCountBefore)
                        (void)root->RemoveChild(root->GetChildCount() - 1);

                    if(tokenizer.Current().type == TokenType::Invalid)
                    {
                        Diagnose(DiagnosticSeverity::Fatal, static_cast<DiagnosticType>(tokenizer.Current().extra8), tokenizer, tokenizer.Current(), sink);
                        return nullptr;  // Lexer error: can't reliably resume
                    }
                    SkipToNextEntry(tokenizer, false);  // ParseVariable already reported the error
                }

                continue;
            }

            if(currentToken.type == TokenType::EndOfFile)
                break;

            Diagnose(DiagnosticSeverity::Error, DiagnosticType::UnexpectedToken, tokenizer, currentToken, sink);
            SkipToNextEntry(tokenizer, false);
        }

        #if !FDF_NO_COMMENTS
            // Stored raw, the writer strips per-line leading whitespace when emitting the block
            if(fileCommentToken.type != TokenType::NonExisting)
                root->comment = tokenizer.ToView(fileCommentToken);
        #endif

        return root;
    }

    template<Style STYLE>
    bool WriteFile(const Entry& e, const std::filesystem::path& filepath, bool bCreateIfNotExists) noexcept
    {
        std::error_code ec;
        const bool bFileExists = std::filesystem::exists(filepath, ec);
        if(ec)
            return false;
        if(bFileExists)
        {
            if(!std::filesystem::is_regular_file(filepath, ec) || ec)
                return false;
        }
        else if(!bCreateIfNotExists)
            return false;

        const std::filesystem::path parentDir = filepath.parent_path();
        if(!parentDir.empty())
        {
            const bool bParentExists = std::filesystem::exists(parentDir, ec);
            if(ec)
                return false;
            if(!bParentExists && (!bCreateIfNotExists || !std::filesystem::create_directories(parentDir, ec) || ec))
                return false;
            if(bParentExists && (!std::filesystem::is_directory(parentDir, ec) || ec))
                return false;
        }

        // serialize first because opening truncates the existing file
        String buffer = WriteBuffer<STYLE>(e);

        std::filesystem::path tempPath;
        for(uint32_t attempt = 0; attempt < 1024; attempt++)
        {
            std::filesystem::path candidate = filepath;
            candidate += std::format(".{}.tmp", attempt);
            const bool bTaken = std::filesystem::exists(candidate, ec);
            if(ec)
                return false;
            if(!bTaken)
            {
                tempPath = std::move(candidate);
                break;
            }
        }
        if(tempPath.empty())
            return false;

        {
            // Preserve platform newline translation
            std::ofstream file(tempPath);
            if(!file)
                return false;

            file.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            file.flush();
            if(!file)
            {
                file.close();
                std::filesystem::remove(tempPath, ec);
                return false;
            }
        }

        std::filesystem::rename(tempPath, filepath, ec);
        if(ec)
        {
            std::error_code cleanupEc;
            std::filesystem::remove(tempPath, cleanupEc);
            return false;
        }
        return true;
    }

    constexpr UniqueEntryPtr NewEntry() noexcept
    {
        return UniqueEntryPtr(detail::GlobalAllocator::Create<Entry>());
    }
}

namespace fdf::detail
{
    constexpr void EntryDeleter::operator()(Entry* e) noexcept
    {
        GlobalAllocator::Destroy<Entry>(e);
    }

    template<typename SINK>
    constexpr void Diagnose(DiagnosticSeverity severity, DiagnosticType type, const Tokenizer& tokenizer, const Token& token, SINK& sink) noexcept
    {
        if constexpr(!std::is_same_v<std::remove_cvref_t<SINK>, NoDiagnostics>)
        {
            static_assert(IsValidDiagnosticCallback<SINK&>, "the diagnostic sink must be invocable with (const Diagnostic&)");
            sink(Diagnostic{ severity, type, tokenizer.ToView(token), token.line, token.column, token.startPosition });
        }
    }

    // skips a malformed entry while tracking nested braces
    // bStopAtCloseBrace leaves the enclosing close brace for the caller
    constexpr void SkipToNextEntry(Tokenizer& tokenizer, bool bStopAtCloseBrace) noexcept
    {
        uint32_t depth = 0;
        while(true)
        {
            const Token current = tokenizer.Current();
            switch(current.type)
            {
            case TokenType::EndOfFile:
            case TokenType::Invalid:
                return;
            case TokenType::CurlyBraceOpen:
            case TokenType::SquareBraceOpen:
                depth++;
                break;
            case TokenType::CurlyBraceClose:
            case TokenType::SquareBraceClose:
                if(depth == 0)
                {
                    if(bStopAtCloseBrace)
                        return;  // belongs to the enclosing container, let its loop handle it
                }
                else
                    depth--;
                break;
            case TokenType::Comma:
            case TokenType::NewLine:
                if(depth == 0)
                {
                    (void)tokenizer.Advance();  // consume the separator, resume at the next entry
                    return;
                }
                break;
            default:
                break;
            }

            (void)tokenizer.Advance();
        }
    }


    template<typename SINK>
    [[nodiscard]] constexpr bool ParseVariable(Tokenizer& tokenizer, Entry& parent FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept
    {
        Token currentToken = tokenizer.Current();
        const Token keyToken = currentToken;
        UniqueEntryPtr _temp(GlobalAllocator::Create<Entry>());
        if(!_temp)
            return false;

        // parse detached so an invalid duplicate cannot replace the existing entry
        Entry* entry = _temp.get();
        if(parent.type == Type::Map)
        {
            assert(tokenizer.Current().type == TokenType::Atom && "Sanity check!");
            if(!entry->SetIdentifier(tokenizer.ToView(currentToken)))
            {
                Diagnose(DiagnosticSeverity::Error, DiagnosticType::InvalidIdentifier, tokenizer, currentToken, sink);
                return false;  // caller recovers: skips to the next entry
            }
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
                if(comment.type != TokenType::NonExisting)
                    Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken, sink);
                comment = currentToken;
            }
        #endif

            currentToken = tokenizer.Advance();
            FDF_CHECK_TOKEN(currentToken);
            FDF_CHECK_TOKEN_FOR_EOF(currentToken);
        }

        bool bParsed = false;
        if(IsValueLiteral(currentToken.type) && (bHasEqual || parent.type == Type::Array))
            bParsed = ParseSimpleValue(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment), sink);
        // '=' introduces a scalar value only; a container must follow the identifier directly
        else if(bHasEqual && (currentToken.type == TokenType::CurlyBraceOpen || currentToken.type == TokenType::SquareBraceOpen))
        {
            Diagnose(DiagnosticSeverity::Error, DiagnosticType::UnexpectedToken, tokenizer, currentToken, sink);
            return false;
        }
        else if(currentToken.type == TokenType::CurlyBraceOpen)
            bParsed = ParseMap(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment), sink);
        else if(currentToken.type == TokenType::SquareBraceOpen)
            bParsed = ParseArray(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment), sink);
        else
        {
            Diagnose(DiagnosticSeverity::Error, DiagnosticType::UnexpectedToken, tokenizer, currentToken, sink);
            return false;
        }

        if(!bParsed)
            return false;

        // _temp is freshly created, unparented and cannot be an ancestor, and parent is a
        // container, so AddChild's guards are all unreachable here and only a duplicate key fails
        assert(_temp && !_temp->parent && parent.IsContainer() && "only a duplicate key may fail this insert");
        if(parent.AddChild(_temp, DuplicateKeyPolicy::Reject))
            return true;

        // the entry was fully consumed, so report and continue rather than letting the caller
        // recover and swallow whatever follows
        Diagnose(DiagnosticSeverity::Error, DiagnosticType::DuplicateKey, tokenizer, keyToken, sink);
        return true;
    }
    template<typename SINK>
    [[nodiscard]] constexpr bool ParseSimpleValue(Tokenizer& tokenizer, Entry& entry FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept
    {
        assert(IsValueLiteral(tokenizer.Current().type) && "Sanity check!");

        Token currentToken = tokenizer.Current();
        const Token valueToken = currentToken;  // start of the value, used for diagnostics

        // The gather leaves currentToken on the first token past the value, so postProcess must not advance first
        auto postProcess = [&]()
        {
            if(currentToken.type == TokenType::Comma)
            {
                currentToken = tokenizer.Advance();
                FDF_CHECK_TOKEN(currentToken);
                FDF_CHECK_TOKEN_FOR_EOF(currentToken);
            }

            if(currentToken.type == TokenType::Comment)
            {
            #if !FDF_NO_COMMENTS
                if(comment.type != TokenType::NonExisting)
                    Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken, sink);
                comment = currentToken;
            #endif
                currentToken = tokenizer.Advance();
            }

            if(currentToken.type == TokenType::NewLine)
                (void)tokenizer.Advance();

        #if !FDF_NO_COMMENTS
            if(comment.type != TokenType::NonExisting)
                entry.GetComment() = tokenizer.ToView(comment);
        #endif
            return true;
        };

        // StringLiteral is already a string and ClassifyAtom resolves atoms
        // structural errors recover here, content errors in the type parser
        auto classifyToken = [&](Token token, Type& out) -> bool
        {
            const std::string_view v = tokenizer.ToView(token);
            if(token.type == TokenType::StringLiteral)
            {
                out = Type::String;
                return true;
            }

            out = Type::String;
            if(!ClassifyAtom(v, out))
            {
                Diagnose(DiagnosticSeverity::Error, DiagnosticType::InvalidNumber, tokenizer, token, sink);
                return false;
            }
            return true;
        };

        // replaying the cheap tokenizer avoids keeping a heap-backed token list
        const Tokenizer componentStart = tokenizer;
        Type firstType;
        if(!classifyToken(currentToken, firstType))
            return false;

        bool bAllBool      = true;
        bool bAllNumeric   = true;
        bool bAllString    = true;
        bool bAllHex       = true;
        bool bAllVersion   = true;
        bool bAllTimestamp = true;
        bool bAllDuration  = true;
        bool bAnyFloat     = false;
        auto includeType = [&](Type type)
        {
            bAllBool      = bAllBool      && type == Type::Bool;
            bAllNumeric   = bAllNumeric   && (type == Type::Int || type == Type::Float);
            bAllString    = bAllString    && type == Type::String;
            bAllHex       = bAllHex       && type == Type::Hex;
            bAllVersion   = bAllVersion   && type == Type::Version;
            bAllTimestamp = bAllTimestamp && type == Type::Timestamp;
            bAllDuration  = bAllDuration  && type == Type::Duration;
            bAnyFloat     = bAnyFloat     || type == Type::Float;
        };
        includeType(firstType);

        uint32_t componentCount = 1;
        currentToken = tokenizer.Advance();
        if(currentToken.type == TokenType::Pipe)
        {
            while(currentToken.type == TokenType::Pipe)
            {
                currentToken = tokenizer.Advance();
                FDF_CHECK_TOKEN(currentToken);
                if(!IsValueLiteral(currentToken.type))
                {
                    Diagnose(DiagnosticSeverity::Error, DiagnosticType::InvalidPack, tokenizer, valueToken, sink);
                    return false;  // dangling '|' with no component
                }

                Type componentType;
                if(!classifyToken(currentToken, componentType))
                    return false;
                includeType(componentType);
                componentCount++;

                currentToken = tokenizer.Advance();
            }
        }

        struct ComponentReader
        {
            Tokenizer tokenizer;
            uint32_t remaining;

            [[nodiscard]] constexpr std::string_view Next() noexcept
            {
                assert(remaining > 0 && IsValueLiteral(tokenizer.Current().type));
                const std::string_view result = tokenizer.ToView(tokenizer.Current());
                remaining--;
                if(remaining > 0)
                {
                    [[maybe_unused]] const Token pipe = tokenizer.Advance();
                    assert(pipe.type == TokenType::Pipe);
                    [[maybe_unused]] const Token component = tokenizer.Advance();
                    assert(IsValueLiteral(component.type));
                }
                return result;
            }
        };
        auto makeComponentReader = [&]() { return ComponentReader{ componentStart, componentCount }; };

        // packs need a common component type
        if(componentCount > 1 && !bAllBool && !bAllNumeric && !bAllString && !bAllHex && !bAllVersion && !bAllTimestamp && !bAllDuration)
        {
            Diagnose(DiagnosticSeverity::Error, DiagnosticType::InvalidPack, tokenizer, valueToken, sink);
            return false;
        }

        if(bAllBool)
        {
            entry.type = Type::Bool;
            detail::GlobalAllocator::Allocate<bool>(entry, componentCount);

            auto componentReader = makeComponentReader();
            for(uint32_t i = 0; i < componentCount; i++)
            {
                const bool value = componentReader.Next() == KEYWORDS[2];
                entry.data.b[i] = value;
            }

            return postProcess();
        }




        if(bAllVersion)
        {
            entry.type = Type::Version;
            detail::GlobalAllocator::Allocate<Version>(entry, componentCount);

            auto fail = [&]() -> bool
            {
                Diagnose(DiagnosticSeverity::Error, DiagnosticType::InvalidNumber, tokenizer, valueToken, sink);
                entry.ReleaseData();
                return false;
            };

            auto componentReader = makeComponentReader();
            for(uint32_t versionIndex = 0; versionIndex < componentCount; versionIndex++)
            {
                Version& version = entry.data.v[versionIndex];
                uint32_t componentIndex = 0;
                uint64_t result = 0;
                bool bHasDigit = false;

                auto storeComponent = [&]() -> bool
                {
                    if(!bHasDigit || result > detail::UINT32_MAX_VALUE || (componentIndex == 0 && result > 0x7FFFFFFFU))
                        return false;

                    switch(componentIndex)
                    {
                        #if defined(__GNUC__) && !defined(__clang__)
                            #pragma GCC diagnostic push
                            #pragma GCC diagnostic ignored "-Wconversion"
                        #endif
                        case 0: version.major    = static_cast<uint32_t>(result); break;
                        #if defined(__GNUC__) && !defined(__clang__)
                            #pragma GCC diagnostic pop
                        #endif
                        case 1: version.minor    = static_cast<uint32_t>(result); break;
                        case 2: version.patch    = static_cast<uint32_t>(result); break;
                        case 3: version.revision = static_cast<uint32_t>(result); break;
                        default: return false;
                    }
                    componentIndex++;
                    result = 0;
                    bHasDigit = false;
                    return true;
                };

                for(char c : componentReader.Next())
                {
                    if(constexpr_isdigit(c))
                    {
                        const uint64_t digit = static_cast<uint64_t>(c - '0');
                        if(result > (detail::UINT32_MAX_VALUE - digit) / 10)
                            return fail();
                        result = result * 10 + digit;
                        bHasDigit = true;
                    }
                    else if(c != '.' || !storeComponent())
                        return fail();
                }

                if(!storeComponent() || (componentIndex != 3 && componentIndex != 4))
                    return fail();
                version.bHasRevision = componentIndex == 4;
            }
            return postProcess();
        }




        if(bAllNumeric && !bAnyFloat)
        {
            entry.type = Type::Int;
            detail::GlobalAllocator::Allocate<int64_t>(entry, componentCount);

            auto fail = [&]() -> bool
            {
                Diagnose(DiagnosticSeverity::Error, DiagnosticType::InvalidNumber, tokenizer, valueToken, sink);
                entry.ReleaseData();
                return false;
            };

            auto componentReader = makeComponentReader();
            for(uint32_t d = 0; d < componentCount; d++)
            {
                const std::string_view seg = componentReader.Next();
                uint64_t result = 0;
                bool bIsNegative = false;
                bool bIsFirstChar = true;
                bool bComponentHasDigit = false;

                for(size_t i = 0; i < seg.size(); i++)
                {
                    const char c = seg[i];
                    if(bIsFirstChar && c == '-')
                        bIsNegative = true;
                    else if(constexpr_isdigit(c))
                    {
                        if(result > UINT64_MAX_VALUE / 10)
                            return fail();  // Overflow

                        result *= 10;

                        const uint64_t digit = static_cast<uint64_t>(c - '0');
                        if(result > UINT64_MAX_VALUE - digit)
                            return fail();  // Overflow

                        result += digit;
                        bComponentHasDigit = true;
                    }
                    else if(c != '_' || i == 0 || i + 1 >= seg.size()
                        || !constexpr_isdigit(seg[i - 1]) || !constexpr_isdigit(seg[i + 1]))
                        return fail();  // unknown character

                    bIsFirstChar = false;
                }

                if(!bComponentHasDigit)
                    return fail();  // empty component like "-"

                if(bIsNegative)
                {
                    constexpr uint64_t INT64_MIN_MAGNITUDE = static_cast<uint64_t>(INT64_MAX_VALUE) + 1U;
                    if(result > INT64_MIN_MAGNITUDE)
                        return fail();

                    entry.data.i[d] = result == INT64_MIN_MAGNITUDE
                        ? std::numeric_limits<int64_t>::min()
                        : -static_cast<int64_t>(result);
                }
                else
                {
                    // values past INT64_MAX keep their unsigned bit pattern, GetValue<uint64_t> reads them back
                    entry.data.i[d] = std::bit_cast<int64_t>(result);
                }
            }

            return postProcess();
        }




        // Numeric widening: any float component makes the whole pack float
        if(bAllNumeric)
        {
            entry.type = Type::Float;
            detail::GlobalAllocator::Allocate<double>(entry, componentCount);

            auto componentReader = makeComponentReader();
            for(uint32_t d = 0; d < componentCount; d++)
            {
                const std::string_view seg = componentReader.Next();
                bool bOk = false;
                const double result = detail::ParseDouble(seg.data(), seg.data() + seg.size(), &bOk);
                // overflow returns +/-inf, which has no text syntax
                if(!bOk || (std::bit_cast<uint64_t>(result) >> 52 & 0x7FF) == 0x7FF)
                {
                    Diagnose(DiagnosticSeverity::Error, DiagnosticType::InvalidNumber, tokenizer, valueToken, sink);
                    entry.ReleaseData();
                    return false;
                }

                entry.data.f[d] = result;
            }

            return postProcess();
        }

        // scalar strings and packs share a String[count] slab
        if(bAllString)
        {
            entry.AllocateStringArray(componentCount);

            String* dest = entry.data.s;

            // decode each literal straight into its stored component, no transient buffer
            auto componentReader = makeComponentReader();
            for(uint32_t i = 0; i < componentCount; i++)
            {
                const std::string_view literal = componentReader.Next();
                dest[i].reserve(literal.size() - 2);   // decoded length <= literal length minus the quotes
                DecodeStringLiteral(literal, [&](char c) { dest[i].push_back(c); });
            }

            return postProcess();
        }

        if(bAllHex)
        {
            entry.type = Type::Hex;
            detail::GlobalAllocator::Allocate<Hex>(entry, componentCount);

            auto componentReader = makeComponentReader();
            for(uint32_t i = 0; i < componentCount; i++)
            {
                // tokenizer already validated the digits, only an oversized component fails here
                if(!entry.data.h[i].Decode_UNSAFE(componentReader.Next().substr(2), 0))
                {
                    Diagnose(DiagnosticSeverity::Error, DiagnosticType::InputTooLarge, tokenizer, valueToken, sink);
                    entry.ReleaseData();
                    return false;
                }
            }

            return postProcess();
        }

        if(bAllTimestamp)
        {
            entry.type = Type::Timestamp;
            detail::GlobalAllocator::Allocate<Timestamp>(entry, componentCount);

            auto componentReader = makeComponentReader();
            for(uint32_t i = 0; i < componentCount; i++)
            {
                const Timestamp timestamp = Timestamp::FromText(componentReader.Next());
                if(!timestamp.IsValid())
                {
                    Diagnose(DiagnosticSeverity::Error, DiagnosticType::InvalidTimestamp, tokenizer, valueToken, sink);
                    entry.ReleaseData();
                    return false;
                }
                entry.data.t[i] = timestamp;
            }

            return postProcess();
        }

        if(bAllDuration)
        {
            entry.type = Type::Duration;
            detail::GlobalAllocator::Allocate<Duration>(entry, componentCount);

            auto componentReader = makeComponentReader();
            for(uint32_t i = 0; i < componentCount; i++)
            {
                bool bValid = false;
                const Duration duration = Duration::FromText(componentReader.Next(), bValid);
                if(!bValid)
                {
                    Diagnose(DiagnosticSeverity::Error, DiagnosticType::InvalidDuration, tokenizer, valueToken, sink);
                    entry.ReleaseData();
                    return false;
                }
                entry.data.dur[i] = duration;
            }

            return postProcess();
        }


        const Type valueType = firstType;

        if(valueType == Type::Null)
        {
            entry.type = Type::Null;
            return postProcess();
        }
        return false;  // unhandled token
    }
    template<Type CONTAINER_TYPE, typename SINK>
    [[nodiscard]] constexpr bool ParseContainer(Tokenizer& tokenizer, Entry& container FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept
    {
        if(tokenizer.depth >= MAX_PARSE_DEPTH)
        {
            Diagnose(DiagnosticSeverity::Error, DiagnosticType::NestingTooDeep, tokenizer, tokenizer.Current(), sink);
            return false;
        }

        tokenizer.depth++;
        const bool bParsed = ParseContainerBody<CONTAINER_TYPE>(tokenizer, container FDF_COMMENT_SWITCH(, comment), sink);
        tokenizer.depth--;
        return bParsed;
    }

    template<Type CONTAINER_TYPE, typename SINK>
    [[nodiscard]] constexpr bool ParseContainerBody(Tokenizer& tokenizer, Entry& container FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept
    {
        static_assert(CONTAINER_TYPE == Type::Array || CONTAINER_TYPE == Type::Map);
        constexpr TokenType CLOSE_TOKEN = CONTAINER_TYPE == Type::Array? TokenType::SquareBraceClose : TokenType::CurlyBraceClose;
        assert(tokenizer.Current().type == (CONTAINER_TYPE == Type::Array? TokenType::SquareBraceOpen : TokenType::CurlyBraceOpen) && "Sanity check!");

        container.type = CONTAINER_TYPE;

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
                    if(childComment.type != TokenType::NonExisting)
                        Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken, sink);
                    childComment = currentToken;
                }
            #endif

                currentToken = tokenizer.Advance();
                FDF_CHECK_TOKEN(currentToken);
                FDF_CHECK_TOKEN_FOR_EOF(currentToken);
            }


            const bool bStartsChild = CONTAINER_TYPE == Type::Array
                ? IsValueLiteral(currentToken.type) || currentToken.type == TokenType::CurlyBraceOpen || currentToken.type == TokenType::SquareBraceOpen
                : currentToken.type == TokenType::Atom;

            if(bStartsChild)
            {
                const uint32_t childCountBefore = container.GetChildCount();
                if(!ParseVariable(tokenizer, container FDF_COMMENT_SWITCH(,childComment), sink))
                {
                    while(container.GetChildCount() > childCountBefore)
                        (void)container.RemoveChild(container.GetChildCount() - 1);

                    if(tokenizer.Current().type == TokenType::Invalid)
                    {
                        Diagnose(DiagnosticSeverity::Fatal, static_cast<DiagnosticType>(tokenizer.Current().extra8), tokenizer, tokenizer.Current(), sink);
                        return false;  // Lexer error: can't reliably resume
                    }
                    SkipToNextEntry(tokenizer, true);  // ParseVariable already reported the error
                    currentToken = tokenizer.Current();
                    continue;
                }
                currentToken = tokenizer.Current();
            }
            else if(currentToken.type == CLOSE_TOKEN)
            {
                currentToken = tokenizer.Advance();
                FDF_CHECK_TOKEN(currentToken);

                if(currentToken.type == TokenType::Comma)
                {
                    currentToken = tokenizer.Advance();
                    FDF_CHECK_TOKEN(currentToken);
                    FDF_CHECK_TOKEN_FOR_EOF(currentToken);
                }

                if(currentToken.type == TokenType::Comment)
                {
                #if !FDF_NO_COMMENTS
                    if(comment.type != TokenType::NonExisting)
                        Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken, sink);
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
                    container.GetComment() = tokenizer.ToView(comment);
            #endif

                return true;
            }
            else
            {
                if(currentToken.type == TokenType::EndOfFile)
                {
                    Diagnose(DiagnosticSeverity::Fatal, DiagnosticType::UnexpectedEndOfFile, tokenizer, currentToken, sink);
                    return false;
                }
                if(currentToken.type == TokenType::Invalid)
                {
                    Diagnose(DiagnosticSeverity::Fatal, static_cast<DiagnosticType>(currentToken.extra8), tokenizer, currentToken, sink);
                    return false;
                }
                Diagnose(DiagnosticSeverity::Error, DiagnosticType::UnexpectedToken, tokenizer, currentToken, sink);

                if(currentToken.type == TokenType::CurlyBraceClose || currentToken.type == TokenType::SquareBraceClose)
                {
                    currentToken = tokenizer.Advance();
                    FDF_CHECK_TOKEN(currentToken);
                    continue;
                }

                SkipToNextEntry(tokenizer, true);
                currentToken = tokenizer.Current();
            }
        }
    }

    template<typename SINK>
    [[nodiscard]] constexpr bool ParseArray(Tokenizer& tokenizer, Entry& array FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept
    {
        return ParseContainer<Type::Array>(tokenizer, array FDF_COMMENT_SWITCH(, comment), sink);
    }

    template<typename SINK>
    [[nodiscard]] constexpr bool ParseMap(Tokenizer& tokenizer, Entry& map FDF_COMMENT_SWITCH(, Token comment), SINK& sink) noexcept
    {
        return ParseContainer<Type::Map>(tokenizer, map FDF_COMMENT_SWITCH(, comment), sink);
    }










    template<bool CHECK_SINGLE_LINE>
    struct ScopePositions;
    template<>
    struct ScopePositions<true> { size_t begin = 0, end = 0, textBegin = 0, spaces = 0; };
    template<>
    struct ScopePositions<false>{ size_t begin = 0; };

}

FDF_EXPORT namespace fdf
{
    template<Style STYLE>
    constexpr String WriteBuffer(const Entry& root) noexcept
    {
        using namespace detail;

        String buffer;
        assert((root.GetIdentifierSize() != 0 || (root.type == Type::Map && !root.parent)) && "Unless it's a map, it must have an identifier");

        static constexpr bool CHECK_SINGLE_LINE = STYLE.singleLineContainerLimit > 5;
        detail::Vector<ScopePositions<CHECK_SINGLE_LINE>> scopes;
        uint32_t lastDepth = 0u;
        const bool bHasDedicatedRoot = !root.parent && root.type == Type::Map && root.GetIdentifierSize() == 0;

        buffer.clear();
        buffer.reserve(buffer.size() + (static_cast<size_t>(root.GetChildCount()) * 50));





        auto writeEntryNameFn = [&](const Entry& e) -> void  { buffer.append(e.GetIdentifier()); };
        auto addTabFn = [&buffer](const size_t count) -> void
        {
            if constexpr(STYLE.bUseSpacesOverTabs)
                buffer.append(count * STYLE.tabSize, ' ');
            else
                buffer.append(count, '\t');
        };





        auto writeQuotedStringFn = [&buffer](std::string_view view) -> void
        {
            // choose the quote that needs less escaping
            // always escape backslashes and control characters
            const bool bHasDouble = view.contains('\"');
            const bool bHasSingle = view.contains('\'');
            const char quote = (bHasDouble && !bHasSingle && !STYLE.bAlwaysUseDoubleQuoteForStrings)? '\'' : '\"';

            buffer.push_back(quote);
            for(char c : view)
            {
                switch(c)
                {
                    case '\\': buffer.append("\\\\"); break;
                    case '\n': buffer.append("\\n");  break;
                    case '\t': buffer.append("\\t");  break;
                    case '\r': buffer.append("\\r");  break;
                    case '\v': buffer.append("\\v");  break;
                    case '\b': buffer.append("\\b");  break;
                    case '\f': buffer.append("\\f");  break;
                    case '\a': buffer.append("\\a");  break;
                    default:
                        if(c == quote)
                            buffer.push_back('\\');
                        buffer.push_back(c);
                }
            }
            buffer.push_back(quote);
        };
        String valueBuffer;
        auto writeSimpleEntryValueFn = [&](const Entry& e) -> void
        {
            if(e.type == Type::String)
            {
                // a plain string is a pack of one, components are quoted separately and joined by '|'
                const std::span<const String> parts = e.GetValue<String>();
                for(uint32_t i = 0; i < parts.size(); i++)
                {
                    if(i)
                        buffer.push_back('|');
                    writeQuotedStringFn(parts[i]);
                }
                return;
            }

            valueBuffer.clear();
            buffer.append(e.DataToView<STYLE>(valueBuffer));
        };





        auto closeScopes = [&](const int64_t i, const bool bNested) -> void
        {
            if constexpr(CHECK_SINGLE_LINE)
            {
                for(size_t j = scopes.size() - 1; j != detail::SIZE_T_MAX_VALUE; j--)
                {
                    if(scopes[j].end == 0)
                    {
                        const bool bWasMap = buffer[scopes[j].begin] == '{';
                        addTabFn(lastDepth - 1 - static_cast<size_t>(i));
                        buffer.push_back(bWasMap? '}' : ']');
                        scopes[j].end = buffer.size() - 1;

                        if constexpr(STYLE.bCommas)
                        {
                            if(bNested)
                                buffer.push_back(',');
                        }
                        buffer.push_back('\n');

                        for(size_t pos = scopes[j].end; pos > scopes[j].begin + 2; pos--)
                        {
                            if(buffer[pos - 1] == '/' && (buffer[pos] == '/' || buffer[pos] == '*'))
                            {
                                scopes.erase(j);
                                return;
                            }
                            if(buffer[pos] == ' ' && (detail::constexpr_isspace(buffer[pos - 2]) || IsBrace(buffer[pos - 2])))
                                scopes[j].spaces++;
                            else if(buffer[pos] == '\t')
                                scopes[j].spaces += STYLE.tabSize;
                        }
                        if(scopes[j].end - scopes[j].textBegin + 1 - scopes[j].spaces > STYLE.singleLineContainerLimit)
                            scopes.erase(j);
                        return;
                    }
                }
            }
            else
            {
                const bool bWasMap = buffer[scopes.back().begin] == '{';
                addTabFn(static_cast<size_t>(lastDepth - 1 - i));
                buffer.push_back(bWasMap? '}' : ']');
                buffer.push_back('\n');
                scopes.pop_back();
            }
        };





        auto writeFn = [&](const Entry& e) -> void
        {
            const uint32_t depth = bHasDedicatedRoot? e.CalculateDepth() - 1 : e.CalculateDepth();
            for(int64_t i = 0; i < static_cast<int64_t>(lastDepth) - static_cast<int64_t>(depth); i++)
                closeScopes(i, depth > 0 || STYLE.bTopLevelCommas || i + 1 < static_cast<int64_t>(lastDepth) - static_cast<int64_t>(depth));





            if(e.IsContainer())
            {
                // no blank line before the first element of a nested container
                const size_t found = buffer.find_last_not_of("\n\t ");
                if(found == String::npos || !IsOpenBrace(buffer[found]))
                    buffer.push_back('\n');   
            }





        #if !FDF_NO_COMMENTS
            // short scalar comments are inline, others get a leading line
            const std::string_view entryComment = STYLE.bEntryComment? std::string_view(e.GetComment()) : std::string_view{};
            size_t commentSize = 0;
            detail::NormalizeComment(entryComment, [&](char) { commentSize++; });
            const bool bInlineComment = commentSize != 0 && !e.IsContainer() && commentSize <= STYLE.singleLineCommentLimit;
            if(commentSize != 0 && !bInlineComment)
            {
                String text;
                detail::NormalizeComment(entryComment, [&](char c) { text.push_back(c); });

                const size_t indent = static_cast<size_t>(depth) * STYLE.tabSize;
                const size_t available = STYLE.singleLineCommentLimit > indent + 3
                                       ? static_cast<size_t>(STYLE.singleLineCommentLimit) - indent - 3
                                       : 0;
                // a wrapped comment must stay one token, so it wraps as a block: several '//' lines
                // read back as several comments and only the last survives. Text holding "*/" or
                // ending in whitespace can't survive the block form, so it stays on one line
                const bool bWrap = available >= 5 && text.size() > available
                                && text.find("*/") == String::npos
                                && !detail::constexpr_isspace(text.back());

                addTabFn(depth);
                if(bWrap)
                {
                    // the body starts at the space after '/*', so it can never look like a '#' file comment
                    buffer.append("/* ");
                    size_t lineStart = 0;
                    while(text.size() - lineStart > available)
                    {
                        // re-parsing folds the newline and all whitespace after it into one space,
                        // so only a space followed by non-whitespace is a reversible break point
                        size_t breakPos = String::npos;
                        for(size_t probe = std::min(lineStart + available, text.size() - 2) + 1; probe > lineStart + 1; )
                        {
                            probe--;
                            if(text[probe] == ' ' && !detail::constexpr_isspace(text[probe + 1]))
                            {
                                breakPos = probe;
                                break;
                            }
                        }
                        if(breakPos == String::npos)
                            break;   // nothing reversible on this line, leave it overlong

                        buffer.append(text.substr(lineStart, breakPos - lineStart));
                        buffer.push_back('\n');
                        addTabFn(depth);
                        buffer.append("   ");
                        lineStart = breakPos + 1;
                    }
                    buffer.append(text.substr(lineStart));
                    buffer.append(" */");
                }
                else
                {
                    buffer.append("// ");
                    buffer.append(text);
                }
                buffer.push_back('\n');
            }
        #endif





            if(!e.IsContainer())
            {
                addTabFn(depth);
                if(!e.parent || e.parent->type != Type::Array)
                {
                    writeEntryNameFn(e);
                    if constexpr(STYLE.bSpaceBeforeAndAfterEqualSign)
                        buffer.append(" = ");
                    else
                        buffer.push_back('=');
                }
                writeSimpleEntryValueFn(e);

                if constexpr(STYLE.bCommas)
                {
                    if(STYLE.bTopLevelCommas || !bHasDedicatedRoot || e.parent != &root)
                        buffer.push_back(',');
                }
            #if !FDF_NO_COMMENTS
                if(bInlineComment)
                {
                    // '\x01' marks comment padding for the final alignment pass
                    buffer.push_back('\x01');
                    buffer.append("// ");
                    detail::NormalizeComment(entryComment, [&](char c) { buffer.push_back(c); });
                }
            #endif
                buffer.push_back('\n');
            }
            else
            {
                const bool bIsMap = e.type == Type::Map;
                addTabFn(depth);

                // empty containers emit inline without opening a deferred scope
                if(e.GetChildCount() == 0)
                {
                    if(!e.parent || e.parent->type != Type::Array)
                        writeEntryNameFn(e);
                    buffer.push_back(bIsMap? '{' : '[');
                    buffer.push_back(' ');
                    buffer.push_back(bIsMap? '}' : ']');
                    if constexpr(STYLE.bCommas)
                    {
                        if(STYLE.bTopLevelCommas || !bHasDedicatedRoot || e.parent != &root)
                            buffer.push_back(',');
                    }
                    buffer.push_back('\n');
                    lastDepth = depth;
                    return;
                }

                auto& scope = scopes.emplace_back();
                if constexpr(CHECK_SINGLE_LINE)
                    { scope.textBegin = buffer.size(); }

                if(!e.parent || e.parent->type != Type::Array)
                {
                    writeEntryNameFn(e);
                    if constexpr(STYLE.bParenthesesOnNewLine)
                    {
                        buffer.push_back('\n');
                        addTabFn(depth);
                    }
                }

                buffer.push_back(bIsMap? '{' : '[');
                scope.begin = buffer.size() - 1;
                buffer.push_back('\n');
            }

            lastDepth = depth;
        };





        #if !FDF_NO_COMMENTS
            if constexpr(STYLE.bFileComment)
            {
                if(bHasDedicatedRoot)
                {
                    // keep raw newlines, strip leading whitespace and blank lines, and escape "*/"
                    const std::string_view fileComment = root.GetComment();
                    if(fileComment.find_first_not_of(" \t\n\v\f\r") != std::string_view::npos)
                    {
                        buffer.append("/*#\n");
                        bool bPendingNewLine = false;
                        bool bLineStart = true;
                        for(char c : fileComment)
                        {
                            if(c == '\n' || c == '\r')
                            {
                                bPendingNewLine = true;
                                bLineStart = true;
                                continue;
                            }
                            if(bLineStart && detail::constexpr_isspace(c))
                                continue;
                            if(bPendingNewLine)
                            {
                                buffer.push_back('\n');
                                bPendingNewLine = false;
                            }
                            if(bLineStart)
                            {
                                addTabFn(1);
                                bLineStart = false;
                            }
                            if(c == '/' && buffer.back() == '*')
                                buffer.push_back(' ');
                            buffer.push_back(c);
                        }
                        buffer.append("\n*/\n\n\n");
                    }
                }
            }
        #endif





        if constexpr(STYLE.bGroupSimilarTypes)
        {
            if(bHasDedicatedRoot)
                root.ForEach<ForEachFlags::Recursive | ForEachFlags::Group>(writeFn);
            else
                root.ForEach<ForEachFlags::Recursive | ForEachFlags::Group | ForEachFlags::IncludeSelf>(writeFn);
        }
        else
        {
            if(bHasDedicatedRoot)
                root.ForEach<ForEachFlags::Recursive>(writeFn);
            else
                root.ForEach<ForEachFlags::Recursive | ForEachFlags::IncludeSelf>(writeFn);
        }


        for(int64_t i = 0; i < static_cast<int64_t>(lastDepth); i++)
            closeScopes(i, STYLE.bTopLevelCommas || i + 1 < static_cast<int64_t>(lastDepth));





        if constexpr(CHECK_SINGLE_LINE)
        {
            for(size_t i = scopes.size() - 1; i != detail::SIZE_T_MAX_VALUE; i--)
            {
                size_t found = detail::SIZE_T_MAX_VALUE;
                for(size_t j = i - 1; j != detail::SIZE_T_MAX_VALUE; j--)
                {
                    if(scopes[j].begin < scopes[i].begin && scopes[j].end > scopes[i].end)
                    {
                        if(scopes[j].end - scopes[j].begin + 1 - scopes[j].spaces > STYLE.singleLineContainerLimit)
                            break;
                        found = j;
                    }
                }

                if(found != detail::SIZE_T_MAX_VALUE)
                    i = found;


                // collapse blank lines
                size_t newLinePos = detail::SIZE_T_MAX_VALUE;
                for(found = scopes[i].end + 1; found < buffer.size(); found++)
                {
                    if(buffer[found] == '\n')
                    {
                        newLinePos = found;
                        continue;
                    }
                    if(buffer[found] == '{')
                    {
                        const size_t found2 = buffer.find_first_of("}\n", found);
                        if(found2 != String::npos && buffer[found2] != '\n')
                            buffer.erase(newLinePos, 1);
                        break;
                    }
                    if(buffer[found] == '[')
                    {
                        const size_t found2 = buffer.find_first_of("]\n", found);
                        if(found2 != String::npos && buffer[found2] != '\n')
                            buffer.erase(newLinePos, 1);
                        break;
                    }
                    if(detail::constexpr_isalpha(buffer[found]))
                    {
                        while(found < buffer.size() && (detail::constexpr_isalpha(buffer[found]) || detail::constexpr_isdigit(buffer[found]) || buffer[found] == '_' || buffer[found] == ' '))
                            found++;
                        if(!IsOpenBrace(buffer[found]))
                            break;
                        found--;
                        continue;
                    }
                    if(!detail::constexpr_isspace(buffer[found]))
                        break;
                }


                for(size_t pos = scopes[i].end; pos > scopes[i].begin; pos--)
                {
                    if(buffer[pos] == '\n')
                    {
                        buffer[pos] = ' ';
                        bool bIncremented = false;
                        if constexpr(!STYLE.bCommas)
                        {
                            if(buffer[pos - 1] != '{' && buffer[pos - 1] != '[')
                            {
                                found = buffer.find_first_not_of(' ', pos);
                                // Skip an open brace too: the token before it is the container's name
                                // (or a prior array element), separated fine by a space, never a comma
                                if(found != String::npos && !IsCloseBrace(buffer[found]) && !IsOpenBrace(buffer[found]))
                                {
                                    buffer[pos] = ',';
                                    buffer.insert(++pos, 1, ' ');
                                    bIncremented = true;
                                }
                            }
                        }

                        while(pos + 1 < scopes[i].end && (buffer[pos + 1] == ' ' || buffer[pos + 1] == '\t'))
                            buffer.erase(pos + 1, 1);
                        if(bIncremented)
                            --pos;

                        // collapse consecutive newlines
                        while(pos - 1 > scopes[i].begin && buffer[pos - 1] == '\n')
                            buffer.erase(--pos, 1);

                        // separate nested single-line containers with spaces
                        if(pos - 1 >= scopes[i].begin)
                        {
                            if((IsOpenBrace(buffer[pos - 1]) && IsOpenBrace(buffer[pos + 1])) || (IsCloseBrace(buffer[pos - 1]) && IsCloseBrace(buffer[pos + 1])))
                                buffer.insert(pos, 2, ' ');
                        }
                    }
                }

                for(size_t pos = scopes[i].begin; pos > scopes[i].textBegin; pos--)
                {
                    if(buffer[pos] == '\n')
                        buffer[pos] = ' ';
                    if(buffer[pos] == ' ' || buffer[pos] == '\t')
                        buffer.erase(pos, 1);
                }
            }
        }


    #if !FDF_NO_COMMENTS
        // resolve inline-comment padding markers
        // aligned comments share a column, otherwise each marker becomes one space
        {
            constexpr char MARKER = '\x01';
            const size_t fileCommentEnd = buffer.starts_with("/*#")? buffer.find("*/") : String::npos;
            auto isPaddingMarker = [&](size_t marker) -> bool
            {
                if(marker >= buffer.size() || buffer[marker] != MARKER
                   || marker + 3 >= buffer.size() || buffer.compare(marker + 1, 3, "// ") != 0
                   || (fileCommentEnd != String::npos && marker < fileCommentEnd))
                    return false;

                const size_t previousNewLine = buffer.rfind('\n', marker);
                const size_t lineStart = previousNewLine == String::npos? 0 : previousNewLine + 1;
                char quote = '\0';
                bool bEscaped = false;
                for(size_t i = lineStart; i < marker; i++)
                {
                    const char c = buffer[i];
                    if(quote != '\0')
                    {
                        if(bEscaped)
                            bEscaped = false;
                        else if(c == '\\')
                            bEscaped = true;
                        else if(c == quote)
                            quote = '\0';
                    }
                    else if(c == '\"' || c == '\'')
                        quote = c;
                    else if(c == '/' && i + 1 < marker && buffer[i + 1] == '/')
                        return false;
                }
                return quote == '\0';
            };
            auto findMarker = [&](size_t start) -> size_t
            {
                for(size_t marker = buffer.find(MARKER, start); marker != String::npos; marker = buffer.find(MARKER, marker + 1))
                    if(isPaddingMarker(marker))
                        return marker;
                return String::npos;
            };
            auto findPreviousMarker = [&](size_t start, size_t lowerBound) -> size_t
            {
                size_t marker = buffer.rfind(MARKER, start);
                while(marker != String::npos && marker >= lowerBound)
                {
                    if(isPaddingMarker(marker))
                        return marker;
                    if(marker == 0)
                        break;
                    marker = buffer.rfind(MARKER, marker - 1);
                }
                return String::npos;
            };

            if constexpr(!STYLE.bAlignCloseComments)
            {
                for(size_t marker = findMarker(0); marker != String::npos; marker = findMarker(marker + 1))
                    buffer[marker] = ' ';
            }
            else
            {
                auto columnOf = [&](size_t marker) -> size_t
                {
                    const size_t lineStart = buffer.rfind('\n', marker);
                    return lineStart == String::npos? marker : marker - lineStart - 1;
                };

                size_t groupStart = findMarker(0);
                while(groupStart != String::npos)
                {
                    size_t groupEnd = groupStart;
                    size_t maxColumn = columnOf(groupStart);
                    while(true)
                    {
                        const size_t next = findMarker(groupEnd + 1);
                        if(next == String::npos)
                            break;
                        const size_t firstNewLine = buffer.find('\n', groupEnd + 1);
                        if(firstNewLine == String::npos || firstNewLine >= next)
                            break;
                        const size_t secondNewLine = buffer.find('\n', firstNewLine + 1);
                        if(secondNewLine != String::npos && secondNewLine < next)
                            break;
                        groupEnd = next;
                        maxColumn = std::max(maxColumn, columnOf(groupEnd));
                    }

                    size_t marker = groupEnd;
                    while(true)
                    {
                        buffer.replace(marker, 1, maxColumn - columnOf(marker) + 1, ' ');
                        if(marker == groupStart)
                            break;
                        marker = findPreviousMarker(marker - 1, groupStart);
                        assert(marker != String::npos && marker >= groupStart);
                    }
                    groupStart = findMarker(groupStart + 1);
                }
            }
        }
    #endif
        return buffer;
    }
}

namespace fdf::detail
{



}










template<>
struct std::formatter<fdf::String> : std::formatter<std::string_view>
{
    template<typename FormatContext>
    constexpr auto format(const fdf::String& value, FormatContext& ctx) const
    {
        return std::formatter<std::string_view>::format(std::string_view(value), ctx);
    }
};




#undef FDF_EXPORT
#undef FDF_EXPORT_INTERNAL
#undef FDF_CHECK_TOKEN
#undef FDF_CHECK_TOKEN_FOR_EOF
#undef FDF_FORWARD_ERROR
#undef FDF_COMMENT_SWITCH
