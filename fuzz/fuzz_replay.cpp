// Runs fuzz targets over saved corpus files under ctest

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) noexcept;

namespace
{
    bool RunFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if(!file)
        {
            std::fprintf(stderr, "cannot open %s\n", path.string().c_str());
            return false;
        }

        const std::vector<char> bytes{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        if(file.bad())
        {
            std::fprintf(stderr, "read failed for %s\n", path.string().c_str());
            return false;
        }

        LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        return true;
    }
}

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        std::fprintf(stderr, "usage: %s <file-or-directory>...\n", argv[0]);
        return 2;
    }

    size_t count = 0;
    for(int i = 1; i < argc; i++)
    {
        std::error_code ec;
        const std::filesystem::path target(argv[i]);

        if(std::filesystem::is_directory(target, ec))
        {
            for(const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(target, ec))
            {
                if(!entry.is_regular_file(ec))
                    continue;
                if(!RunFile(entry.path()))
                    return 1;
                count++;
            }
            if(ec)
            {
                std::fprintf(stderr, "cannot walk %s\n", target.string().c_str());
                return 1;
            }
            continue;
        }

        if(!RunFile(target))
            return 1;
        count++;
    }

    // fail when corpus wiring produced no inputs
    if(count == 0)
    {
        std::fprintf(stderr, "no inputs found\n");
        return 1;
    }

    std::printf("replayed %zu inputs\n", count);
    return 0;
}
