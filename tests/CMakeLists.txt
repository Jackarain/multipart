# ---------------------------------------------------------------------------
# Test suite for multipart
# ---------------------------------------------------------------------------

find_package(GTest QUIET)

if(NOT GTest_FOUND)
    message(STATUS "GTest not found, fetching via FetchContent...")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(googletest)
    set(GTEST_BOTH_LIBRARIES gtest_main)
endif()

set(MULTIPART_TESTS
    test_part
    test_decode
    test_decode_errors
    test_nested
    test_encode
    test_roundtrip
    test_lazy
    test_events
    test_helpers
    test_iterators)

foreach(test_name IN LISTS MULTIPART_TESTS)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name}
        PRIVATE multipart::multipart ${GTEST_BOTH_LIBRARIES})
    if(NOT MSVC)
        target_link_libraries(${test_name} PRIVATE pthread)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# ---------------------------------------------------------------------------
# -fno-exceptions / -fno-rtti verification target.
#
# test_no_exceptions deliberately does NOT use gtest. Building and running it
# proves that the header is usable without exceptions or RTTI.
# ---------------------------------------------------------------------------
add_executable(test_no_exceptions test_no_exceptions.cpp)
target_link_libraries(test_no_exceptions PRIVATE multipart::multipart)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_no_exceptions PRIVATE -fno-exceptions -fno-rtti)
elseif(MSVC)
    target_compile_options(test_no_exceptions PRIVATE /EHs-c- /GR-)
endif()
add_test(NAME test_no_exceptions COMMAND test_no_exceptions)