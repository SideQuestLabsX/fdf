// include std headers before the module import to avoid duplicate declarations
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if FDF_USE_CPP_MODULES
    import fdf;
#else
    #include "fdf.h"
#endif

#if !defined(FDF_VALIDATE_VERSION)
    #define FDF_VALIDATE_VERSION "unknown"
#endif

namespace
{
    constexpr int EXIT_OK      = 0;
    constexpr int EXIT_INVALID = 1;
    constexpr int EXIT_USAGE   = 2;

    constexpr std::string_view STDIN_NAME = "<stdin>";

    constexpr const char* SeverityText(fdf::DiagnosticSeverity severity) noexcept
    {
        switch(severity)
        {
            case fdf::DiagnosticSeverity::Fatal:   return "fatal error";
            case fdf::DiagnosticSeverity::Error:   return "error";
            case fdf::DiagnosticSeverity::Warning: return "warning";
            case fdf::DiagnosticSeverity::Info:    return "note";
            case fdf::DiagnosticSeverity::None:    return "note";
        }
        return "note";
    }

    constexpr const char* TypeText(fdf::DiagnosticType type) noexcept
    {
        switch(type)
        {
            case fdf::DiagnosticType::AlreadyHasComment:    return "entry already has a comment";
            case fdf::DiagnosticType::UnexpectedToken:      return "unexpected token";
            case fdf::DiagnosticType::InvalidIdentifier:    return "invalid identifier";
            case fdf::DiagnosticType::UnexpectedEndOfFile:  return "unexpected end of file";
            case fdf::DiagnosticType::UnterminatedString:   return "unterminated string";
            case fdf::DiagnosticType::UnterminatedComment:  return "unterminated comment";
            case fdf::DiagnosticType::InvalidComment:       return "invalid comment";
            case fdf::DiagnosticType::InvalidNumber:        return "invalid number";
            case fdf::DiagnosticType::InvalidPack:          return "invalid pack";
            case fdf::DiagnosticType::InvalidTimestamp:     return "invalid timestamp";
            case fdf::DiagnosticType::InvalidToken:         return "invalid token";
            case fdf::DiagnosticType::InvalidUtf8:          return "invalid UTF-8";
            case fdf::DiagnosticType::InputTooLarge:        return "input too large";
            case fdf::DiagnosticType::InvalidDuration:      return "invalid duration";
            case fdf::DiagnosticType::DuplicateKey:         return "duplicate key";
            case fdf::DiagnosticType::NestingTooDeep:       return "nesting too deep";
        }
        return "unknown diagnostic";
    }

    class Reporter
    {
    public:
        explicit Reporter(std::string_view inputPath) noexcept : path(inputPath) {}

        void operator()(const fdf::Diagnostic& diagnostic) noexcept
        {
            std::fprintf(stderr, "%.*s:%u:%u: %s: %s",
                         static_cast<int>(path.size()), path.data(),
                         diagnostic.line, diagnostic.column,
                         SeverityText(diagnostic.severity), TypeText(diagnostic.type));

            if(!diagnostic.message.empty())
                std::fprintf(stderr, ": %.*s", static_cast<int>(diagnostic.message.size()), diagnostic.message.data());

            std::fputc('\n', stderr);

            if(diagnostic.severity >= fdf::DiagnosticSeverity::Error)
                bFailed = true;
        }

        bool bFailed = false;

    private:
        std::string_view path;
    };

    bool ReadStdin(std::string& out) noexcept
    {
        char buffer[64 * 1024];
        for(size_t read = std::fread(buffer, 1, sizeof(buffer), stdin); read > 0;
            read = std::fread(buffer, 1, sizeof(buffer), stdin))
        {
            out.append(buffer, read);
        }

        if(std::ferror(stdin))
        {
            std::fprintf(stderr, "%.*s: read failed\n", static_cast<int>(STDIN_NAME.size()), STDIN_NAME.data());
            return false;
        }
        return true;
    }

