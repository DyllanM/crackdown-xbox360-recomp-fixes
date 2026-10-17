# The tracker header ships in patches/rexglue-sdk; the test needs the patched source tree.
set(_invalid_fetch_include "${CMAKE_CURRENT_SOURCE_DIR}/../out/rexglue-sdk-src/include")
if(EXISTS "${_invalid_fetch_include}/rex/graphics/pipeline/texture/invalid_fetch_log.h")
    crackdown_add_test(invalid_fetch_log_tests SOURCES invalid_fetch_log_tests.cpp)
    target_include_directories(invalid_fetch_log_tests PRIVATE "${_invalid_fetch_include}")
endif()
