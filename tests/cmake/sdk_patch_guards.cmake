crackdown_add_test(sdk_patch_guards_tests SOURCES sdk_patch_guards_tests.cpp)
target_compile_definitions(sdk_patch_guards_tests PRIVATE CRACKDOWN_PATCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../patches/rexglue-sdk")
