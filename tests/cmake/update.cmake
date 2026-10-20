# Launcher self-update: versions, release JSON, SHA-256, ZIP safety, staging, jobs and rollback.
set(_crackdown_updater_sources
    ../src/updater/github_release.cpp
    ../src/updater/install_transaction.cpp
    ../src/updater/json.cpp
    ../src/updater/sha256.cpp
    ../src/updater/update_job.cpp
    ../src/updater/update_paths.cpp
    ../src/updater/update_platform.cpp
    ../src/updater/update_zip.cpp
    ../src/updater/version.cpp
    ../src/updater/third_party/miniz/miniz.c
)
set_source_files_properties(../src/updater/third_party/miniz/miniz.c PROPERTIES COMPILE_OPTIONS "-w")
crackdown_add_test(update_tests SOURCES update_tests.cpp ${_crackdown_updater_sources})
if(WIN32)
    target_link_libraries(update_tests PRIVATE shell32)
endif()

if(WIN32)
    add_executable(trg-updater WIN32
        ../src/updater/helper_main.cpp
        ../src/updater/install_transaction.cpp
        ../src/updater/sha256.cpp
        ../src/updater/update_job.cpp
        ../src/updater/update_paths.cpp
        ../src/updater/update_platform.cpp
    )
    target_include_directories(trg-updater PRIVATE "${CRACKDOWN_SRC_DIR}")
    target_link_libraries(trg-updater PRIVATE shell32)
    target_link_options(trg-updater PRIVATE -static)
    set_property(TARGET trg-updater PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(trg-updater PRIVATE -Wall -Wextra)

    add_executable(update_restart_marker WIN32 update_restart_marker.cpp)
    target_link_libraries(update_restart_marker PRIVATE shell32)
    target_link_options(update_restart_marker PRIVATE -static)
    target_compile_options(update_restart_marker PRIVATE -Wall -Wextra)

    crackdown_add_test(update_helper_tests SOURCES update_helper_tests.cpp)
    target_compile_definitions(update_helper_tests PRIVATE
        CRACKDOWN_UPDATER_EXE="$<TARGET_FILE:trg-updater>"
        CRACKDOWN_RESTART_MARKER_EXE="$<TARGET_FILE:update_restart_marker>")
    add_dependencies(update_helper_tests trg-updater update_restart_marker)
endif()
