cmake_minimum_required(VERSION 3.28.3...3.31)


if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()


option(FDF_USE_CPP_MODULES                 "Enable C++ modules" OFF)
option(FDF_NO_COMMENTS                     "Disable comment processing" OFF)
option(FDF_EXTENDED_NO_COMMENT_IDENTIFIERS "Use extended identifier size limit when comments are disabled (otherwise keep standard limit)" OFF)
option(FDF_DISABLE_SLAB_ALLOCATOR          "Bypass slab allocation and use operator new/delete directly" OFF)
option(FDF_ENABLE_ASAN                     "Enable AddressSanitizer (bundles LeakSanitizer on Linux)" OFF)
option(FDF_ENABLE_LSAN                     "Enable standalone LeakSanitizer (non-MSVC)" OFF)


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
    target_compile_definitions(fdf PUBLIC FDF_DISABLE_SLAB_ALLOCATOR=$<IF:$<BOOL:${FDF_DISABLE_SLAB_ALLOCATOR}>,true,false>)

else()
    add_library(fdf INTERFACE)
    target_include_directories(fdf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_sources(fdf INTERFACE include/fdf.h)

    target_compile_definitions(fdf INTERFACE FDF_USE_CPP_MODULES=false)
    target_compile_definitions(fdf INTERFACE FDF_NO_COMMENTS=$<IF:$<BOOL:${FDF_NO_COMMENTS}>,true,false>)
    target_compile_definitions(fdf INTERFACE FDF_EXTENDED_NO_COMMENT_IDENTIFIERS=$<IF:$<BOOL:${FDF_EXTENDED_NO_COMMENT_IDENTIFIERS}>,true,false>)
    target_compile_definitions(fdf INTERFACE FDF_DISABLE_SLAB_ALLOCATOR=$<IF:$<BOOL:${FDF_DISABLE_SLAB_ALLOCATOR}>,true,false>)

endif()


if(PROJECT_IS_TOP_LEVEL)
    if(FDF_USE_CPP_MODULES)
        target_compile_definitions(fdf PUBLIC FDF_TESTING)
    else()
        target_compile_definitions(fdf INTERFACE FDF_TESTING)
    endif()

    enable_testing()
    add_subdirectory(tests)
endif()
