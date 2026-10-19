crackdown_add_test(keyboard_tests SOURCES keyboard_tests.cpp SDK_HEADERS)
target_include_directories(keyboard_tests PRIVATE "${CRACKDOWN_SRC_DIR}/../third_party/trg-launcher/include")
