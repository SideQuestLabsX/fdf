cmake_minimum_required(VERSION 3.28.3...3.31)


option(FDF_USE_CPP_MODULES                 "Enable C++ modules" OFF)
option(FDF_NO_COMMENTS                     "Disable comment processing" OFF)
option(FDF_EXTENDED_NO_COMMENT_IDENTIFIERS "Use extended identifier size limit when comments are disabled (otherwise keep standard limit)" OFF)
option(FDF_DISABLE_SLAB_ALLOCATOR          "Bypass slab allocation and use operator new/delete directly" OFF)
option(FDF_NO_FILE_IO                       "Disable filesystem-based parsing and writing" OFF)
option(FDF_NO_STD_FORMAT                    "Disable fdf's std::formatter specialization for fdf::String" OFF)
option(FDF_ENABLE_ASAN                     "Enable AddressSanitizer (bundles LeakSanitizer on Linux)" OFF)
option(FDF_ENABLE_LSAN                     "Enable standalone LeakSanitizer (non-MSVC)" OFF)
option(FDF_BUILD_FUZZERS                   "Build the libFuzzer targets (Clang only)" OFF)
set(FDF_ASSERTIONS "auto" CACHE STRING "Library assertions: auto (follow NDEBUG), on, off")
set_property(CACHE FDF_ASSERTIONS PROPERTY STRINGS auto on off)


project(fdf VERSION 0.2.0 LANGUAGES CXX)

