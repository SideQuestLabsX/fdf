cmake_minimum_required(VERSION 3.28.3...3.31)


if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()


option(FDF_USE_CPP_MODULES                 "Enable C++ modules" OFF)
option(FDF_NO_COMMENTS                     "Disable comment processing" OFF)
option(FDF_EXTENDED_NO_COMMENT_IDENTIFIERS "Use extended identifier size limit when comments are disabled (otherwise keep standard limit)" OFF)
option(FDF_ENABLE_ASAN                     "Enable AddressSanitizer (bundles LeakSanitizer on Linux)" OFF)
option(FDF_ENABLE_LSAN                     "Enable standalone LeakSanitizer (non-MSVC)" OFF)


if(FDF_USE_CPP_MODULES)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.3")
        set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
    elseif(CMAKE_VERSION VERSION_GREATER_EQUAL "3.31")
        set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
    elseif(CMAKE_VERSION VERSION_GREATER_EQUAL "3.30")
        set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")
    else()
        message(FATAL_ERROR "FDF_USE_CPP_MODULES requires CMake >= 3.30 for `import std`")
    endif()
    set(CMAKE_CXX_MODULE_STD ON)
endif()

project(fdf VERSION 0.1.0 LANGUAGES CXX)
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
    target_compile_definitions(fdf PUBLIC FDF_USE_CPP_MODULES=true)
    target_compile_definitions(fdf PUBLIC FDF_NO_COMMENTS=$<IF:$<BOOL:${FDF_NO_COMMENTS}>,true,false>)
    target_compile_definitions(fdf PUBLIC FDF_EXTENDED_NO_COMMENT_IDENTIFIERS=$<IF:$<BOOL:${FDF_EXTENDED_NO_COMMENT_IDENTIFIERS}>,true,false>)

    target_compile_definitions(fdf PUBLIC FDF_ROOT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(fdf PUBLIC FDF_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests")

    if(PROJECT_IS_TOP_LEVEL)
        target_compile_definitions(fdf PUBLIC FDF_TESTING)
    endif()
else()
    add_library(fdf INTERFACE)
    target_include_directories(fdf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_sources(fdf INTERFACE include/fdf.h)

    target_compile_definitions(fdf INTERFACE FDF_USE_CPP_MODULES=false)
    target_compile_definitions(fdf INTERFACE FDF_NO_COMMENTS=$<IF:$<BOOL:${FDF_NO_COMMENTS}>,true,false>)
    target_compile_definitions(fdf INTERFACE FDF_EXTENDED_NO_COMMENT_IDENTIFIERS=$<IF:$<BOOL:${FDF_EXTENDED_NO_COMMENT_IDENTIFIERS}>,true,false>)

    target_compile_definitions(fdf INTERFACE FDF_ROOT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(fdf INTERFACE FDF_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()


if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory(tests)
endif()
