# The classifier header ships in patches/rexglue-sdk; the test needs the patched source tree.
set(_expected_miss_include "${CMAKE_CURRENT_SOURCE_DIR}/../out/rexglue-sdk-src/include")
if(EXISTS "${_expected_miss_include}/rex/filesystem/expected_miss.h")
    crackdown_add_test(expected_miss_tests SOURCES expected_miss_tests.cpp)
    target_include_directories(expected_miss_tests SYSTEM PRIVATE "${_expected_miss_include}")
endif()
