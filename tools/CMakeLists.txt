include(CheckIPOSupported)
check_ipo_supported(RESULT bIpoSupported OUTPUT ipoError)

add_executable(fdf_validate validate.cpp)
set_target_properties(fdf_validate PROPERTIES OUTPUT_NAME fdf-validate)

# ship without toolchain runtime DLLs
target_link_libraries(fdf_validate PRIVATE fdf SQ_CW SQ_SRT)
target_compile_definitions(fdf_validate PRIVATE FDF_VALIDATE_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(fdf_validate PRIVATE /std:c++latest)
    # fopen/strerror are the portable choice; silence MSVC's Annex-K deprecation push
    target_compile_definitions(fdf_validate PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_features(fdf_validate PRIVATE cxx_std_26)
endif()

# CMP0069 NEW makes unsupported IPO fatal without this guard
if(bIpoSupported)
    set_property(TARGET fdf_validate PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if((MINGW OR CMAKE_CXX_COMPILER_TARGET MATCHES "mingw") AND CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    target_link_libraries(fdf_validate PRIVATE stdc++exp)
endif()

if(FDF_SANITIZER_FLAGS)
    target_compile_options(fdf_validate PRIVATE ${FDF_SANITIZER_FLAGS})
    target_link_options(fdf_validate PRIVATE ${FDF_SANITIZER_FLAGS})
endif()

# new examples join after reconfiguring
file(GLOB FDF_EXAMPLE_FILES "${PROJECT_SOURCE_DIR}/examples/*.fdf")
add_test(NAME ValidateExamples COMMAND fdf_validate --round-trip ${FDF_EXAMPLE_FILES})

add_test(NAME ValidateUsage COMMAND fdf_validate)
set_tests_properties(ValidateUsage PROPERTIES WILL_FAIL TRUE)
