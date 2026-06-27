
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
    #include <bit>
    #include <cassert>
    #include <cctype>
    #include <charconv>
    #include <cstdint>
    #include <filesystem>
    #include <limits>
    #include <format>
    #include <fstream>
    #include <ranges>
    #include <span>
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
        Map,
        Array,
        Null,
        Nil = Null,

        Bool,
        Int,
        UInt,
        Float,

        String,
        Hex,
        Version,
        Timestamp
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
        InvalidIdentifier,
        UnexpectedEndOfFile,
        UnterminatedString,
        UnterminatedComment,
        InvalidComment,
        InvalidNumber,
        InvalidTimestamp,
        InvalidToken,        // generic lexer failure with no more specific reason
    };

    // Passed to a DIAGNOSTIC_CALLBACK for every issue found while parsing
    struct Diagnostic
    {
        DiagnosticSeverity severity = DiagnosticSeverity::None;
        DiagnosticType     type     = DiagnosticType::UnexpectedToken;
        std::string_view   message;     // offending text, or a short description
        uint32_t           line   = 0;  // 1-based line number
        uint32_t           column = 0;  // column within the line
        uint32_t           offset = 0;  // byte offset into the source buffer
    };

    class Entry;
}





namespace fdf::detail
{
    template<auto DIAGNOSTIC_CALLBACK = nullptr>
    struct Utils;
    
    template<typename T>
    concept IsValidIDType = std::integral<std::remove_cvref_t<T>> || std::convertible_to<std::remove_cvref_t<T>, std::string_view>;
    
    template<typename Callable>
    constexpr bool IsValidDiagnosticCallback = std::is_invocable_v<Callable, const Diagnostic&>;

    inline constexpr size_t MAX_IDENTIFIER_LENGTH = FDF_NO_COMMENTS && FDF_EXTENDED_NO_COMMENT_IDENTIFIERS? 38 : 30;
    
    struct EntryDeleter { static constexpr void operator()(Entry* e) noexcept; };

    inline constexpr auto SIZE_T_MAX_VALUE = std::numeric_limits<  size_t>::max();
    inline constexpr auto INT64_MAX_VALUE  = std::numeric_limits< int64_t>::max();
    inline constexpr auto DOUBLE_MAX_VALUE = std::numeric_limits<  double>::max();

    inline constexpr auto UINT8_MAX_VALUE  = std::numeric_limits< uint8_t>::max();
    inline constexpr auto UINT16_MAX_VALUE = std::numeric_limits<uint16_t>::max();
    inline constexpr auto UINT32_MAX_VALUE = std::numeric_limits<uint32_t>::max();
    inline constexpr auto UINT64_MAX_VALUE = std::numeric_limits<uint64_t>::max();
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
    struct VersionType { consteval VersionType() noexcept = default; };


    // Decoded view of an ISO-8601 timestamp. Transient: a Timestamp entry stores the raw text
    // (lossless round-trip), and GetValue<Timestamp>() parses that text into this struct on demand
    // Ordinal (YYYY-DDD) and ISO week (YYYY-Www-D) dates are normalized to calendar year/month/day,
    // with dateKind remembering the original spelling. Sub-second precision is capped at nanoseconds
    struct Timestamp
    {
        enum class TzKind   : uint8_t { None, Utc, Offset };
        enum class DateKind : uint8_t { Calendar, Ordinal, Week };

        uint16_t year        = 0;   // 0-9999
        uint8_t  month       = 0;   // 1-12
        uint8_t  day         = 0;   // 1-31
        uint8_t  hour        = 0;   // 0-23
        uint8_t  minute      = 0;   // 0-59
        uint8_t  second      = 0;   // 0-60
        uint8_t  fracDigits  = 0;   // sub-second digits as written, 0-9
        uint32_t nanosecond  = 0;   // 0-999'999'999
        int16_t  tzOffsetMin = 0;   // signed minutes from UTC (Offset kind only)
        TzKind   tzKind      = TzKind::None;
        DateKind dateKind    = DateKind::Calendar;
        bool     bHasDate    = false;
        bool     bHasTime    = false;
        bool     bValid      = false;