    bool ReadInput(const char* path, std::string& out) noexcept
    {
        if(std::strcmp(path, "-") == 0)
            return ReadStdin(out);

        std::FILE* file = std::fopen(path, "rb");
        if(!file)
        {
            std::fprintf(stderr, "%s: %s\n", path, std::strerror(errno));
            return false;
        }

        char buffer[64 * 1024];
        size_t n;
        while((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
            out.append(buffer, n);

        const bool bad = std::ferror(file) != 0;
        std::fclose(file);
        if(bad)
        {
            std::fprintf(stderr, "%s: read failed\n", path);
            return false;
        }
        return true;
    }

    // canonical output must settle after one write
    bool RoundTrips(const fdf::Entry& root, std::string_view path) noexcept
    {
        const fdf::String text = fdf::WriteBuffer(root);

        // diagnostics refer to generated text
        const std::string label = std::string(path) + " (written back)";
        Reporter reporter(label);
        const fdf::UniqueEntryPtr reparsed = fdf::ParseBuffer(std::string_view(text), reporter);
        if(!reparsed || reporter.bFailed)
        {
            std::fprintf(stderr, "%.*s: round trip failed: the writer produced text the parser rejects\n",
                         static_cast<int>(path.size()), path.data());
            return false;
        }

        const fdf::String again = fdf::WriteBuffer(*reparsed);
        if(std::string_view(text) != std::string_view(again))
        {
            std::fprintf(stderr, "%.*s: round trip failed: the second write differs from the first\n",
                         static_cast<int>(path.size()), path.data());
            return false;
        }
        return true;
    }

    bool Validate(const char* path, bool bRoundTrip) noexcept
    {
        const std::string_view name = std::strcmp(path, "-") == 0? STDIN_NAME : std::string_view(path);

        std::string content;
        if(!ReadInput(path, content))
            return false;

        Reporter reporter(name);
        const fdf::UniqueEntryPtr root = fdf::ParseBuffer(content, reporter);
        if(!root)
        {
            std::fprintf(stderr, "%.*s: parse failed\n", static_cast<int>(name.size()), name.data());
            return false;
        }
        if(reporter.bFailed)
            return false;

        return !bRoundTrip || RoundTrips(*root, name);
    }

    void PrintUsage(std::FILE* stream) noexcept
    {
        std::fprintf(stream,
            "usage: fdf-validate [options] <file>...\n"
            "\n"
            "  -                read the document from stdin\n"
            "  --round-trip     also write the document back and check the round trip settles\n"
            "  --version        print the version and exit\n"
            "  -h, --help       print this help and exit\n"
            "\n"
            "Diagnostics go to stderr as path:line:column: severity: message.\n"
            "Exit status: 0 valid, 1 a document is invalid or unreadable, 2 bad usage.\n");
    }
}

int main(int argc, char** argv)
{
    bool bRoundTrip = false;
    bool bEndOfOptions = false;
    std::vector<const char*> paths;

    for(int i = 1; i < argc; i++)
    {
        const std::string_view arg = argv[i];

        if(!bEndOfOptions && arg == "--")
            bEndOfOptions = true;
        else if(!bEndOfOptions && arg == "--round-trip")
            bRoundTrip = true;
        else if(!bEndOfOptions && (arg == "-h" || arg == "--help"))
        {
            PrintUsage(stdout);
            return EXIT_OK;
        }
        else if(!bEndOfOptions && arg == "--version")
        {
            std::printf("fdf-validate %s\n", FDF_VALIDATE_VERSION);
            return EXIT_OK;
        }
        else if(!bEndOfOptions && arg.size() > 1 && arg.front() == '-')
        {
            std::fprintf(stderr, "fdf-validate: unknown option '%.*s'\n", static_cast<int>(arg.size()), arg.data());
            PrintUsage(stderr);
            return EXIT_USAGE;
        }
        else
            paths.push_back(argv[i]);
    }

    if(paths.empty())
    {
        std::fprintf(stderr, "fdf-validate: no input files\n");
        PrintUsage(stderr);
        return EXIT_USAGE;
    }

    bool bAllValid = true;
    for(const char* path : paths)
    {
        if(!Validate(path, bRoundTrip))
            bAllValid = false;
    }

    return bAllValid? EXIT_OK : EXIT_INVALID;
}
