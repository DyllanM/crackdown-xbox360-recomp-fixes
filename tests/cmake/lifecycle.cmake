crackdown_add_test(lifecycle_tests SOURCES lifecycle_tests.cpp)
target_compile_definitions(lifecycle_tests PRIVATE CRACKDOWN_PATCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../patches/rexglue-sdk")