        [[nodiscard]] constexpr bool IsValid() const noexcept  { return bValid; }

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
        [[nodiscard]] constexpr int64_t ToUnixNanos()  const noexcept  { return ToUnixSeconds() * 1'000'000'000LL + nanosecond; }

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
            const int64_t s = FloorDiv(ns, 1'000'000'000);
            Timestamp t = FromUnixSeconds(s);
            t.nanosecond = static_cast<uint32_t>(ns - s * 1'000'000'000);
            t.fracDigits = 9;
            return t;
        }

        // --- Parse / serialize -----------------------------------------------------------------
        [[nodiscard]] static constexpr Timestamp FromText(std::string_view ts) noexcept;
        constexpr void AppendTo(std::string& out) const noexcept;

    private:
        [[nodiscard]] static constexpr bool IsLeap(int y) noexcept  { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
        [[nodiscard]] static constexpr int64_t FloorDiv(int64_t a, int64_t b) noexcept  { return a >= 0? a / b : -((-a + b - 1) / b); }

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
        static constexpr void AppendPadded(std::string& out, uint32_t v, uint8_t width) noexcept
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


    // Parse + validate in one pass (the single source of truth; IsValidTimestamp wraps this)
    // Returns a Timestamp with bValid == false on any structural or range error
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

        if(ts.size() >= 5 && digitsAt(0, 4) && ts[4] == '-')
        {
            t.year = static_cast<uint16_t>(num(0, 4));
            t.bHasDate = true;
            pos = 5;

            if(pos < ts.size() && ts[pos] == 'W')
            {
                if(!(digitsAt(pos + 1, 2) && pos + 3 < ts.size() && ts[pos + 3] == '-' &&
                     digitsAt(pos + 4, 1) && pos + 5 == ts.size()))
                    return t;
                const int week    = num(pos + 1, 2);
                const int weekday  = num(pos + 4, 1);
                if(week < 1 || week > 53 || weekday < 1 || weekday > 7)
                    return t;

                // ISO week date -> the Monday of week 1 is the Monday on/before Jan 4
                const int64_t jan4 = DaysFromCivil(t.year, 1, 4);
                const int     jan4Mon0 = static_cast<int>(((jan4 % 7) + 7 + 3) % 7);
                CivilFromDays(jan4 - jan4Mon0 + int64_t(week - 1) * 7 + (weekday - 1), t.year, t.month, t.day);
                t.dateKind = DateKind::Week;
                t.bValid = true;
                return t;
            }

            if(!digitsAt(pos, 2))
                return t;

            if(pos + 2 < ts.size() && ts[pos + 2] == '-')
            {
                if(!digitsAt(pos + 3, 2))
                    return t;
                const int month = num(pos, 2);
                const int day   = num(pos + 3, 2);
                constexpr int DAYS_IN_MONTH[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
                if(month < 1 || month > 12)
                    return t;
                const int maxDay = (month == 2 && IsLeap(t.year))? 29 : DAYS_IN_MONTH[month - 1];
                if(day < 1 || day > maxDay)
                    return t;
                t.month = static_cast<uint8_t>(month);
                t.day   = static_cast<uint8_t>(day);
                pos += 5;
            }
            else if(pos + 2 < ts.size() && digit(ts[pos + 2]))
            {
                const int ordinal = num(pos, 3);
                if(ordinal < 1 || ordinal > (IsLeap(t.year)? 366 : 365))
                    return t;
                CivilFromDays(DaysFromCivil(t.year, 1, 1) + (ordinal - 1), t.year, t.month, t.day);
                t.dateKind = DateKind::Ordinal;
                pos += 3;
            }
            else
                return t;

            if(pos == ts.size()) { t.bValid = true; return t; }
            if(ts[pos] != 'T') return t;
            pos++;
        }
        else if(!digitsAt(0, 2))
            return t;

        // Time part: HH:MM:SS
        if(!digitsAt(pos, 2) || pos + 2 >= ts.size() || ts[pos + 2] != ':' ||
           !digitsAt(pos + 3, 2) || pos + 5 >= ts.size() || ts[pos + 5] != ':' ||
           !digitsAt(pos + 6, 2))
            return t;
        if(num(pos, 2) > 23 || num(pos + 3, 2) > 59 || num(pos + 6, 2) > 59)
            return t;
        t.hour    = static_cast<uint8_t>(num(pos, 2));
        t.minute  = static_cast<uint8_t>(num(pos + 3, 2));
        t.second  = static_cast<uint8_t>(num(pos + 6, 2));
        t.bHasTime = true;
        pos += 8;

        if(pos == ts.size()) { t.bValid = true; return t; }

        // Optional fractional seconds (kept to nanosecond precision; extra digits are validated but dropped)
        if(ts[pos] == '.')
        {
            pos++;
            if(pos >= ts.size() || !digit(ts[pos]))
                return t;
            uint64_t frac = 0;
            while(pos < ts.size() && digit(ts[pos]))
            {
                if(t.fracDigits < 9)
                {
                    frac = frac * 10 + static_cast<uint64_t>(ts[pos] - '0');
                    t.fracDigits++;
                }
                pos++;
            }
            for(uint8_t i = t.fracDigits; i < 9; i++)
                frac *= 10;
            t.nanosecond = static_cast<uint32_t>(frac);
            if(pos == ts.size()) { t.bValid = true; return t; }
        }

        // Timezone
        if(ts[pos] == 'Z')
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
            t.tzKind = TzKind::Offset;
            t.tzOffsetMin = static_cast<int16_t>((bNeg? -1 : 1) * (offHour * 60 + offMin));
            t.bValid = true;
            return t;
        }

        return t;
    }

    // Serialize to canonical ISO-8601 calendar text (used when injecting a Timestamp value)
    // Ordinal/week origins are emitted as the equivalent calendar date
    constexpr void Timestamp::AppendTo(std::string& out) const noexcept
    {
        if(bHasDate)
        {
            AppendPadded(out, year, 4);
            out.push_back('-');
            AppendPadded(out, month, 2);
            out.push_back('-');
            AppendPadded(out, day, 2);
            if(bHasTime)
                out.push_back('T');
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
            out.push_back('Z');
        else if(tzKind == TzKind::Offset)
        {
            out.push_back(tzOffsetMin < 0? '-' : '+');
            const uint32_t abs = static_cast<uint32_t>(tzOffsetMin < 0? -tzOffsetMin : tzOffsetMin);
            AppendPadded(out, abs / 60, 2);
            out.push_back(':');
            AppendPadded(out, abs % 60, 2);
        }
    }



    using UniqueEntryPtr = std::unique_ptr<Entry, detail::EntryDeleter>;
    class Entry
    {
    public:
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        
        constexpr Entry(Entry&& other) noexcept;
        constexpr Entry& operator=(Entry&& other) noexcept;

        // Public so constexpr `new Entry{}` works on MSVC (no friend access to a private ctor in
        // constant eval). Dtor stays private, so construct via NewEntry/Emplace
        constexpr Entry() noexcept;

    private:
        constexpr ~Entry() noexcept;

        friend constexpr UniqueEntryPtr NewEntry() noexcept;

        template<auto DIAGNOSTIC_CALLBACK>
        friend struct detail::Utils;

        struct CommentControlBlock { uint32_t capacity, size; };

    private:
        char identifier[detail::MAX_IDENTIFIER_LENGTH + 1] = {};
        Type type = Type::Map;
        uint32_t size = 0;
        uint32_t capacity = 0;  // container child-buffer capacity (containers only)
        Entry* parent = nullptr;

        // Union of typed pointers, not a bare void*: constexpr can't cast void*->T* (MSVC). Consteval
        // reads the member matching `type`; runtime uses `raw` (all members alias). Active member == type
        // TODO: collapse to `void* data` once MSVC allows void*->T* in constant expressions
        union DataPtr
        {
            void* raw = nullptr;
            bool* boolArray;
            std::string* str;
            std::vector<int64_t>* vInt;
            std::vector<uint64_t>* vUInt;
            std::vector<double>* vFloat;
            std::vector<Entry*>* vEntry;
        };
        DataPtr data;
    #if !FDF_NO_COMMENTS
        union CommentPtr
        {
            std::string* str = nullptr;   // default-active member: at consteval comment is always a std::string
            void* raw;
            CommentControlBlock* block;
        };
        CommentPtr comment;
    #endif

    private:
        template<typename T>
        [[nodiscard]] constexpr std::vector<T>*& GetDataVector() noexcept
        {
            if constexpr(std::is_same_v<T, int64_t>)       return data.vInt;
            else if constexpr(std::is_same_v<T, uint64_t>) return data.vUInt;
            else if constexpr(std::is_same_v<T, double>)   return data.vFloat;
            else                                           return data.vEntry;   // Entry*
        }
        template<typename T>
        [[nodiscard]] constexpr const std::vector<T>* GetDataVector() const noexcept
        {
            if constexpr(std::is_same_v<T, int64_t>)       return data.vInt;
            else if constexpr(std::is_same_v<T, uint64_t>) return data.vUInt;
            else if constexpr(std::is_same_v<T, double>)   return data.vFloat;
            else                                           return data.vEntry;
        }

        // Consteval-only typed pointers (std::string / bool[]). Runtime raw-byte access casts
        // data.raw directly at the call site
        template<typename T>
        [[nodiscard]] constexpr T*& GetDataAs() noexcept
        {
            if constexpr(std::is_same_v<T, bool>) return data.boolArray;
            else                                  return data.str;   // std::string
        }
        template<typename T>
        [[nodiscard]] constexpr const T* GetDataAs() const noexcept
        {
            if constexpr(std::is_same_v<T, bool>) return data.boolArray;
            else                                  return data.str;
        }

        // Null-check the data union through the member matching `type` (the active one at constexpr)
        [[nodiscard]] constexpr bool IsDataNull() const noexcept
        {
            switch(type)
            {
                case Type::Bool:                                          return !data.boolArray;
                case Type::Int:                                           return !data.vInt;
                case Type::UInt:   case Type::Version:                    return !data.vUInt;
                case Type::Float:                                         return !data.vFloat;
                case Type::String: case Type::Hex: case Type::Timestamp:  return !data.str;
                case Type::Array:  case Type::Map:                        return !data.vEntry;
                default:                                                  return true;   // Null / Nil hold no data
            }
        }

        
    #if !FDF_NO_COMMENTS
        [[nodiscard]] constexpr       CommentControlBlock* GetCommentControlBlock()       noexcept  { return comment.block; }
        [[nodiscard]] constexpr const CommentControlBlock* GetCommentControlBlock() const noexcept  { return comment.block; }
        [[nodiscard]] constexpr       char*                GetCommentData()               noexcept  { return static_cast<      char*>(comment.raw) + sizeof(CommentControlBlock); }
        [[nodiscard]] constexpr const char*                GetCommentData()         const noexcept  { return static_cast<const char*>(comment.raw) + sizeof(CommentControlBlock); }

        [[nodiscard]] constexpr       std::string* GetCommentString()       noexcept  { return comment.str; }
        [[nodiscard]] constexpr const std::string* GetCommentString() const noexcept  { return comment.str; }
    #endif

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
        [[nodiscard]] constexpr std::string GetFullIdentifier() const noexcept
        {
            const Entry* prev = this;
            const Entry* cur = parent;
            std::string temp = std::string(GetIdentifier());
            while(cur)
            {
                if(cur->GetIdentifierSize() == 0)
                {
                    if(cur->parent)
                    {
                        if(cur->parent->type == Type::Array)
                        {
                            const uint32_t index = cur->FindChildIndex(*prev);
                            assert(index != detail::UINT32_MAX_VALUE && "Index must be valid!");
                            temp = std::format("{}.{}", index, temp);
                        }
                        else
                        {
                            assert(false && "If it has a parent and no identifier, it must be a Type::Array element!");
                            return temp;
                        }
                    }
                    else
                    {
                        assert(cur->type == Type::Map && "If it has no identifier and no parent, it must be a Type::Map! (Specifically the root map)");
                        return temp;
                    }
                }
                else
                {
                    temp = std::format("{}.{}", cur->GetIdentifier(), temp);
                }
                
                prev = cur;
                cur = cur->parent;
            }
            return temp;
        }

        [[nodiscard]] constexpr std::string_view GetComment() const noexcept
        {
            #if !FDF_NO_COMMENTS
                if consteval
                {
                    if(comment.str)
                        return *GetCommentString();
                }
                else
                {
                    if(comment.raw)
                        return {GetCommentData(), GetCommentControlBlock()->size};
                }
            #endif
                return {};
        }
        
    private:
        constexpr void SetIdentifier_INTERNAL(std::string_view newIdentifier) noexcept;

    public:
        [[nodiscard]] constexpr bool SetIdentifier(std::string_view newIdentifier) noexcept;
        constexpr void SetComment(std::string_view newComment) noexcept;
        constexpr void ReleaseData() noexcept;
        constexpr void ReleaseComment() noexcept;
        constexpr void ReleaseEverything() noexcept;

    public:
        [[nodiscard]] constexpr uint32_t FindChildIndex(const Entry& e) const noexcept;
        [[nodiscard]] constexpr uint32_t FindChildIndex(std::string_view _identifier) const noexcept;
        [[nodiscard]] constexpr Entry* Emplace(std::string_view _identifier) noexcept;
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
        constexpr void SetValue(char value) noexcept;
        constexpr void SetValue(const char* value) noexcept;
        constexpr void SetValue(const Timestamp& value) noexcept;
        constexpr void SetValue(auto* value) = delete; // no pointer types (except char*)
        
        constexpr void SetValue(std::span<bool> value) noexcept;
        template<std::signed_integral T>
        constexpr void SetValue(std::span<T> value) noexcept;
        template<std::unsigned_integral T>
        constexpr void SetValue(std::span<T> value) noexcept;
        template<std::unsigned_integral T>
        constexpr void SetValue(std::span<T> value, VersionType) noexcept;
        template<std::floating_point T>
        constexpr void SetValue(std::span<T> value) noexcept;

        template<Style STYLE = {}>
        [[nodiscard]] constexpr std::string_view DataToView(std::string& temp) const noexcept;

    public:
        template<auto DIAGNOSTIC_CALLBACK = nullptr>
        [[nodiscard]] bool ParseCombineFile(const std::filesystem::path& filepath, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseNewIfExistingIsEmpty) noexcept;
        template<auto DIAGNOSTIC_CALLBACK = nullptr>
        [[nodiscard]] constexpr bool ParseCombineBuffer(std::string_view content, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseNewIfExistingIsEmpty) noexcept;
        [[nodiscard]] constexpr bool Combine(UniqueEntryPtr& other, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseNewIfExistingIsEmpty) noexcept;
    };
    
    
    
    
    
    template<auto DIAGNOSTIC_CALLBACK = nullptr>
    [[nodiscard]] UniqueEntryPtr ParseFile(const std::filesystem::path& filepath) noexcept;
    template<auto DIAGNOSTIC_CALLBACK = nullptr>
    [[nodiscard]] constexpr UniqueEntryPtr ParseBuffer(std::string_view content) noexcept;
        
    template<Style STYLE = {}>
    [[nodiscard]] bool WriteFile(const Entry& e, const std::filesystem::path& filepath, bool bCreateIfNotExists = true) noexcept;
    template<Style STYLE = {}>
    constexpr void WriteBuffer(const Entry& root, std::string& buffer) noexcept;
    
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


    inline constexpr auto KEYWORDS = std::to_array<std::string_view>(
    {
        "null", "nil",
        "true", "false", " MD_BOOL_PLACEHOLDER "
    });

    FDF_EXPORT_INTERNAL enum class TokenType : uint8_t
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
        ValueLiteral_End = TimestampLiteral,
    };

    FDF_EXPORT_INTERNAL struct Token
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

    FDF_EXPORT_INTERNAL struct Tokenizer
    {
        explicit constexpr Tokenizer(std::string_view content_) noexcept
            : content(content_), index(0), line(1), lastNewLineIndex(0), currentToken(GetNextToken())  { }

        [[nodiscard]] constexpr Token Current() const noexcept  { return currentToken; }
        [[nodiscard]] constexpr Token Advance()       noexcept  { currentToken = GetNextToken(); return currentToken; }

        [[nodiscard]] constexpr std::string_view ToView(Token token) const noexcept { return content.substr(token.startPosition, token.count); }

    private:
        [[nodiscard]] constexpr Token GetNextToken() noexcept;

        // Build a token at the current position, set its line/column, and advance past it
        [[nodiscard]] constexpr Token MakeToken(TokenType type, uint32_t count) noexcept
        {
            Token token = Token(type, index, count);
            token.line = line;
            token.column = static_cast<uint16_t>(index - lastNewLineIndex);
            index += count;
            return token;
        }

        // Build an Invalid token tagged with the reason and the current position so the parser
        // can forward a precise diagnostic. The reason is stashed in extra8
        [[nodiscard]] constexpr Token MakeInvalid(DiagnosticType reason) const noexcept
        {
            Token token = Token(TokenType::Invalid, index);
            token.line = line;
            token.column = static_cast<uint16_t>(index - lastNewLineIndex);
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
    constexpr void AppendInt(std::string& s, int64_t v) noexcept
    {
        char b[24];
        const auto r = std::to_chars(b, b + sizeof(b), v);
        s.append(b, r.ptr);
    }
    constexpr void AppendUInt(std::string& s, uint64_t v) noexcept
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
    // 'e' so the tokenizer classifies the result as a FloatLiteral, never an int
    FDF_EXPORT_INTERNAL constexpr void AppendDouble(std::string& s, double v) noexcept
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

    // Parse one number from [s, end). Accepts: [-] digits [. digits] [(e|E)[+|-]digits].
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

        for(; p < end && (*p >= '0' && *p <= '9'); p++)
            pushDigit(*p - '0');

        if(p < end && *p == '.')
        {
            p++;
            for(; p < end && (*p >= '0' && *p <= '9'); p++)
            {
                pushDigit(*p - '0');
                if(sigDigits <= 40)
                    exp10--;
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
            if(p >= end || !(*p >= '0' && *p <= '9'))
            {
                *bOk = false;
                return 0.0;
            }
            int ev = 0;
            for(; p < end && (*p >= '0' && *p <= '9'); p++)
                ev = ev < 100000? ev * 10 + (*p - '0') : ev;
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

    // Validates ISO-8601 timestamps (structure AND value ranges). Defers to Timestamp::FromText, the
    // single parse/validate implementation. Accepts: YYYY-MM-DD, YYYY-DDD (ordinal), YYYY-Www-D
    // (week), an optional Thh:mm:ss after a date, a bare hh:mm:ss, fractional seconds, Z / +-hh:mm
    FDF_EXPORT_INTERNAL [[nodiscard]] constexpr bool IsValidTimestamp(std::string_view ts) noexcept
    {
        return Timestamp::FromText(ts).IsValid();
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
        
        inline static constinit SlabAllocator<8U>  B8;
        inline static constinit SlabAllocator<16U> B16;
        inline static constinit SlabAllocator<32U> B32;
        inline static constinit SlabAllocator<64U> B64;
        inline static constinit SlabAllocator<sizeof(Entry), alignof(Entry)>  ENTRY_ALLOCATOR;

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
            if(size <= 8U)
                return B8.Deallocate(p);
            if(size <= 16U)
                return B16.Deallocate(p);
            if(size <= 32U)
                return B32.Deallocate(p);
            if(size <= 64U)
                return B64.Deallocate(p);
            
            ::operator delete(p);
            return true;
        }




        template<size_t size>
        static void* Allocate() noexcept
        {
            if constexpr(size <= 8U)
                return B8.Allocate();
            else if constexpr(size <= 16U)
                return B16.Allocate();
            else if constexpr(size <= 32U)
                return B32.Allocate();
            else if constexpr(size <= 64U)
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
            if constexpr(size <= 8U)
                return B8.Deallocate(p);
            else if constexpr(size <= 16U)
                return B16.Deallocate(p);
            else if constexpr(size <= 32U)
                return B32.Deallocate(p);
            else if constexpr(size <= 64U)
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
            if constexpr(sizeof(T) <= 8U)
                return B8.Create<T>(std::forward<Args>(args)...);
            else if constexpr(sizeof(T) <= 16U)
                return B16.Create<T>(std::forward<Args>(args)...);
            else if constexpr(sizeof(T) <= 32U)
                return B32.Create<T>(std::forward<Args>(args)...);
            else if constexpr(sizeof(T) <= 64U)
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
            if constexpr(sizeof(T) <= 8U)
                return B8.Destroy(obj);
            else if constexpr(sizeof(T) <= 16U)
                return B16.Destroy(obj);
            else if constexpr(sizeof(T) <= 32U)
                return B32.Destroy(obj);
            else if constexpr(sizeof(T) <= 64U)
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
            return MakeToken(TokenType::EndOfFile, 0);

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

            index++;
            if(index >= content.size())
                return MakeToken(TokenType::EndOfFile, 0);
        }



        if(content[index] == '\"' || content[index] == '\'')
        {
            size_t nextQuote = content.find_first_of(content[index], index + 1);
            if(nextQuote == std::string_view::npos)
                return MakeInvalid(DiagnosticType::UnterminatedString);  // Non matching quotes

            while(content[nextQuote - 1] == '\\' && content[nextQuote - 2] != '\\')
            {
                nextQuote = content.find_first_of(content[index], nextQuote + 1);
                if(nextQuote == std::string_view::npos)
                    return MakeInvalid(DiagnosticType::UnterminatedString);  // Non matching quotes
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
                return MakeInvalid(DiagnosticType::InvalidComment); // not enough space for a comment

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
                index = detail::UINT32_MAX_VALUE;
                return token;
            }

            if(content[index + 1] == '*') // multi line comment
            {
                size_t slashPos = content.find_first_of('/', index + 2);
                while(true)
                {
                    if(slashPos == std::string_view::npos)
                        return MakeInvalid(DiagnosticType::UnterminatedComment); // Non-matching comment scope (There is only "/*" and not "*/")

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

                        if(token.count == detail::UINT32_MAX_VALUE)
                            token.count = 0;
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



        if(constexpr_isalpha(content[index]) || content[index] == '_') // identifier, keyword
        {
            Token token = Token(TokenType::Identifier, index);
            token.line = line;
            token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
            auto checkKeywords = [&](std::string_view view) -> void
            {
                for(size_t i = 0; i < KEYWORDS.size(); i++)
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
            checkKeywords(ToView(token));
            index = firstNonAlpha >= content.size()? detail::UINT32_MAX_VALUE : static_cast<uint32_t>(firstNonAlpha);
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

                if(firstNonHex == std::string_view::npos) // eof before any separator
                    return MakeInvalid(DiagnosticType::InvalidNumber);

                if(firstChar < firstNonHex) // has hex chars, not a number
                    return MakeInvalid(DiagnosticType::InvalidNumber);

                // falls through as a multidimensional int
            }



            size_t firstNonDigit = content.find_first_not_of("0123456789", index + 1);
            if(firstNonDigit == std::string_view::npos)  // eof before any separator
            {
                Token token = Token(TokenType::IntLiteral, index, static_cast<uint32_t>(content.size()) - index);
                token.line = line;
                token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc.)
                index = detail::UINT32_MAX_VALUE;
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
                            return MakeInvalid(DiagnosticType::InvalidNumber);  // Float can't have more than 1 dot
                        if(dotCount > 2)
                            return MakeInvalid(DiagnosticType::InvalidNumber);  // Version can have 3 dots maximum
                        
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

                    if((content[temp] == 'e' || content[temp] == 'E') && dotCount <= 1)
                    {
                        // Scientific exponent: optional sign then at least one digit
                        size_t j = temp + 1;
                        if(j < content.size() && (content[j] == '+' || content[j] == '-'))
                            j++;
                        if(j >= content.size() || !constexpr_isdigit(content[j]))
                            return MakeInvalid(DiagnosticType::InvalidNumber);  // Exponent needs a digit

                        temp = j;  // loop consumes the exponent digits
                        continue;
                    }

                    if(constexpr_isspace(content[temp]) || content[temp] == ',')
                    {
                        calculateResult();
                        index = static_cast<uint32_t>(temp);
                        return token;
                    }

                    return MakeInvalid(DiagnosticType::InvalidNumber); // Non allowed character
                }

                calculateResult();
                index = detail::UINT32_MAX_VALUE;
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
                        index = detail::UINT32_MAX_VALUE;
                        return token;
                    }

                    if(previous + 1 == firstNonDigit && (content[previous] != ',' || !constexpr_isspace(content[firstNonDigit])) && (content[previous] != 'x' || content[firstNonDigit] != '-'))
                        return MakeInvalid(DiagnosticType::InvalidNumber);  // It must have number(s) in between

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
                            return MakeInvalid(DiagnosticType::InvalidNumber);  // Multidimensional numbers can't contain more than 1 dot (for each number)

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

            /* ISO-8601 durations, not supported (they start with a letter, so not handled here)
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
                    index = detail::UINT32_MAX_VALUE;
                    return token;
                }

                if(constexpr_isspace(content[firstNonDate]) || content[firstNonDate] == ',')
                {
                    token.count = static_cast<uint32_t>(firstNonDate - token.startPosition);
                    token.extra8 = static_cast<uint8_t>(token.count);
                    index = static_cast<uint32_t>(firstNonDate);
                    return token;
                }

                return MakeInvalid(DiagnosticType::InvalidTimestamp);  // Invalid character after timestamp
            }

            if(content[firstNonDigit] == ':')  // time
            {
                Token token = Token(TokenType::TimestampLiteral, index);
                token.line = line;
                token.column = static_cast<uint16_t>(token.startPosition - lastNewLineIndex);
                const size_t firstNonDate = content.find_first_not_of("0123456789+-:.Z", index);  // time may carry a Z or +-hh:mm timezone
                if(firstNonDate == std::string_view::npos)
                {
                    token.count = static_cast<uint32_t>(content.size()) - token.startPosition;
                    index = detail::UINT32_MAX_VALUE;
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

                return MakeInvalid(DiagnosticType::InvalidTimestamp);  // Invalid character after timestamp
            }

            return MakeInvalid(DiagnosticType::InvalidToken);  // unhandled token
        }

        return MakeInvalid(DiagnosticType::InvalidToken);  // unhandled token
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

        static constexpr void                         Diagnose(DiagnosticSeverity severity, DiagnosticType type, const Tokenizer& tokenizer, const Token& token) noexcept;
        static constexpr void                         SkipToNextEntry(Tokenizer& tokenizer, bool bStopAtCloseBrace) noexcept;

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
        : type(other.type), size(other.size), capacity(other.capacity), parent(other.parent), data(other.data)   FDF_COMMENT_SWITCH(, comment(other.comment))
    {
        other.type = Type::Map;
        other.size = 0;
        other.capacity = 0;
        other.parent = nullptr;
        other.data.vEntry = nullptr;   // active member matches the reset type (Map)

        detail::constexpr_memcpy(identifier, other.identifier, detail::MAX_IDENTIFIER_LENGTH + 1);
        other.SetIdentifierSize(0U);

#if !FDF_NO_COMMENTS
        other.comment.str = nullptr;
#endif
    }
    
    constexpr Entry& Entry::operator=(Entry&& other) noexcept
    {
        if(parent)
            (void)parent->OrphanChild_INTERNAL(*this);
        ReleaseEverything();
        
        type = other.type;
        size = other.size;
        capacity = other.capacity;
        parent = other.parent;
        data = other.data;

        other.type = Type::Map;
        other.size = 0;
        other.capacity = 0;
        other.parent = nullptr;
        other.data.vEntry = nullptr;

        detail::constexpr_memcpy(identifier, other.identifier, detail::MAX_IDENTIFIER_LENGTH + 1);
        other.SetIdentifierSize(0U);

#if !FDF_NO_COMMENTS
        comment = other.comment;
        other.comment.str = nullptr;
#endif

        return *this;
    }
    
    
    constexpr Entry::Entry() noexcept
    {
        SetIdentifierSize(0);
        data.vEntry = nullptr;   // default type is Map -> keep vEntry the active union member
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

    constexpr void Entry::SetComment([[maybe_unused]] std::string_view newComment) noexcept
    {
    #if !FDF_NO_COMMENTS
        if consteval
        {
            if(comment.str)
                *comment.str = newComment;
            else
            {
                comment.str = new (std::nothrow) std::string(newComment);
                assert(comment.str && "Allocation shouldn't fail");
            }
        }
        else
        {
            if(comment.raw)
            {
                if(newComment.size() > GetCommentControlBlock()->capacity)
                {
                    detail::GlobalAllocator::Deallocate(comment.raw, GetCommentControlBlock()->capacity + sizeof(Entry::CommentControlBlock) + 1);
                    comment.raw = detail::GlobalAllocator::Allocate(newComment.size() + sizeof(Entry::CommentControlBlock) + 1);
                    GetCommentControlBlock()->capacity = static_cast<uint32_t>(newComment.size());
                    GetCommentControlBlock()->size = GetCommentControlBlock()->capacity;
                }
                else
                {
                    GetCommentControlBlock()->size = static_cast<uint32_t>(newComment.size());
                }
            }
            else
            {
                comment.raw = detail::GlobalAllocator::Allocate(newComment.size() + sizeof(Entry::CommentControlBlock) + 1);
                GetCommentControlBlock()->capacity = static_cast<uint32_t>(newComment.size());
                GetCommentControlBlock()->size = GetCommentControlBlock()->capacity;
            }
            
            size_t i = 0, j = 0;
            while(i < newComment.size() && detail::constexpr_isspace(newComment[i]))
                i++;
            for(; i < newComment.size(); i++)
            {
                if(newComment[i] != '\n')
                    GetCommentData()[j++] = newComment[i];
                else
                {
                    GetCommentData()[j++] = ' ';
                    while(i + 1 < newComment.size() && detail::constexpr_isspace(newComment[i + 1]))
                        i++;
                }
            }
            GetCommentControlBlock()->size = static_cast<uint32_t>(j);
            GetCommentData()[GetCommentControlBlock()->size] = '\0';
        }
    #endif
    }

    constexpr void Entry::ReleaseData() noexcept
    {
        if(IsDataNull())
            return;

        switch(type)
        {
        case Type::Bool:
            if consteval
                { delete[] GetDataAs<bool>(); }
            else
                { detail::GlobalAllocator::Deallocate(data.raw, size * sizeof(bool)); }
            break;
        case Type::Int:
            if consteval
                { delete GetDataVector<int64_t>(); }
            else
                { detail::GlobalAllocator::Deallocate(data.raw, size * sizeof(int64_t)); }
            break;
        case Type::UInt:
            if consteval
                { delete GetDataVector<uint64_t>(); }
            else
                { detail::GlobalAllocator::Deallocate(data.raw, size * sizeof(uint64_t)); }
            break;
        case Type::Float:
            if consteval
                { delete GetDataVector<double>(); }
            else
                { detail::GlobalAllocator::Deallocate(data.raw, size * sizeof(double)); }
            break;
        case Type::String:
            if consteval
                { delete GetDataAs<std::string>(); }
            else
                { detail::GlobalAllocator::Deallocate(data.raw, *static_cast<uint32_t*>(data.raw) + 1 + sizeof(uint32_t)); }
            break;
        case Type::Hex:
            if consteval
                { delete GetDataAs<std::string>(); }
            else
                { detail::GlobalAllocator::Deallocate(data.raw, *static_cast<uint32_t*>(data.raw) + 1 + sizeof(uint32_t)); }
            break;
        case Type::Version:
            if consteval
                { delete GetDataVector<uint64_t>(); }
            else
                { detail::GlobalAllocator::Deallocate(data.raw, size * sizeof(uint64_t)); }
            break;
        case Type::Timestamp:
            if consteval
                { delete GetDataAs<std::string>(); }
            else
                { detail::GlobalAllocator::Deallocate(data.raw, *static_cast<uint32_t*>(data.raw) + 1 + sizeof(uint32_t)); }
            break;
        case Type::Array:
        case Type::Map:
            (void)ClearChildren();
            if consteval
                { delete GetDataVector<Entry*>(); }
            else
                { (void)detail::GlobalAllocator::Deallocate(data.raw, capacity * sizeof(void*)); }
            data.vEntry = nullptr;   // keep the active member consistent with type (Map/Array)
            capacity = 0;
            return;
        case Type::Null:
        default:
            return;
        }

        data.raw = nullptr;
    }

    constexpr void Entry::ReleaseComment() noexcept
    {
    #if !FDF_NO_COMMENTS
        if consteval
        {
            if(comment.str)
                delete comment.str;
            comment.str = nullptr;
        }
        else
        {
            if(comment.raw)
                detail::GlobalAllocator::Deallocate(comment.raw, GetCommentControlBlock()->capacity + sizeof(Entry::CommentControlBlock) + 1);
            comment.raw = nullptr;
        }
    #endif
    }
    
    constexpr void Entry::ReleaseEverything() noexcept
    {
        ReleaseData();
        ReleaseComment();
    }



    
    constexpr uint32_t Entry::FindChildIndex(const Entry& e) const noexcept
    {
        assert(IsContainer() && "You can only find index, if it's a container!");
        for(uint32_t i = 0; i < size; i++)
        {
            if consteval
            {
                if((*GetDataVector<Entry*>())[i] == &e)
                    return i;
            }
            else
            {
                if(static_cast<Entry**>(data.raw)[i] == &e)
                    return i;
            }
        }
        return detail::UINT32_MAX_VALUE;
    }
    
    constexpr uint32_t Entry::FindChildIndex(const std::string_view _identifier) const noexcept
    {
        assert(IsContainer() && "You can only find index, if it's a container!");
        for(uint32_t i = 0; i < size; i++)
        {
            if consteval
            {
                if((*GetDataVector<Entry*>())[i]->GetIdentifier() == _identifier)
                    return i;
            }
            else
            {
                if(static_cast<Entry**>(data.raw)[i]->GetIdentifier() == _identifier)
                    return i;
            }
        }
        return detail::UINT32_MAX_VALUE;
    }
    
    constexpr Entry* Entry::Emplace(std::string_view _identifier) noexcept
    {
        assert(IsContainer() && "Sanity check!");
        
        if(type == Type::Map && !detail::IsValidIdentifier(_identifier))
            return nullptr;
        
        UniqueEntryPtr e = detail::Utils<>::Create();
        if(!e)
            return nullptr;
        if(type == Type::Map)
            e->SetIdentifier_INTERNAL(_identifier);
        
        return AddChild(e);
    }
    
    constexpr Entry* Entry::AddChild(UniqueEntryPtr& e) noexcept
    {
        if(!e || !IsContainer() || e->parent)
            return nullptr;
        
        e->parent = this;
        
        //TODO: In this case, we always prefer new one silently. We should allow customizing that behaviour
        if(type == Type::Map)
        {
            if(Entry* found = GetDirectChild(e->GetIdentifier()))
            {
                std::swap(found->type, e->type);
                std::swap(found->size, e->size);
                std::swap(found->capacity, e->capacity);
                std::swap(found->data, e->data);
            #if !FDF_NO_COMMENTS
                std::swap(found->comment, e->comment);
            #endif
                e.reset();
                return found;
            }
        }

        if consteval
        {
            if(!data.vEntry)
                data.vEntry = new (std::nothrow) std::vector<Entry*>();
            assert(data.vEntry && "Allocation shouldn't fail");
            GetDataVector<Entry*>()->push_back(e.get());
            size = static_cast<uint32_t>(GetDataVector<Entry*>()->size());
            return e.release();
        }
        else
        {
            if(data.raw)
            {
                assert(size <= capacity && "size must never exceed capacity");
                if(size == capacity)
                {
                    const uint32_t newCapacity = capacity * 2;
                    void* newBuffer = detail::GlobalAllocator::Allocate(newCapacity * sizeof(void*));

                    for(size_t i = 0; i < size; i++)
                        static_cast<Entry**>(newBuffer)[i] = static_cast<Entry**>(data.raw)[i];

                    (void)detail::GlobalAllocator::Deallocate(data.raw, capacity * sizeof(void*));
                    data.raw = newBuffer;
                    capacity = newCapacity;
                }
            }
            else
            {
                data.raw = detail::GlobalAllocator::Allocate(4 * sizeof(void*));
                capacity = 4;
                size = 0;
            }

            static_cast<Entry**>(data.raw)[size++] = e.get();
            return e.release();
        }
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

        if consteval
        {
            Entry* child = (*GetDataVector<Entry*>())[index];
            child->parent = nullptr;  // prevent the destructor from self-orphaning (double removal)
            // Manual shift + pop_back instead of vector::erase: MSVC's debug iterators aren't
            // constexpr-evaluable, and erase adopts an iterator over the new end
            auto& vec = *GetDataVector<Entry*>();
            for(size_t i = index; i + 1 < vec.size(); i++)
                vec[i] = vec[i + 1];
            vec.pop_back();
            detail::Utils<>::Destroy(child);
        }
        else
        {
            Entry* child = static_cast<Entry**>(data.raw)[index];
            child->parent = nullptr;  // prevent the destructor from self-orphaning (double removal)
            for(; index + 1 < size; index++)
                static_cast<Entry**>(data.raw)[index] = static_cast<Entry**>(data.raw)[index + 1];
            static_cast<Entry**>(data.raw)[index] = nullptr;
            detail::Utils<>::Destroy(child);
        }

        size--;
        return true;
    }
    
    constexpr bool Entry::ClearChildren() noexcept
    {
        if(!IsContainer() || !data.vEntry)
            return false;

        if consteval
        {
            std::vector<Entry*>& childVec = (*GetDataVector<Entry*>());
            for(size_t i = 0; i < childVec.size(); i++)
            {
                childVec[i]->parent = nullptr;  // stop the dtor self-orphaning, which would mutate childVec mid-loop
                detail::Utils<>::Destroy(childVec[i]);
            }
        }
        else
        {
            for(size_t i = 0; i < size; i++)
            {
                static_cast<Entry**>(data.raw)[i]->parent = nullptr;
                detail::Utils<>::Destroy(static_cast<Entry**>(data.raw)[i]);
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

        Entry* original;
        if consteval
        {
            original = (*GetDataVector<Entry*>())[index];
            auto& vec = *GetDataVector<Entry*>();   // manual shift + pop_back (MSVC debug erase isn't constexpr)
            for(size_t i = index; i + 1 < vec.size(); i++)
                vec[i] = vec[i + 1];
            vec.pop_back();
        }
        else
        {
            original = static_cast<Entry**>(data.raw)[index];
            for(; index + 1 < size; index++)
                static_cast<Entry**>(data.raw)[index] = static_cast<Entry**>(data.raw)[index + 1];

            static_cast<Entry**>(data.raw)[index] = nullptr;
        }

        original->parent = nullptr;
        size--;
        return original;
    }
    
    constexpr std::span<Entry*> Entry::OrphanChildren_INTERNAL() noexcept
    {
        if(!IsContainer() || !data.vEntry)
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
        if(size == 0 || type != Type::Map)
            return nullptr;
        return GetDirectChild(FindChildIndex(_identifier));
    }
    constexpr const Entry* Entry::GetDirectChild(std::string_view _identifier) const noexcept
    {
        if(size == 0 || type != Type::Map)
            return nullptr;
        return GetDirectChild(FindChildIndex(_identifier));
    }
    
    constexpr Entry* Entry::GetDirectChild(uint32_t index) noexcept
    {
        if(index >= size || !IsContainer())
            return nullptr;
        
        if consteval
            { return (*GetDataVector<Entry*>())[index]; }
        return static_cast<Entry**>(data.raw)[index];
    }
    constexpr const Entry* Entry::GetDirectChild(uint32_t index) const noexcept
    {
        if(index >= size || !IsContainer())
            return nullptr;
        
        if consteval
            { return (*GetDataVector<Entry*>())[index]; }
        return static_cast<Entry**>(data.raw)[index];
    }

    
    
    
    constexpr std::span<Entry*> Entry::GetChildren() noexcept
    {
        if(size == 0 || !IsContainer())
            return {};

        if consteval
        {
            return *GetDataVector<Entry*>();
        }
        return {static_cast<Entry**>(data.raw), size};
    }
    constexpr std::span<const Entry*> Entry::GetChildren() const noexcept
    {
        if(size == 0 || !IsContainer())
            return {};

        if consteval
        {
            return {const_cast<const Entry**>(const_cast<Entry*>(this)->GetDataVector<Entry*>()->data()), size};
        }
        return {static_cast<const Entry**>(data.raw), size};
    }
    
    constexpr std::span<Entry*> Entry::GetChildren_INTERNAL() noexcept
    {
        assert((size != 0 || IsContainer()) && "If we opt into this version which doesn't checks these, it should be already in a known good state!");
        
        if consteval
        {
            return *GetDataVector<Entry*>();
        }
        return {static_cast<Entry**>(data.raw), size};
    }
    constexpr std::span<const Entry*> Entry::GetChildren_INTERNAL() const noexcept
    {
        assert((size != 0 || IsContainer()) && "If we opt into this version which doesn't checks these, it should be already in a known good state!");
        
        if consteval
        {
            return {const_cast<const Entry**>(const_cast<Entry*>(this)->GetDataVector<Entry*>()->data()), size};
        }
        return {static_cast<const Entry**>(data.raw), size};
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
                {
                    const auto reverseChildren = current->GetChildren_INTERNAL();
                    for(size_t ci = reverseChildren.size(); ci-- > 0; )
                        stack.push_back(reverseChildren[ci]);
                }
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
                {
                    const auto reverseChildren = current->GetChildren_INTERNAL();
                    for(size_t ci = reverseChildren.size(); ci-- > 0; )
                        stack.push_back(reverseChildren[ci]);
                }
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
                {
                    const auto reverseChildren = current->GetChildren_INTERNAL();
                    for(size_t ci = reverseChildren.size(); ci-- > 0; )
                        stack.push_back(reverseChildren[ci]);
                }
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
            
            std::vector<Entry*> stack;
            if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
            {
                stack.push_back(this);
            }
            else
            {
                {
                    const auto reverseChildren = GetChildren_INTERNAL();
                    for(size_t ci = reverseChildren.size(); ci-- > 0; )
                        stack.push_back(reverseChildren[ci]);
                }
            }

            while(!stack.empty())
            {
                Entry* current = stack.back();
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
            if(size == 0 || !IsContainer())
            {
                if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                    callback(*this);
                return;
            }
            
            enum class Phase : uint8_t { Pre, InOrder, Leaf, Array, Map };
            struct Frame { Entry* e; Phase phase; uint32_t idx;};
        
            std::vector<Frame> stack;
            stack.push_back(Frame{ this, Phase::Pre, 0 });
        
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
                            if(f.e != this)
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
                            Entry* c = children[f.idx++];
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
            
            std::vector<const Entry*> stack;
            if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
            {
                stack.push_back(this);
            }
            else
            {
                {
                    const auto reverseChildren = GetChildren_INTERNAL();
                    for(size_t ci = reverseChildren.size(); ci-- > 0; )
                        stack.push_back(reverseChildren[ci]);
                }
            }

            while(!stack.empty())
            {
                const Entry* current = stack.back();
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
            if(size == 0 || !IsContainer())
            {
                if constexpr(ForEachFlags::IsSet(FLAGS, ForEachFlags::IncludeSelf))
                    callback(*this);
                return;
            }
            
            enum class Phase : uint8_t { Pre, InOrder, Leaf, Array, Map };
            struct Frame { const Entry* e; Phase phase; uint32_t idx;};
        
            std::vector<Frame> stack;
            stack.push_back(Frame{ this, Phase::Pre, 0 });
        
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
                            if(f.e != this)
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
                            const Entry* c = children[f.idx++];
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










    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<bool>() noexcept
    {
        if(type != Type::Bool)
            return std::span<bool>();
        if consteval
            { return std::span(GetDataAs<bool>(), size); }
        return std::span(static_cast<bool*>(data.raw), size);
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<int64_t>() noexcept
    {
        if(type != Type::Int)
            return std::span<int64_t>();
        if consteval
            { return std::span(*GetDataVector<int64_t>()); }
        return std::span(static_cast<int64_t*>(data.raw), size);
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
        return std::span(static_cast<uint64_t*>(data.raw), size);
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
        return std::span(static_cast<double*>(data.raw), size);
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
        return std::string_view((static_cast<char*>(data.raw) + sizeof(uint32_t)), size);
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
    [[nodiscard]] constexpr auto Entry::GetValue<Timestamp>() noexcept
    {
        return type == Type::Timestamp? Timestamp::FromText(GetValue<std::string_view>()) : Timestamp{};
    }




    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<bool>() const noexcept
    {
        if(type != Type::Bool)
            return std::span<const bool>();
        if consteval
            { return std::span(GetDataAs<bool>(), size); }
        return std::span(static_cast<const bool*>(data.raw), size);
    }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<int64_t>() const noexcept
    {
        if(type != Type::Int)
            return std::span<const int64_t>();
        if consteval
            { return std::span(*GetDataVector<int64_t>()); }
        return std::span(static_cast<const int64_t*>(data.raw), size);
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
        return std::span(static_cast<const uint64_t*>(data.raw), size);
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
        return std::span(static_cast<const double*>(data.raw), size);
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
        return std::string_view((static_cast<const char*>(data.raw) + sizeof(uint32_t)), size);
    }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<std::string>() const noexcept  { return GetValue<char>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<std::string_view>() const noexcept  { return GetValue<char>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<char*>() const noexcept  { return GetValue<char>(); }
    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<const char*>() const noexcept  { return GetValue<char>(); }

    template<>
    [[nodiscard]] constexpr auto Entry::GetValue<Timestamp>() const noexcept
    {
        return type == Type::Timestamp? Timestamp::FromText(GetValue<std::string_view>()) : Timestamp{};
    }



    
    
    
    
    
    
    
    constexpr void Entry::SetType(Type _type) noexcept
    {
        if(_type == type)
            return;
        ReleaseData();
        type = _type;
    }
    
    // Resize only applies to numeric scalar arrays. Existing elements are preserved, new ones zero-filled
    constexpr void Entry::Resize(const uint32_t _size) noexcept
    {
        switch(type)
        {
        case Type::Bool:
        case Type::Int:
        case Type::UInt:
        case Type::Float:
        case Type::Version:
            break;
        default:
            return;
        }

        if(size == _size)
            return;

        // Bool stores a raw bool[] at compile time (not a vector), so it needs its own consteval branch
        if(type == Type::Bool && std::is_constant_evaluated())
        {
            bool* newData = _size? new (std::nothrow) bool[_size] : nullptr;
            assert((_size == 0 || newData) && "Allocation shouldn't fail");
            uint32_t i = 0;
            if(data.boolArray)
            {
                for(; i < std::min(size, _size); i++)
                    newData[i] = GetDataAs<bool>()[i];
                delete[] GetDataAs<bool>();
            }
            for(; i < _size; i++)
                newData[i] = false;

            data.boolArray = newData;
            size = _size;
            return;
        }

        auto resize = [&]<typename T>() noexcept
        {
            if consteval
            {
                // Bool's consteval path (raw bool[]) is handled above; this lambda only runs for bool at runtime
                if constexpr(!std::is_same_v<T, bool>)
                {
                    if(!GetDataVector<T>())
                    {
                        GetDataVector<T>() = new (std::nothrow) std::vector<T>();
                        assert(GetDataVector<T>() && "Allocation shouldn't fail");
                    }
                    GetDataVector<T>()->resize(_size);
                }
            }
            else
            {
                void* newData = _size? detail::GlobalAllocator::Allocate(_size * sizeof(T)) : nullptr;
                uint32_t i = 0;
                if(data.raw)
                {
                    for(; i < std::min(size, _size); i++)
                        static_cast<T*>(newData)[i] = static_cast<T*>(data.raw)[i];
                    detail::GlobalAllocator::Deallocate(data.raw, size * sizeof(T));
                }
                for(; i < _size; i++)
                    static_cast<T*>(newData)[i] = T{};
                data.raw = newData;
            }
        };

        switch(type)
        {
        case Type::Bool:                    resize.operator()<bool>();     break;  // runtime only (consteval handled above)
        case Type::Int:                     resize.operator()<int64_t>();  break;
        case Type::UInt: case Type::Version: resize.operator()<uint64_t>(); break;
        case Type::Float:                   resize.operator()<double>();   break;
        default:                                                           break;
        }

        size = _size;
    }
    
    
    
    
    
    constexpr void Entry::SetValue(NullType) noexcept
    {
        ReleaseData();
        type = Type::Null;
    }
    constexpr void Entry::SetValue(NilType) noexcept
    {
        ReleaseData();
        type = Type::Null;
    }
    
    constexpr void Entry::SetValue(ArrayType) noexcept
    {
        ReleaseData();
        type = Type::Array;
    }
    constexpr void Entry::SetValue(MapType) noexcept
    {
        ReleaseData();
        type = Type::Map;
    }
    
    constexpr void Entry::SetValue(const bool value) noexcept
    {
        ReleaseData();
        type = Type::Bool;
        size = 1;
        if consteval
        {
            data.boolArray = new (std::nothrow) bool[1];
            assert(data.boolArray && "Allocation shouldn't fail");
            *GetDataAs<bool>() = value;
        }
        else
        {
            data.raw = detail::GlobalAllocator::Allocate(sizeof(bool));
            static_cast<bool*>(data.raw)[0] = value;
        }
    }
    
    constexpr void Entry::SetValue(const std::signed_integral auto value) noexcept
    {
        ReleaseData();
        type = Type::Int;
        size = 1;
        if consteval
        {
            data.vInt = new (std::nothrow) std::vector<int64_t>();
            assert(data.vInt && "Allocation shouldn't fail");
            GetDataVector<int64_t>()->resize(size);
            (*GetDataVector<int64_t>())[0] = static_cast<int64_t>(value);
        }
        else
        {
            data.raw = detail::GlobalAllocator::Allocate(size * sizeof(int64_t));
            static_cast<int64_t*>(data.raw)[0] = static_cast<int64_t>(value);
        }
    }
    
    constexpr void Entry::SetValue(const std::unsigned_integral auto value) noexcept
    {
        ReleaseData();
        type = Type::UInt;
        size = 1;
        if consteval
        {
            data.vUInt = new (std::nothrow) std::vector<uint64_t>();
            assert(data.vUInt && "Allocation shouldn't fail");
            GetDataVector<uint64_t>()->resize(size);
            (*GetDataVector<uint64_t>())[0] = static_cast<uint64_t>(value);
        }
        else
        {
            data.raw = detail::GlobalAllocator::Allocate(size * sizeof(uint64_t));
            static_cast<uint64_t*>(data.raw)[0] = static_cast<uint64_t>(value);
        }
    }
    
    constexpr void Entry::SetValue(const std::floating_point auto value) noexcept
    {
        ReleaseData();
        type = Type::Float;
        size = 1;
        if consteval
        {
            data.vFloat = new (std::nothrow) std::vector<double>();
            assert(data.vFloat && "Allocation shouldn't fail");
            GetDataVector<double>()->resize(size);
            (*GetDataVector<double>())[0] = static_cast<double>(value);
        }
        else
        {
            data.raw = detail::GlobalAllocator::Allocate(size * sizeof(double));
            static_cast<double*>(data.raw)[0] = static_cast<double>(value);
        }
    }
    
    constexpr void Entry::SetValue(const std::string_view value) noexcept
    {
        ReleaseData();
        size = static_cast<uint32_t>(value.size());
        type = Type::String;
        if consteval
        {
            data.str = new (std::nothrow) std::string();
            assert(data.str && "Allocation shouldn't fail");
            (*GetDataAs<std::string>()) = value;
        }
        else
        {
            data.raw = detail::GlobalAllocator::Allocate(size + 1 + sizeof(uint32_t));
            *static_cast<uint32_t*>(data.raw) = size;
            detail::constexpr_memcpy((static_cast<char*>(data.raw) + sizeof(uint32_t)), value.data(), value.size() + 1);
            (static_cast<char*>(data.raw) + sizeof(uint32_t))[value.size()] = '\0';
        }
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
        ReleaseData();
        std::string text;
        value.AppendTo(text);
        type = Type::Timestamp;
        size = static_cast<uint32_t>(text.size());
        if consteval
        {
            data.str = new (std::nothrow) std::string(std::move(text));
            assert(data.str && "Allocation shouldn't fail");
        }
        else
        {
            data.raw = detail::GlobalAllocator::Allocate(size + 1 + sizeof(uint32_t));
            *static_cast<uint32_t*>(data.raw) = size;
            detail::constexpr_memcpy((static_cast<char*>(data.raw) + sizeof(uint32_t)), text.data(), size);
            (static_cast<char*>(data.raw) + sizeof(uint32_t))[size] = '\0';
        }
    }
    
    
    
    
    
    constexpr void Entry::SetValue(std::span<bool> value) noexcept
    {
        ReleaseData();
        type = Type::Bool;
        size = static_cast<uint32_t>(value.size());
        if consteval
        {
            data.boolArray = new (std::nothrow) bool[size];
            assert(data.boolArray && "Allocation shouldn't fail");
            for(size_t i = 0; i < size; i++)
                *(GetDataAs<bool>() + 1) = value[i];
        }
        else
        {
            data.raw = detail::GlobalAllocator::Allocate(size * sizeof(bool));
            for(size_t i = 0; i < size; i++)
                static_cast<bool*>(data.raw)[i] = value[i];
        }
    }
    
    template <std::signed_integral T>
    constexpr void Entry::SetValue(std::span<T> value) noexcept
    {
        ReleaseData();
        type = Type::Int;
        size = static_cast<uint32_t>(value.size());
        if(size <= 0U)
            return;
        if consteval
        {
            data.vInt = new (std::nothrow) std::vector<int64_t>();
            assert(data.vInt && "Allocation shouldn't fail");
            GetDataVector<int64_t>()->resize(size);
            for(size_t i = 0; i < size; i++)
                (*GetDataVector<int64_t>())[i] = static_cast<int64_t>(value[i]);
        }
        else
        {
            data.raw = detail::GlobalAllocator::Allocate(size * sizeof(int64_t));
            for(size_t i = 0; i < size; i++)
                static_cast<int64_t*>(data.raw)[i] = static_cast<int64_t>(value[i]);
        }
    }
    
    template <std::unsigned_integral T>
    constexpr void Entry::SetValue(std::span<T> value) noexcept
    {
        ReleaseData();
        type = Type::UInt;
        size = static_cast<uint32_t>(value.size());
        if(size <= 0U)
            return;
        if consteval
        {
            data.vUInt = new (std::nothrow) std::vector<uint64_t>();
            assert(data.vUInt && "Allocation shouldn't fail");
            GetDataVector<uint64_t>()->resize(size);
            for(size_t i = 0; i < size; i++)
                (*GetDataVector<uint64_t>())[i] = static_cast<uint64_t>(value[i]);
        }
        else
        {
            data.raw = detail::GlobalAllocator::Allocate(size * sizeof(uint64_t));
            for(size_t i = 0; i < size; i++)
                static_cast<uint64_t*>(data.raw)[i] = static_cast<uint64_t>(value[i]);
        }
    }
    
    template <std::unsigned_integral T>
    constexpr void Entry::SetValue(std::span<T> value, VersionType) noexcept
    {
        assert((value.size() == 3 || value.size() == 4) && "Version must include 3 or 4 elements!");
        ReleaseData();
        type = Type::UInt;
        size = static_cast<uint32_t>(value.size());
        if consteval
        {
            data.vUInt = new (std::nothrow) std::vector<uint64_t>();
            assert(data.vUInt && "Allocation shouldn't fail");
            GetDataVector<uint64_t>()->resize(4, 0ULL);
            for(size_t i = 0; i < size; i++)
                (*GetDataVector<uint64_t>())[i] = static_cast<uint64_t>(value[i]);
        }
        else
        {
            data.raw = detail::GlobalAllocator::Allocate(4 * sizeof(uint64_t));
            static_cast<uint64_t*>(data.raw)[0] = 0ULL;
            static_cast<uint64_t*>(data.raw)[1] = 0ULL;
            static_cast<uint64_t*>(data.raw)[2] = 0ULL;
            static_cast<uint64_t*>(data.raw)[3] = 0ULL;
            for(size_t i = 0; i < size; i++)
                static_cast<uint64_t*>(data.raw)[i] = static_cast<uint64_t>(value[i]);
        }
    }
    
    template <std::floating_point T>
    constexpr void Entry::SetValue(std::span<T> value) noexcept
    {
        ReleaseData();
        type = Type::Float;
        size = static_cast<uint32_t>(value.size());
        if(size <= 0u)
            return;
        if consteval
        {
            data.vFloat = new (std::nothrow) std::vector<double>();
            assert(data.vFloat && "Allocation shouldn't fail");
            GetDataVector<double>()->resize(size);
            for(size_t i = 0; i < size; i++)
                (*GetDataVector<double>())[i] = static_cast<double>(value[i]);
        }
        else
        {
            data.raw = detail::GlobalAllocator::Allocate(size * sizeof(double));
            for(size_t i = 0; i < size; i++)
                static_cast<double*>(data.raw)[i] = static_cast<double>(value[i]);
        }
    }










#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4702)
#endif
    template<Style STYLE>
    [[nodiscard]] constexpr std::string_view Entry::DataToView(std::string& temp) const noexcept
    {
        switch(type)
        {
            case Type::Null:    if constexpr(STYLE.bUseNilInsteadOfNull) return detail::KEYWORDS[1]; return detail::KEYWORDS[0];
            case Type::Array:   return detail::ARRAY_TEXT;
            case Type::Map:     return detail::MAP_TEXT;

            case Type::String:
            case Type::Timestamp:
                return GetValue<char>();
            
            case Type::Hex:
            {
                // Stored as "0x" + digits, no terminator. Keep the "0x" prefix lowercase (the lexer only
                // accepts a lowercase x), case the digits per style, and re-append the '#' terminator
                temp = GetValue<char>();
                for(size_t i = 2; i < temp.size(); i++)
                {
                    const char c = temp[i];
                    if constexpr(STYLE.bUppercaseHex)
                        { if(c >= 'a' && c <= 'f') temp[i] = static_cast<char>(c - 32); }
                    else
                        { if(c >= 'A' && c <= 'F') temp[i] = static_cast<char>(c + 32); }
                }
                temp.push_back('#');
                return temp;
            }

            case Type::Version:
            {
                const auto span = GetValue<uint64_t>();
                if(span.empty())
                    return {};
                temp.clear();
                for(size_t i = 0; i < span.size(); i++)
                {
                    if(i) temp.push_back('.');
                    detail::AppendUInt(temp, span[i]);
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
                    if(i) temp.push_back('x');
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
                    if(i) temp.push_back('x');
                    detail::AppendInt(temp, span[i]);
                }
                return temp;
            }

            case Type::UInt:
            {
                const auto span = GetValue<uint64_t>();
                if(span.empty())
                    return {};
                temp.clear();
                for(size_t i = 0; i < span.size(); i++)
                {
                    if(i) temp.push_back('x');
                    detail::AppendUInt(temp, span[i]);
                }
                return temp;
            }

            case Type::Float:
            {
                const auto span = GetValue<float>();
                if(span.empty())
                    return {};
                temp.clear();
                for(size_t i = 0; i < span.size(); i++)
                {
                    if(i) temp.push_back('x');
                    detail::AppendDouble(temp, span[i]);
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
    template <auto DIAGNOSTIC_CALLBACK>
    bool Entry::ParseCombineFile(const std::filesystem::path& filepath, CommentCombineStrategy fileCommentCombineStrategy) noexcept
    {
        std::error_code ec;
        if(!std::filesystem::exists(filepath, ec) || ec || !std::filesystem::is_regular_file(filepath, ec) || ec)
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
    
    constexpr bool Entry::Combine(UniqueEntryPtr& other, [[maybe_unused]] CommentCombineStrategy fileCommentCombineStrategy) noexcept
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
            {
                const std::string_view existing = GetComment();
                const std::string_view incoming = other->GetComment();
                std::string merged;
                merged.reserve(existing.size() + 1 + incoming.size());
                merged.append(existing).append(1, '\n').append(incoming);
                SetComment(merged);
            }
            break;
        case CommentCombineStrategy::Clear: ReleaseComment(); break;
        default: std::unreachable();
        }
    #endif
        
        return AddChild(other);
    }
    
    
    
    
    
    template<auto DIAGNOSTIC_CALLBACK>
    UniqueEntryPtr ParseFile(const std::filesystem::path& filepath) noexcept
    {
        std::error_code ec;
        if(!std::filesystem::exists(filepath, ec) || ec || !std::filesystem::is_regular_file(filepath, ec) || ec)
            return nullptr;

        std::ifstream file(filepath);
        if(!file)
            return nullptr;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return detail::Utils<DIAGNOSTIC_CALLBACK>::ParseBuffer(content);
    }

    template<auto DIAGNOSTIC_CALLBACK>
    constexpr UniqueEntryPtr ParseBuffer(std::string_view content) noexcept
    {
        return detail::Utils<DIAGNOSTIC_CALLBACK>::ParseBuffer(content);
    }

    template<Style STYLE>
    bool WriteFile(const Entry& e, const std::filesystem::path& filepath, bool bCreateIfNotExists) noexcept
    {
        auto parentDir = filepath.parent_path();
        std::error_code ec;
        if(!std::filesystem::exists(parentDir, ec) && (ec || !bCreateIfNotExists || !std::filesystem::create_directories(parentDir, ec) || ec))
            return false;
        if(std::filesystem::exists(filepath, ec) && (ec || !std::filesystem::is_regular_file(filepath, ec) || ec))
            return false;

        std::ofstream file(filepath);
        if(!file)
            return false;

        std::string buffer;
        detail::Utils<>::WriteBuffer<STYLE>(e, buffer);

        file << buffer;
        return static_cast<bool>(file);
    }
    template<Style STYLE>
    constexpr void WriteBuffer(const Entry& root, std::string& buffer) noexcept
    {
        detail::Utils<>::WriteBuffer<STYLE>(root, buffer);
    }
    
    
    constexpr UniqueEntryPtr NewEntry() noexcept
    {
        UniqueEntryPtr e = detail::Utils<>::Create();
        if(!e)
            return nullptr;
        
        e->type = Type::Map;
        return e;
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
    constexpr void Utils<DIAGNOSTIC_CALLBACK>::Diagnose(DiagnosticSeverity severity, DiagnosticType type, const Tokenizer& tokenizer, const Token& token) noexcept
    {
        if constexpr(!std::is_null_pointer_v<std::remove_cvref_t<decltype(DIAGNOSTIC_CALLBACK)>>)
        {
            static_assert(IsValidDiagnosticCallback<decltype(DIAGNOSTIC_CALLBACK)>, "DIAGNOSTIC_CALLBACK must be invocable with (const Diagnostic&)");
            DIAGNOSTIC_CALLBACK(Diagnostic{ severity, type, tokenizer.ToView(token), token.line, token.column, token.startPosition });
        }
    }

    // Skip tokens until the next entry boundary so parsing can resume after a malformed entry
    // Brace-depth aware so it behaves the same for single-line and multi-line containers: a
    // comma or newline at depth 0 separates entries, a close brace at depth 0 ends the container
    // When bStopAtCloseBrace, that close brace is left for the caller's container loop to consume
    template<auto DIAGNOSTIC_CALLBACK>
    constexpr void Utils<DIAGNOSTIC_CALLBACK>::SkipToNextEntry(Tokenizer& tokenizer, bool bStopAtCloseBrace) noexcept
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

    template<auto DIAGNOSTIC_CALLBACK>
    constexpr UniqueEntryPtr Utils<DIAGNOSTIC_CALLBACK>::ParseBuffer(std::string_view content) noexcept
    {
        // Strip UTF-8 BOM if present
        if(content.size() >= 3 &&
           static_cast<uint8_t>(content[0]) == 0xEF &&
           static_cast<uint8_t>(content[1]) == 0xBB &&
           static_cast<uint8_t>(content[2]) == 0xBF)
            content = content.substr(3);

        Tokenizer tokenizer(content);
        #if !FDF_NO_COMMENTS
            Token fileCommentToken = TokenType::NonExisting;
        #endif

        UniqueEntryPtr root = Create();
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
                Diagnose(DiagnosticSeverity::Fatal, static_cast<DiagnosticType>(currentToken.extra8), tokenizer, currentToken);
                return nullptr;
            }

            while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
            {
            #if !FDF_NO_COMMENTS
                if(currentToken.type == TokenType::Comment)
                {
                    if(root->size == 0 && root->comment.str == nullptr && currentToken.count > 0 && content[currentToken.startPosition] == '#')
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
                            Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken);
                        fileCommentToken = currentToken;
                    }
                    else
                    {
                        if(comment.type != TokenType::NonExisting)
                            Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken);
                        comment = currentToken;
                    }
                }
            #endif
        
                currentToken = tokenizer.Advance();
            }
        
            if(currentToken.type == TokenType::Identifier)
            {
                const uint32_t childCountBefore = root->GetChildCount();
                if(!ParseVariable(tokenizer, *root   FDF_COMMENT_SWITCH(,comment)))
                {
                    // Drop the half-built entry, report it, and resume at the next line
                    while(root->GetChildCount() > childCountBefore)
                        (void)root->RemoveChild(root->GetChildCount() - 1);

                    if(tokenizer.Current().type == TokenType::Invalid)
                    {
                        Diagnose(DiagnosticSeverity::Fatal, static_cast<DiagnosticType>(tokenizer.Current().extra8), tokenizer, tokenizer.Current());
                        return nullptr;  // Lexer error: can't reliably resume
                    }
                    SkipToNextEntry(tokenizer, false);  // ParseVariable already reported the error
                }

                continue;
            }

            if(currentToken.type == TokenType::EndOfFile)
                break;

            // A top-level line must start with an identifier; report and skip it
            Diagnose(DiagnosticSeverity::Error, DiagnosticType::UnexpectedToken, tokenizer, currentToken);
            SkipToNextEntry(tokenizer, false);
        }

        #if !FDF_NO_COMMENTS
            // Trim the whitespace from the comment (not '\n')
            if(fileCommentToken.type != TokenType::NonExisting)
            {
                std::string_view view = tokenizer.ToView(fileCommentToken);
                if consteval
                {
                    root->comment.str = new (std::nothrow) std::string();
                    assert(root->comment.str && "Allocation shouldn't fail");
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
                    root->comment.raw = GlobalAllocator::Allocate(view.size() + sizeof(Entry::CommentControlBlock) + 1);
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
        Token currentToken = tokenizer.Current();
        UniqueEntryPtr _temp = Utils<>::Create();
        Entry* entry = parent.AddChild(_temp);
        if(!entry)
            return false;

        if(parent.type == Type::Map)
        {
            assert(tokenizer.Current().type == TokenType::Identifier && "Sanity check!");
            if(!entry->SetIdentifier(tokenizer.ToView(currentToken)))
            {
                Diagnose(DiagnosticSeverity::Error, DiagnosticType::InvalidIdentifier, tokenizer, currentToken);
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
                    Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken);
                comment = currentToken;
            }
        #endif

            currentToken = tokenizer.Advance();
            FDF_CHECK_TOKEN(currentToken);
            FDF_CHECK_TOKEN_FOR_EOF(currentToken);
        }
        
        if(IsValueLiteral(currentToken.type) && (bHasEqual || parent.type == Type::Array))
            return ParseSimpleValue(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment));

        // '=' introduces a scalar value only; a container must follow the identifier directly
        if(bHasEqual && (currentToken.type == TokenType::CurlyBraceOpen || currentToken.type == TokenType::SquareBraceOpen))
        {
            Diagnose(DiagnosticSeverity::Error, DiagnosticType::UnexpectedToken, tokenizer, currentToken);
            return false;
        }
        if(currentToken.type == TokenType::CurlyBraceOpen)
            return ParseMap(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment));
        if(currentToken.type == TokenType::SquareBraceOpen)
            return ParseArray(tokenizer, *entry    FDF_COMMENT_SWITCH(, comment));

        Diagnose(DiagnosticSeverity::Error, DiagnosticType::UnexpectedToken, tokenizer, currentToken);
        return false;  // unhandled token
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
                    Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken);
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
                    entry.data.boolArray = new (std::nothrow) bool[1];
                    assert(entry.data.boolArray && "Allocation shouldn't fail");
                    *entry.GetDataAs<bool>() = currentToken.extra8 == 2;
                }
                else
                {
                    entry.data.raw = GlobalAllocator::Allocate(sizeof(bool));
                    static_cast<bool*>(entry.data.raw)[0] = currentToken.extra8 == 2;
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
                    entry.data.boolArray = new (std::nothrow) bool[entry.size];
                    assert(entry.data.boolArray && "Allocation shouldn't fail");
                }
                else
                {
                    entry.data.raw = GlobalAllocator::Allocate(entry.size * sizeof(bool));
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
                            { static_cast<bool*>(entry.data.raw)[cur++] = true; }
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
                            { static_cast<bool*>(entry.data.raw)[cur++] = false; }
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




        // Dimension count (Int/Float/Version, always small) or byte length (String/Hex/Timestamp)
        // Length comes from the view, not extra8 (uint8) which truncates strings over 255 bytes,
        // common with UTF-8 content, and would under-allocate then overflow on write
        switch(currentToken.type)
        {
            case TokenType::StringLiteral:                            entry.size = static_cast<uint32_t>(view.size() - 2); break;
            case TokenType::HexLiteral: case TokenType::TimestampLiteral: entry.size = static_cast<uint32_t>(view.size()); break;
            default:                                                  entry.size = currentToken.extra8; break;
        }




        if(currentToken.type == TokenType::IntLiteral)
        {
            if consteval
            {
                entry.data.vInt = new (std::nothrow) std::vector<int64_t>();
                assert(entry.data.vInt && "Allocation shouldn't fail");
                entry.GetDataVector<int64_t>()->resize(entry.size);
            }
            else
            {
                entry.data.raw = GlobalAllocator::Allocate(entry.size * sizeof(int64_t));
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
                        { static_cast<int64_t*>(entry.data.raw)[currentDimension] = -static_cast<int64_t>(result); }
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
                                entry.data.vUInt = temp;
                            }
                            else
                            {
                                for(uint8_t i = 0; i < currentDimension - 1; i++)
                                {
                                    const int64_t temp = static_cast<int64_t*>(entry.data.raw)[i];
                                    static_cast<uint64_t*>(entry.data.raw)[i] = static_cast<uint64_t>(temp);
                                }
                            }
                        }

                        if consteval
                            { (*entry.GetDataVector<uint64_t>())[currentDimension] = result; }
                        else
                            { static_cast<uint64_t*>(entry.data.raw)[currentDimension] = result; }
                    }
                    else
                    {
                        if consteval
                            { (*entry.GetDataVector<int64_t>())[currentDimension] = static_cast<int64_t>(result); }
                        else
                            { static_cast<int64_t*>(entry.data.raw)[currentDimension] = static_cast<int64_t>(result); }
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
            return postProcess();
        }



        
        if(currentToken.type == TokenType::FloatLiteral)
        {
            entry.type = Type::Float;
            if consteval
            {
                entry.data.vFloat = new (std::nothrow) std::vector<double>();
                assert(entry.data.vFloat && "Allocation shouldn't fail");
                entry.GetDataVector<double>()->resize(entry.size);
            }
            else
            {
                entry.data.raw = GlobalAllocator::Allocate(entry.size * sizeof(double));
            }

            uint8_t currentDimension = 0;
            const uint8_t dimensionCount = static_cast<uint8_t>(entry.size);

            // Split on 'x' (multi-dimensional) and round-trip-parse each component
            size_t segStart = 0;
            for(size_t i = 0; i <= view.size(); i++)
            {
                if(i != view.size() && view[i] != 'x')
                    continue;

                if(currentDimension >= dimensionCount)
                    return false;  // more components than the tokenizer counted

                const std::string_view seg = view.substr(segStart, i - segStart);
                bool bOk = false;
                const double result = detail::ParseDouble(seg.data(), seg.data() + seg.size(), &bOk);
                if(!bOk)
                    return false;

                if consteval
                    { (*entry.GetDataVector<double>())[currentDimension] = result; }
                else
                    { static_cast<double*>(entry.data.raw)[currentDimension] = result; }

                currentDimension++;
                segStart = i + 1;
            }

            if(currentDimension != dimensionCount)
                return false;

            return postProcess();
        }
    
    
    
    
        if(currentToken.type == TokenType::VersionLiteral)
        {
            entry.type = Type::Version;

            // Allocate exactly entry.size (the component count): GetValue and ReleaseData both key off
            // size, so over-allocating to 4 desynced the consteval span length and the runtime free size
            if consteval
            {
                entry.data.vUInt = new (std::nothrow) std::vector<uint64_t>();
                assert(entry.data.vUInt && "Allocation shouldn't fail");
                entry.GetDataVector<uint64_t>()->resize(entry.size);
            }
            else
            {
                entry.data.raw = GlobalAllocator::Allocate(entry.size * sizeof(uint64_t));
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
                        { static_cast<uint64_t*>(entry.data.raw)[currentDimension] = result; }
    
                    result = 0;
                    currentDimension++;
                }
                else
                    return false;  // unknown character
            }

            if consteval
                { (*entry.GetDataVector<uint64_t>())[currentDimension] = result; }
            else
                { static_cast<uint64_t*>(entry.data.raw)[currentDimension] = result; }

            return postProcess();
        }


        // Validate the timestamp before allocating below. Returning after the allocation would leave
        // a String-shaped buffer attached to a still-default (Map) entry, which ReleaseData would
        // later misread as a child array, corrupting recovery cleanup
        if(currentToken.type == TokenType::TimestampLiteral && !IsValidTimestamp(view))
        {
            Diagnose(DiagnosticSeverity::Error, DiagnosticType::InvalidTimestamp, tokenizer, currentToken);
            return false;
        }


        if consteval
        {
            entry.data.str = new (std::nothrow) std::string();
            assert(entry.data.str && "Allocation shouldn't fail");
            entry.GetDataAs<std::string>()->resize(entry.size);
        }
        else
        {
            entry.data.raw = GlobalAllocator::Allocate(entry.size + 1 + sizeof(uint32_t));
            *static_cast<uint32_t*>(entry.data.raw) = entry.size;
        }
        
        size_t size = 0;
        auto writeCharacter = [&](char c)
        {
            if consteval
                { (*entry.GetDataAs<std::string>())[size++] = c; }
            else
                { (static_cast<char*>(entry.data.raw) + sizeof(uint32_t))[size++] = c; }
        };

        if(currentToken.type == TokenType::StringLiteral)
        {
            entry.type = Type::String;

            static constexpr uint32_t start = 1;
                   const     uint32_t end = static_cast<uint32_t>(view.size()) - 1U;

            auto isEscapableChar     = [](char c) -> bool  { return c == '\"' || c == '\'' || c == '\\'; };
            auto isMergeEscapeChar   = [](char c) -> bool  { return c == 'n'  || c == 'r'  || c == 't' || c == 'v' || c == 'b' || c == 'f' || c == 'a'; };

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
                { entry.GetDataAs<std::string>()->resize(size); }  // escapes shrink the string below the pre-allocated upper bound
            else
                { writeCharacter('\0'); }

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
                constexpr_memcpy((static_cast<char*>(entry.data.raw) + sizeof(uint32_t)), view.data(), entry.size);
                (static_cast<char*>(entry.data.raw) + sizeof(uint32_t))[entry.size] = '\0';
            }

            return postProcess();
        }

        return false;  // unhandled token
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
                    if(childComment.type != TokenType::NonExisting)
                        Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken);
                    childComment = currentToken;
                }
            #endif

                currentToken = tokenizer.Advance();
                FDF_CHECK_TOKEN(currentToken);
                FDF_CHECK_TOKEN_FOR_EOF(currentToken);
            }


            if(IsValueLiteral(currentToken.type) || currentToken.type == TokenType::CurlyBraceOpen || currentToken.type == TokenType::SquareBraceOpen)
            {
                const uint32_t childCountBefore = array.GetChildCount();
                if(!ParseVariable(tokenizer, array   FDF_COMMENT_SWITCH(,childComment)))
                {
                    while(array.GetChildCount() > childCountBefore)
                        (void)array.RemoveChild(array.GetChildCount() - 1);

                    if(tokenizer.Current().type == TokenType::Invalid)
                    {
                        Diagnose(DiagnosticSeverity::Fatal, static_cast<DiagnosticType>(tokenizer.Current().extra8), tokenizer, tokenizer.Current());
                        return false;  // Lexer error: can't reliably resume
                    }
                    SkipToNextEntry(tokenizer, true);  // ParseVariable already reported the error
                    currentToken = tokenizer.Current();
                    continue;
                }
                currentToken = tokenizer.Current();
            }
            else if(currentToken.type == TokenType::SquareBraceClose)
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
                        Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken);
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

                return true;
            }
            else
            {
                // Unexpected token inside an array: report and skip to the next element or the array's end
                if(currentToken.type == TokenType::EndOfFile)
                {
                    Diagnose(DiagnosticSeverity::Fatal, DiagnosticType::UnexpectedEndOfFile, tokenizer, currentToken);
                    return false;
                }
                if(currentToken.type == TokenType::Invalid)
                {
                    Diagnose(DiagnosticSeverity::Fatal, static_cast<DiagnosticType>(currentToken.extra8), tokenizer, currentToken);
                    return false;
                }
                Diagnose(DiagnosticSeverity::Error, DiagnosticType::UnexpectedToken, tokenizer, currentToken);
                SkipToNextEntry(tokenizer, true);
                currentToken = tokenizer.Current();
            }
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
                    if(childComment.type != TokenType::NonExisting)
                        Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken);
                    childComment = currentToken;
                }
            #endif

                currentToken = tokenizer.Advance();
                FDF_CHECK_TOKEN(currentToken);
                FDF_CHECK_TOKEN_FOR_EOF(currentToken);
            }


            if(currentToken.type == TokenType::Identifier)
            {
                const uint32_t childCountBefore = map.GetChildCount();
                if(!ParseVariable(tokenizer, map   FDF_COMMENT_SWITCH(,childComment)))
                {
                    while(map.GetChildCount() > childCountBefore)
                        (void)map.RemoveChild(map.GetChildCount() - 1);

                    if(tokenizer.Current().type == TokenType::Invalid)
                    {
                        Diagnose(DiagnosticSeverity::Fatal, static_cast<DiagnosticType>(tokenizer.Current().extra8), tokenizer, tokenizer.Current());
                        return false;  // Lexer error: can't reliably resume
                    }
                    SkipToNextEntry(tokenizer, true);  // ParseVariable already reported the error
                    currentToken = tokenizer.Current();
                    continue;
                }
                currentToken = tokenizer.Current();
            }
            else if(currentToken.type == TokenType::CurlyBraceClose)
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
                        Diagnose(DiagnosticSeverity::Warning, DiagnosticType::AlreadyHasComment, tokenizer, currentToken);
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

                return true;
            }
            else
            {
                // Unexpected token inside a map: report and skip to the next entry or the map's end
                if(currentToken.type == TokenType::EndOfFile)
                {
                    Diagnose(DiagnosticSeverity::Fatal, DiagnosticType::UnexpectedEndOfFile, tokenizer, currentToken);
                    return false;
                }
                if(currentToken.type == TokenType::Invalid)
                {
                    Diagnose(DiagnosticSeverity::Fatal, static_cast<DiagnosticType>(currentToken.extra8), tokenizer, currentToken);
                    return false;
                }
                Diagnose(DiagnosticSeverity::Error, DiagnosticType::UnexpectedToken, tokenizer, currentToken);
                SkipToNextEntry(tokenizer, true);
                currentToken = tokenizer.Current();
            }
        }
    }










    template<bool CHECK_SINGLE_LINE>
    struct ScopePositions;
    template<>
    struct ScopePositions<true> { size_t begin = 0, end = 0, textBegin = 0, spaces = 0; };
    template<>
    struct ScopePositions<false>{ size_t begin = 0; };
    
    
    template<auto DIAGNOSTIC_CALLBACK>
    template<Style STYLE>
    constexpr void Utils<DIAGNOSTIC_CALLBACK>::WriteBuffer(const Entry& root, std::string& buffer, const bool bOverwrite) noexcept
    {
        assert((root.GetIdentifierSize() != 0 || (root.type == Type::Map && !root.parent)) && "Unless it's a map, it must have an identifier");

        static constexpr bool CHECK_SINGLE_LINE = STYLE.singleLineContainerLimit > 5;
        std::vector<ScopePositions<CHECK_SINGLE_LINE>> scopes;
        uint32_t lastDepth = 0u;
        const bool bHasDedicatedRoot = !root.parent && root.type == Type::Map && root.GetIdentifierSize() == 0;

        const size_t totalChildCount = root.GetChildCountRecursive();
        if(bOverwrite)
            buffer.clear();
        buffer.reserve(buffer.size() + (totalChildCount * 50));





        auto writeEntryNameFn = [&](const Entry& e) -> void  { buffer.append(e.GetIdentifier()); };
        auto addTabFn = [&buffer](const size_t count) -> void
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
            buffer.append(", ");
        };





        auto writeSimpleEntryValueFn = [&](const Entry& e) -> void
        {
            std::string temp;
            const std::string_view view = e.DataToView<STYLE>(temp);
            if(e.type != Type::String)
            {
                buffer.append(view);
                return;
            }

            // Pick the quote that needs the least escaping: single quotes when the value has a double
            // quote but no single quote (and the style allows it), double quotes otherwise. Backslash
            // and control chars are always escaped so the output re-parses to the exact same value
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
        [[maybe_unused]] auto writeSimpleEntryFn = [&](const Entry& e) -> void
        {
            writeEntryNameFn(e);
            addEqualSignFn();
            writeSimpleEntryValueFn(e);
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
                        
                        for(size_t pos = scopes[j].end; pos - 2 > scopes[j].begin; pos--)
                        {
                            if(buffer[pos] == '/' && buffer[pos - 1] == '/')
                            {
                                { for(size_t k = j; k + 1 < scopes.size(); k++) scopes[k] = scopes[k + 1]; scopes.pop_back(); }   // manual erase (MSVC debug vector::erase isn't constexpr)
                                return;
                            }
                            if(buffer[pos] == ' ' && (detail::constexpr_isspace(buffer[pos - 2]) || IsBrace(buffer[pos - 2])))
                                scopes[j].spaces++;
                            else if(buffer[pos] == '\t')
                                scopes[j].spaces += STYLE.tabSize;
                        }
                        if(scopes[j].end - scopes[j].textBegin + 1 - scopes[j].spaces > STYLE.singleLineContainerLimit)
                            { for(size_t k = j; k + 1 < scopes.size(); k++) scopes[k] = scopes[k + 1]; scopes.pop_back(); }   // manual erase (MSVC debug vector::erase isn't constexpr)
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





        //TODO LOOK AT DIFF BETWEEN DEPTH AND LAST DEPTH TO FIGURE OUT COMMAS
        auto writeFn = [&](const Entry& e) -> void
        {
            const uint32_t depth = bHasDedicatedRoot? e.CalculateDepth() - 1 : e.CalculateDepth();
            for(int64_t i = 0; i < static_cast<int64_t>(lastDepth) - static_cast<int64_t>(depth); i++)
                closeScopes(i, depth > 0 || STYLE.bTopLevelCommas || i + 1 < static_cast<int64_t>(lastDepth) - static_cast<int64_t>(depth));





            if(e.IsContainer())
            {
                // no blank line before the first element of a nested container
                const size_t found = buffer.find_last_not_of("\n\t ");
                if(found == std::string::npos || !IsOpenBrace(buffer[found]))
                    buffer.push_back('\n');   
            }





        #if !FDF_NO_COMMENTS
            // Short comments on simple entries go inline (trailing); long ones and container
            // comments stay as leading // lines because they can't share the value's line
            const std::string_view entryComment = STYLE.bEntryComment? e.GetComment() : std::string_view{};
            const bool bInlineComment = !entryComment.empty() && !e.IsContainer() && entryComment.size() <= STYLE.singleLineCommentLimit;
            if(!entryComment.empty() && !bInlineComment)
            {
                std::string_view sv = entryComment;
                const size_t start = buffer.size();
                if constexpr(STYLE.singleLineCommentLimit < 5)
                {
                    addTabFn(depth);
                    buffer.append("// ");
                    buffer.append(sv);
                    buffer.append("\n");
                }
                else
                {
                    const uint32_t overhead = (depth * STYLE.tabSize) + 3;
                    if(overhead + 5 > STYLE.singleLineCommentLimit)
                    {
                        addTabFn(depth);
                        buffer.append("// ");
                        buffer.append(sv);
                        buffer.append("\n");
                    }
                    else
                    {
                        bool bMultiLine = false;
                        while(!sv.empty())
                        {
                            addTabFn(depth);
                            buffer.append("// ");
                            if(sv.size() > STYLE.singleLineCommentLimit - overhead)
                            {
                                bMultiLine = true;
                                uint32_t pos = STYLE.singleLineCommentLimit - overhead;
                                if(!detail::constexpr_isspace(buffer[pos]))
                                {
                                    const size_t found = sv.find_last_of(' ', pos);
                                    if(found != std::string::npos)
                                        pos = static_cast<uint32_t>(found);
                                }
                                buffer.append(sv.substr(0, pos));
                                sv = sv.substr(pos + 1);
                            }
                            else
                            {
                                buffer.append(sv);
                                sv = {};
                            }
                            buffer.append("\n");
                        }
                        
                        if(bMultiLine)
                            buffer.insert(start, 1, '\n');
                    }
                }
            }
        #endif





            if(!e.IsContainer())
            {
                addTabFn(depth);
                if(!e.parent || e.parent->type != Type::Array)
                {
                    writeEntryNameFn(e);
                    addEqualSignFn();
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
                    // '\x01' is a placeholder for the gap before the comment, resolved (and aligned) in a final pass
                    buffer.push_back('\x01');
                    buffer.append("// ");
                    buffer.append(entryComment);
                }
            #endif
                buffer.push_back('\n');
            }
            else
            {
                const bool bIsMap = e.type == Type::Map;
                addTabFn(depth);

                // Empty containers emit inline (name{ } / name[ ]) without opening a scope. A deferred
                // close brace would otherwise be lost: the final flush only closes lastDepth scopes,
                // and an empty container never advances depth past its own opening
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
                    const std::string_view fileComment = root.GetComment();
                    if(!fileComment.empty())
                    {
                        buffer.append("/*#\n");
                        size_t prevNewLinePos = detail::SIZE_T_MAX_VALUE;
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
                        if(std::max(scopes[i].end, scopes[j].end) - std::min(scopes[j].begin, scopes[j].begin) + 1 - scopes[j].spaces > STYLE.singleLineContainerLimit)
                            break;
                        found = j;
                    }
                }

                if(found != detail::SIZE_T_MAX_VALUE)
                    i = found;
                
                
                // Delete blank lines between multiple new lines
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
                        if(found2 != std::string::npos && buffer[found2] != '\n')
                            buffer.erase(newLinePos, 1);
                        break;
                    }
                    if(buffer[found] == '[')
                    {
                        const size_t found2 = buffer.find_first_of("]\n", found);
                        if(found2 != std::string::npos && buffer[found2] != '\n')
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
                                if(found != std::string::npos && !IsCloseBrace(buffer[found]) && !IsOpenBrace(buffer[found]))
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
                        
                        // Eliminate multiple new lines
                        while(pos - 1 > scopes[i].begin && buffer[pos - 1] == '\n')
                            buffer.erase(--pos, 1);
                        
                        // If it contains nested single line, add some spaces to make it more clear
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
        // Resolve inline-comment pad placeholders ('\x01'). Each marks the gap before a trailing
        // "// comment". With bAlignCloseComments, the '//' of consecutive inline-commented lines
        // are padded to a shared column; otherwise each becomes a single space
        {
            constexpr char MARKER = '\x01';
            std::vector<size_t> markers;
            for(size_t p = buffer.find(MARKER); p != std::string::npos; p = buffer.find(MARKER, p + 1))
                markers.push_back(p);

            if(!markers.empty())
            {
                std::vector<size_t> pads(markers.size(), 1);
                if constexpr(STYLE.bAlignCloseComments)
                {
                    auto columnOf = [&](size_t m) -> size_t
                    {
                        const size_t lineStart = buffer.rfind('\n', m);
                        return lineStart == std::string::npos? m : m - lineStart - 1;
                    };

                    for(size_t groupStart = 0; groupStart < markers.size(); )
                    {
                        size_t groupEnd = groupStart;
                        while(groupEnd + 1 < markers.size() &&
                              std::count(buffer.begin() + static_cast<int64_t>(markers[groupEnd]), buffer.begin() + static_cast<int64_t>(markers[groupEnd + 1]), '\n') == 1)
                            groupEnd++;

                        size_t maxColumn = 0;
                        for(size_t k = groupStart; k <= groupEnd; k++)
                            maxColumn = std::max(maxColumn, columnOf(markers[k]));
                        for(size_t k = groupStart; k <= groupEnd; k++)
                            pads[k] = (maxColumn - columnOf(markers[k])) + 1;

                        groupStart = groupEnd + 1;
                    }
                }

                // Replace from the last marker to the first so earlier indices stay valid
                for(size_t k = markers.size(); k-- > 0; )
                    buffer.replace(markers[k], 1, pads[k], ' ');
            }
        }
    #endif
    }
}










#undef FDF_EXPORT
#undef FDF_EXPORT_INTERNAL
#undef FDF_CHECK_TOKEN
#undef FDF_CHECK_TOKEN_FOR_EOF
#undef FDF_FORWARD_ERROR
#undef FDF_COMMENT_SWITCH
