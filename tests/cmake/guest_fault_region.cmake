# The classifier header ships in patches/rexglue-sdk; the test needs the patched source tree.
set(_guest_fault_include "${CMAKE_CURRENT_SOURCE_DIR}/../out/rexglue-sdk-src/include")
if(EXISTS "${_guest_fault_include}/rex/system/guest_fault_region.h")
    crackdown_add_test(guest_fault_region_tests SOURCES guest_fault_region_tests.cpp)
    target_include_directories(guest_fault_region_tests PRIVATE "${_guest_fault_include}")
endif()
