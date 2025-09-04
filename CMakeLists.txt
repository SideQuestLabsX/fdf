cmake_minimum_required(VERSION 3.28.3...3.31)


if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()


option(FDF_USE_CPP_MODULES "Enable C++ modules" OFF)
option(FDF_NO_COMMENTS     "Disable comment processing" OFF)


project(fdf VERSION 0.1.0 LANGUAGES CXX)
if(FDF_USE_CPP_MODULES)
    add_library(fdf STATIC)
    target_compile_features(fdf PUBLIC cxx_std_23)
    target_include_directories(fdf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_sources(fdf
        PRIVATE
            include/fdf.h
        PRIVATE
            FILE_SET my_modules
            TYPE CXX_MODULES
            FILES src/fdf.ixx
    )
    target_compile_definitions(fdf PUBLIC FDF_USE_CPP_MODULES=true)
    target_compile_definitions(fdf PUBLIC FDF_NO_COMMENTS=$<IF:$<BOOL:${FDF_NO_COMMENTS}>,true,false>)

    target_compile_definitions(fdf PUBLIC FDF_ROOT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(fdf PUBLIC FDF_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests")
else()
    add_library(fdf INTERFACE)
    target_include_directories(fdf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_sources(fdf INTERFACE include/fdf.h)

    target_compile_definitions(fdf INTERFACE FDF_USE_CPP_MODULES=false)
    target_compile_definitions(fdf INTERFACE FDF_NO_COMMENTS=$<IF:$<BOOL:${FDF_NO_COMMENTS}>,true,false>)

    target_compile_definitions(fdf INTERFACE FDF_ROOT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(fdf INTERFACE FDF_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()


if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory(tests)
endif()
