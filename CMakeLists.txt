# crackdown - ReXGlue Recompiled Project
#
# SDK wiring for generated/default lives in cmake/rexglue_pregenerated.cmake.

cmake_minimum_required(VERSION 3.25)
project(crackdown VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where tools/codegen.ps1 and tools/package-linux.sh unpack the pinned SDK.
if(WIN32)
    set(CRACKDOWN_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")
else()
    set(CRACKDOWN_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/linux-amd64")
endif()
list(APPEND CMAKE_PREFIX_PATH "${CRACKDOWN_SDK_DIR}")

get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo|Release)$")
    message(FATAL_ERROR "Set CMAKE_BUILD_TYPE to Debug, RelWithDebInfo or Release (the presets and tools/build.ps1 "
        "do): the ReXGlue runtime and GPU plugin are checked per configuration")
endif()

# Release binaries must not carry the build machine's folders (__FILE__ in log calls).
add_compile_options("$<$<CONFIG:Release>:-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>")

include(cmake/rexglue_pregenerated.cmake)

# TRG Launcher: the third_party/trg-launcher submodule.
set(_trg_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/trg-launcher")
if(NOT EXISTS "${_trg_dir}/CMakeLists.txt")
    message(FATAL_ERROR "TRG Launcher not found: run `git submodule update --init third_party/trg-launcher`")
endif()

# The launcher draws inside the runtime's own Dear ImGui frame, so it must use the ImGui compiled into rexruntime
# (one context), never a downloaded copy.
add_library(crackdown_imgui INTERFACE)
target_link_libraries(crackdown_imgui INTERFACE rex::runtime)
set(TRG_LAUNCHER_IMGUI_TARGET crackdown_imgui CACHE STRING "" FORCE)
set(TRG_LAUNCHER_STANDALONE OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_FETCH_DEPS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_trg_dir}" trg-launcher)
# Written for C++17: in C++20 and later u8string() is a different type.
set_target_properties(trg_launcher PROPERTIES CXX_STANDARD 17)
# TRG defines this only for cl.exe; clang++ needs it too for the MSVC CRT.
target_compile_definitions(trg_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
get_directory_property(CRACKDOWN_TRG_VERSION DIRECTORY "${_trg_dir}" DEFINITION PROJECT_VERSION)

set(CRACKDOWN_SOURCES
    src/main.cpp
    src/achievements_input.cpp
    src/achievements_overlay.cpp
    src/aspect_render.cpp
    src/audio_mixer_priority.cpp
    src/audio_pcm_firewall.cpp
    src/audio_wave_timeout.cpp
    src/cache_device.cpp
    src/cheat_hooks.cpp
    src/cheat_menu_input.cpp
    src/cheat_menu_overlay.cpp
    src/cheats.cpp
    src/crt_native.cpp
    src/frame_counter.cpp
    src/frame_pacing.cpp
    src/frame_rate.cpp
    src/game_files.cpp
    src/gpu_fence_wait.cpp
    src/graphics_adapters.cpp
    src/guest_fault_log.cpp
    src/launcher.cpp
    src/launcher_achievements.cpp
    src/launcher_achievements_page.cpp
    src/massive_server_guard.cpp
    src/platform.cpp
    src/settings.cpp
    src/settings_file.cpp
    src/title_compat.cpp
    src/title_language.cpp
    src/user_data.cpp
    src/window_host.cpp
    src/xdvdfs.cpp
    src/skip_intro.cpp
)

if(WIN32)
    # Resource 1 is the first icon in the executable: Explorer, and SDL's window class, use it. The icon is
    # generated from packaging/icons/app-icon.svg by tools/make-app-icon.py and is committed to the repository.
    enable_language(RC)
    set(CRACKDOWN_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/app-icon.ico")
    if(EXISTS "${CRACKDOWN_ICON}")
        set(CRACKDOWN_RC_ICON_LINE "1 ICON \"${CRACKDOWN_ICON}\"")
    else()
        set(CRACKDOWN_RC_ICON_LINE "")
        message(STATUS "No packaging/icons/app-icon.ico: the executable gets no icon (run tools/make-app-icon.py)")
    endif()
    string(REPLACE "\\" "/" CRACKDOWN_RC_ICON_LINE "${CRACKDOWN_RC_ICON_LINE}")
    set(CRACKDOWN_VERSION_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    configure_file(packaging/windows/crackdown.rc.in "${CMAKE_CURRENT_BINARY_DIR}/crackdown.rc" @ONLY)
    if(EXISTS "${CRACKDOWN_ICON}")
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/crackdown.rc" PROPERTIES OBJECT_DEPENDS "${CRACKDOWN_ICON}")
    endif()
    list(APPEND CRACKDOWN_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/crackdown.rc")
    add_executable(crackdown WIN32 ${CRACKDOWN_SOURCES})
else()
    add_executable(crackdown ${CRACKDOWN_SOURCES})
endif()

rexglue_setup_target(crackdown GPU_PLUGINS xenos)
target_link_libraries(crackdown PRIVATE trg::launcher)
target_compile_definitions(crackdown PRIVATE
    CRACKDOWN_PORT_VERSION="${PROJECT_VERSION}"
    CRACKDOWN_TRG_VERSION="${CRACKDOWN_TRG_VERSION}")

if(WIN32)
    target_link_libraries(crackdown PRIVATE shcore synchronization dxgi)
    # The SDK stages its DLLs only when the program links, so a rebuilt runtime or GPU plugin has to make it link
    # again; the check and staging targets below and tools/build.ps1 also verify the staged copies.
    foreach(_sdk_dll rexruntime rexruntimed rexruntimerd rexgpu-xenos rexgpu-xenosd rexgpu-xenosrd)
        if(EXISTS "${CRACKDOWN_SDK_DIR}/bin/${_sdk_dll}.dll")
            set_property(TARGET crackdown APPEND PROPERTY LINK_DEPENDS "${CRACKDOWN_SDK_DIR}/bin/${_sdk_dll}.dll")
        endif()
    endforeach()
else()
    # The SDK stages the runtime beside the program only on Windows and macOS; the RPATH is $ORIGIN.
    add_custom_command(TARGET crackdown POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:crackdown>"
        VERBATIM
    )
    foreach(_sdk_lib rexruntime rexruntimed rexruntimerd rexgpu-xenos rexgpu-xenosd rexgpu-xenosrd)
        if(EXISTS "${CRACKDOWN_SDK_DIR}/lib/lib${_sdk_lib}.so")
            set_property(TARGET crackdown APPEND PROPERTY LINK_DEPENDS "${CRACKDOWN_SDK_DIR}/lib/lib${_sdk_lib}.so")
        endif()
    endforeach()
endif()

# Every build checks that tools/rexglue holds the runtime and GPU plugin built from the patches, and that the
# copies next to the program are those; tools/build.ps1 and tools/build.sh rebuild them when they are not.
if(WIN32)
    # Windows PowerShell has a fixed path, unlike a Store pwsh; the module path a parent pwsh sets breaks it.
    find_program(CRACKDOWN_WINDOWS_POWERSHELL NAMES powershell REQUIRED)
    set(CRACKDOWN_REXGLUE_CHECK "${CMAKE_COMMAND}" -E env --unset=PSModulePath
        "${CRACKDOWN_WINDOWS_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-patched-rexruntime.ps1" -Check -Config $<CONFIG>)
    set(CRACKDOWN_REXGLUE_STAGED -Staged)
else()
    find_program(CRACKDOWN_BASH bash REQUIRED)
    set(CRACKDOWN_REXGLUE_CHECK "${CRACKDOWN_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-patched-rexruntime.sh"
        --check "${CRACKDOWN_SDK_DIR}" --config $<CONFIG>)
    set(CRACKDOWN_REXGLUE_STAGED --staged)
endif()
add_custom_target(crackdown_rexglue_check COMMAND ${CRACKDOWN_REXGLUE_CHECK} VERBATIM)
add_dependencies(crackdown crackdown_rexglue_check)
add_custom_target(crackdown_rexglue_staging ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE:rex::gpu-xenos>"
        "$<TARGET_FILE_DIR:crackdown>"
    COMMAND ${CRACKDOWN_REXGLUE_CHECK} ${CRACKDOWN_REXGLUE_STAGED} "$<TARGET_FILE_DIR:crackdown>"
    VERBATIM
)
add_dependencies(crackdown_rexglue_staging crackdown)

# ThinLTO in Release and RelWithDebInfo; the runtime and GPU plugin are prebuilt. Debug compiles at -O0, which marks
# every function optnone, so LTO would optimize nothing there.
function(crackdown_enable_thinlto target)
    set(_lto "$<CONFIG:Release,RelWithDebInfo>")
    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        # lld-link runs the LTO backend at -O2 unless told otherwise; Release compiles at -O3.
        set(_link -flto=thin "$<$<CONFIG:Release>:-Wl,/opt:lldlto=3>"
            "-Wl,/lldltocache:${CMAKE_BINARY_DIR}/lto-cache")
    elseif(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(_link -flto=thin -fuse-ld=lld "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/lto-cache")
    else()
        return()
    endif()
    target_compile_options(${target} PRIVATE "$<$<AND:${_lto},$<COMPILE_LANGUAGE:CXX>>:-flto=thin>")
    get_target_property(_type ${target} TYPE)
    if(NOT _type STREQUAL "OBJECT_LIBRARY")
        target_link_options(${target} PRIVATE "$<${_lto}:${_link}>")
    endif()
endfunction()
crackdown_enable_thinlto(crackdown)
if(TARGET crackdown_recomp)
    crackdown_enable_thinlto(crackdown_recomp)
endif()

# The runtime reads its cvars from <exe dir>/crackdown.toml.
add_custom_command(TARGET crackdown POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/crackdown.toml"
        "$<TARGET_FILE_DIR:crackdown>"
    VERBATIM
)

# Runtime-linked tests, not part of the default build. Each copies the runtime next to itself; tools/test.ps1 runs the
# pure-logic tests in tests/ without the runtime.
function(crackdown_add_runtime_test name source)
    add_executable(${name} EXCLUDE_FROM_ALL "tests/${source}")
    target_link_libraries(${name} PRIVATE rex::runtime)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:${name}>"
        VERBATIM
    )
    if(NOT WIN32)
        set_target_properties(${name} PROPERTIES BUILD_RPATH "$ORIGIN")
    endif()
endfunction()
crackdown_add_runtime_test(crackdown_physical_heap_stress physical_heap_stress.cpp)
crackdown_add_runtime_test(crackdown_title_thread_shutdown_tests title_thread_shutdown_tests.cpp)
crackdown_add_runtime_test(crackdown_shutdown_order_tests shutdown_order_tests.cpp)

# RenderDoc detection test (Linux; the fake library goes into renderdoc/, out of reach of the runtime's RUNPATH).
if(UNIX AND NOT APPLE)
    add_library(crackdown_fake_renderdoc SHARED EXCLUDE_FROM_ALL tests/fake_renderdoc.cpp)
    set_target_properties(crackdown_fake_renderdoc PROPERTIES OUTPUT_NAME renderdoc
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/renderdoc")
    crackdown_add_runtime_test(crackdown_renderdoc_detect_tests renderdoc_detect_tests.cpp)
    target_link_libraries(crackdown_renderdoc_detect_tests PRIVATE rex::renderdoc ${CMAKE_DL_LIBS})
    add_dependencies(crackdown_renderdoc_detect_tests crackdown_fake_renderdoc)
endif()

# Writes the decrypted guest image of the supported XEX (the input of tools/make-app-icon.py and of the analysis
# tools): cmake --build <dir> --target crackdown_dump_image
add_executable(crackdown_dump_image EXCLUDE_FROM_ALL tools/dump_image/main.cpp)
target_link_libraries(crackdown_dump_image PRIVATE rex::runtime)
add_custom_command(TARGET crackdown_dump_image POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:crackdown_dump_image>"
    VERBATIM
)
