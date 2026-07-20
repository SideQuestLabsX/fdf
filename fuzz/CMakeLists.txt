# Replay targets run the corpus on every compiler
# LibFuzzer targets are opt-in and require Clang

set(FDF_FUZZ_TARGETS
    parse     FDF_FUZZ_TARGET_PARSE
    roundtrip FDF_FUZZ_TARGET_ROUNDTRIP
    combine   FDF_FUZZ_TARGET_COMBINE)

list(LENGTH FDF_FUZZ_TARGETS FDF_FUZZ_TARGET_COUNT)
math(EXPR FDF_FUZZ_LAST "${FDF_FUZZ_TARGET_COUNT} - 1")

foreach(i RANGE 0 ${FDF_FUZZ_LAST} 2)
    math(EXPR j "${i} + 1")
    list(GET FDF_FUZZ_TARGETS ${i} name)
    list(GET FDF_FUZZ_TARGETS ${j} define)

    add_executable(fuzz_replay_${name} fuzz_parse.cpp fuzz_replay.cpp)
    # match the test target's static runtime so ctest needs no toolchain DLLs
    target_link_libraries(fuzz_replay_${name} PRIVATE fdf SQ_CW SQ_SRT)
    target_compile_definitions(fuzz_replay_${name} PRIVATE ${define}=1)
    if(MSVC)
        target_compile_options(fuzz_replay_${name} PRIVATE /std:c++latest)
    else()
        target_compile_features(fuzz_replay_${name} PRIVATE cxx_std_26)
    endif()
    # the ASan job runs these as ctest cases, so they need the same sanitizer as the test target
    if(FDF_SANITIZER_FLAGS)
        target_compile_options(fuzz_replay_${name} PRIVATE ${FDF_SANITIZER_FLAGS})
        target_link_options(fuzz_replay_${name} PRIVATE ${FDF_SANITIZER_FLAGS})
    endif()

    if((MINGW OR CMAKE_CXX_COMPILER_TARGET MATCHES "mingw") AND CMAKE_CXX_COMPILER_ID STREQUAL GNU)
        target_link_libraries(fuzz_replay_${name} PRIVATE stdc++exp)
    endif()

    add_test(NAME FuzzReplay_${name}
             COMMAND fuzz_replay_${name} "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
endforeach()

if(NOT FDF_BUILD_FUZZERS)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "FDF_BUILD_FUZZERS needs Clang, found ${CMAKE_CXX_COMPILER_ID}")
endif()

# Windows CRT mismatch: clang_rt.fuzzer is static and the MSVC-targeting driver is dynamic
if(WIN32)
    message(FATAL_ERROR
            "FDF_BUILD_FUZZERS is not supported on a Windows target: the prebuilt libFuzzer runtime "
            "and the MSVC CRT disagree. Use WSL or Linux. The FuzzReplay_* ctest cases cover the "
            "same targets here.")
endif()

foreach(i RANGE 0 ${FDF_FUZZ_LAST} 2)
    math(EXPR j "${i} + 1")
    list(GET FDF_FUZZ_TARGETS ${i} name)
    list(GET FDF_FUZZ_TARGETS ${j} define)

    add_executable(fuzz_${name} fuzz_parse.cpp)
    target_link_libraries(fuzz_${name} PRIVATE fdf)
    target_compile_features(fuzz_${name} PRIVATE cxx_std_26)
    target_compile_definitions(fuzz_${name} PRIVATE ${define}=1)

    # catch non-crashing out-of-bounds reads
    target_compile_options(fuzz_${name} PRIVATE -fsanitize=fuzzer,address -fno-omit-frame-pointer -g)
    target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer,address)
endforeach()
