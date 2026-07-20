
include(FetchContent)

FetchContent_Declare(common_resources
  GIT_REPOSITORY https://github.com/SideQuestLabsX/common.git
  GIT_TAG        48b58cf109cde83127086450efa578354caec054)
FetchContent_MakeAvailable(common_resources)

list(APPEND CMAKE_MODULE_PATH "${common_resources_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(StaticRuntimes)








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
