crackdown_add_test(cursor_hider_tests SOURCES cursor_hider_tests.cpp SDK_HEADERS)
target_compile_definitions(cursor_hider_tests PRIVATE CRACKDOWN_PATCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../patches/rexglue-sdk")
