# The recovery policy header ships in patches/rexglue-sdk; the test needs the patched source tree.
set(_save_recovery_include "${CMAKE_CURRENT_SOURCE_DIR}/../out/rexglue-sdk-src/include")
if(EXISTS "${_save_recovery_include}/rex/system/guest_page_recovery.h")
    crackdown_add_test(save_recovery_tests SOURCES save_recovery_tests.cpp)
    target_include_directories(save_recovery_tests PRIVATE "${_save_recovery_include}")
endif()
