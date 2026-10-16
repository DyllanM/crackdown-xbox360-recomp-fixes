# Game-data check and installer: SHA-256, XDVDFS reader, staging and swap. No game files needed.
find_package(Threads REQUIRED)

crackdown_add_test(installer_sha256_tests SOURCES sha256_tests.cpp)

crackdown_add_test(installer_tests SOURCES installer_tests.cpp ../src/game_files.cpp ../src/xdvdfs.cpp)
target_link_libraries(installer_tests PRIVATE Threads::Threads)