# one CRT policy for every target below (fdf, tests, fuzz, tools): StaticRuntimes sets
# CMAKE_MSVC_RUNTIME_LIBRARY at root scope so subdirectories inherit it. Included at tests/
# scope before, so it never reached the fdf_validate target and that binary linked the DLL CRT
if(PROJECT_IS_TOP_LEVEL)
    include(FetchContent)
    FetchContent_Declare(common_resources
      GIT_REPOSITORY https://github.com/SideQuestLabsX/common.git
      GIT_TAG        48b58cf109cde83127086450efa578354caec054)
    FetchContent_MakeAvailable(common_resources)

    list(APPEND CMAKE_MODULE_PATH "${common_resources_SOURCE_DIR}/cmake")
    include(CompilerWarnings)
    include(StaticRuntimes)

    # Release ships stripped; RelWithDebInfo keeps the symbols for debugging
    if(NOT MSVC)
        add_compile_options($<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
        add_link_options($<$<CONFIG:Release>:-s> $<$<CONFIG:Release>:-Wl,--gc-sections>)
    endif()
endif()

# computed here so every subdirectory target picks it up, not just the tests
set(FDF_SANITIZER_FLAGS "")
if(FDF_ENABLE_ASAN OR FDF_ENABLE_LSAN)
    if(MSVC)
        if(FDF_ENABLE_ASAN)
            list(APPEND FDF_SANITIZER_FLAGS /fsanitize=address)
        endif()
        if(FDF_ENABLE_LSAN)
            message(WARNING "FDF_ENABLE_LSAN ignored: MSVC has no standalone LeakSanitizer")
        endif()
    elseif(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL GNU)
        message(FATAL_ERROR
                "FDF_ENABLE_ASAN/LSAN: MinGW GCC ships no sanitizer runtime (libasan). "
                "Use MSVC or Clang for sanitizers, or disable these options.")
    else()
        set(FDF_SANITIZE_LIST "")
        if(FDF_ENABLE_ASAN)
            list(APPEND FDF_SANITIZE_LIST address)
        endif()
        if(FDF_ENABLE_LSAN AND NOT FDF_ENABLE_ASAN)
            list(APPEND FDF_SANITIZE_LIST leak)
        endif()
        list(JOIN FDF_SANITIZE_LIST "," FDF_SANITIZE_ARG)
        list(APPEND FDF_SANITIZER_FLAGS -fsanitize=${FDF_SANITIZE_ARG} -fno-omit-frame-pointer)
    endif()
endif()


if(FDF_USE_CPP_MODULES)
    add_library(fdf STATIC)
    
    if(MSVC)
        target_compile_features(fdf PUBLIC cxx_std_23)
        target_compile_options(fdf PUBLIC /std:c++latest)
    else()
        target_compile_features(fdf PUBLIC cxx_std_26)
    endif()

    target_include_directories(fdf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_sources(fdf
        PRIVATE
            include/fdf.h
        PUBLIC
            FILE_SET my_modules
            TYPE CXX_MODULES
            FILES src/fdf.ixx
    )
    # FDF_ASSERT's optional message uses __VA_OPT__, which MSVC honors only in conforming mode
    if(MSVC)
        target_compile_options(fdf PUBLIC /Zc:preprocessor)
    endif()

    target_compile_definitions(fdf PUBLIC FDF_USE_CPP_MODULES=true)
    target_compile_definitions(fdf PUBLIC FDF_NO_COMMENTS=$<IF:$<BOOL:${FDF_NO_COMMENTS}>,true,false>)
    target_compile_definitions(fdf PUBLIC FDF_EXTENDED_NO_COMMENT_IDENTIFIERS=$<IF:$<BOOL:${FDF_EXTENDED_NO_COMMENT_IDENTIFIERS}>,true,false>)
    target_compile_definitions(fdf PUBLIC FDF_DISABLE_SLAB_ALLOCATOR=$<IF:$<BOOL:${FDF_DISABLE_SLAB_ALLOCATOR}>,true,false>)
    target_compile_definitions(fdf PUBLIC FDF_NO_FILE_IO=$<IF:$<BOOL:${FDF_NO_FILE_IO}>,true,false>)
    target_compile_definitions(fdf PUBLIC FDF_NO_STD_FORMAT=$<IF:$<BOOL:${FDF_NO_STD_FORMAT}>,true,false>)

else()
    add_library(fdf INTERFACE)
    target_include_directories(fdf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_sources(fdf INTERFACE include/fdf.h)

    # FDF_ASSERT's optional message uses __VA_OPT__, which MSVC honors only in conforming mode
    if(MSVC)
        target_compile_options(fdf INTERFACE /Zc:preprocessor)
    endif()

    target_compile_definitions(fdf INTERFACE FDF_USE_CPP_MODULES=false)
    target_compile_definitions(fdf INTERFACE FDF_NO_COMMENTS=$<IF:$<BOOL:${FDF_NO_COMMENTS}>,true,false>)
    target_compile_definitions(fdf INTERFACE FDF_EXTENDED_NO_COMMENT_IDENTIFIERS=$<IF:$<BOOL:${FDF_EXTENDED_NO_COMMENT_IDENTIFIERS}>,true,false>)
    target_compile_definitions(fdf INTERFACE FDF_DISABLE_SLAB_ALLOCATOR=$<IF:$<BOOL:${FDF_DISABLE_SLAB_ALLOCATOR}>,true,false>)
    target_compile_definitions(fdf INTERFACE FDF_NO_FILE_IO=$<IF:$<BOOL:${FDF_NO_FILE_IO}>,true,false>)
    target_compile_definitions(fdf INTERFACE FDF_NO_STD_FORMAT=$<IF:$<BOOL:${FDF_NO_STD_FORMAT}>,true,false>)

endif()


if(NOT FDF_ASSERTIONS STREQUAL "auto")
    if(FDF_ASSERTIONS STREQUAL "on")
        set(FDF_ASSERTIONS_VALUE true)
    elseif(FDF_ASSERTIONS STREQUAL "off")
        set(FDF_ASSERTIONS_VALUE false)
    else()
        message(FATAL_ERROR "FDF_ASSERTIONS must be auto, on or off (got '${FDF_ASSERTIONS}')")
    endif()

    if(FDF_USE_CPP_MODULES)
        target_compile_definitions(fdf PUBLIC FDF_ASSERTIONS=${FDF_ASSERTIONS_VALUE})
    else()
        target_compile_definitions(fdf INTERFACE FDF_ASSERTIONS=${FDF_ASSERTIONS_VALUE})
    endif()
endif()


if(PROJECT_IS_TOP_LEVEL)
    if(FDF_USE_CPP_MODULES)
        target_compile_definitions(fdf PUBLIC FDF_TESTING)
    else()
        target_compile_definitions(fdf INTERFACE FDF_TESTING)
    endif()

    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(fuzz)
    add_subdirectory(tools)
endif()
