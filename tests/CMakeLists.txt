
include(FetchContent)

FetchContent_Declare(common_resources
  GIT_REPOSITORY https://github.com/SideQuestLabsX/common.git
  GIT_TAG        48b58cf109cde83127086450efa578354caec054)
FetchContent_MakeAvailable(common_resources)

list(APPEND CMAKE_MODULE_PATH "${common_resources_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(StaticRuntimes)




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




function(add_fdf_test TEST_NAME SOURCES)
    add_executable(${TEST_NAME} ${SOURCES})
    target_link_libraries(${TEST_NAME} PRIVATE fdf)
    target_link_libraries(${TEST_NAME} PRIVATE SQ_CW SQ_SRT)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    
    if(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE /std:c++latest)
    else()
        target_compile_features(${TEST_NAME} PRIVATE cxx_std_26)
    endif()

    if((MINGW OR CMAKE_CXX_COMPILER_TARGET MATCHES "mingw") AND CMAKE_CXX_COMPILER_ID STREQUAL GNU)
        target_link_libraries(${TEST_NAME} PRIVATE stdc++exp)
    endif()

    if(FDF_SANITIZER_FLAGS)
        target_compile_options(${TEST_NAME} PRIVATE ${FDF_SANITIZER_FLAGS})
        target_link_options(${TEST_NAME} PRIVATE ${FDF_SANITIZER_FLAGS})
    endif()
endfunction()


add_fdf_test(Test test.cpp)

target_compile_definitions(Test PRIVATE
    FDF_ROOT_DIRECTORY="${PROJECT_SOURCE_DIR}"
    FDF_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}"
    FDF_OUTPUT_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/output"
)
