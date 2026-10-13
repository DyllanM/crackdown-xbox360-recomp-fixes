# crackdown - ReXGlue Recompiled Project
#
# SDK wiring for generated/default lives in cmake/rexglue_pregenerated.cmake.

cmake_minimum_required(VERSION 3.25)
project(crackdown LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where tools/codegen.ps1 unpacks the pinned SDK.
set(CRACKDOWN_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")
list(APPEND CMAKE_PREFIX_PATH "${CRACKDOWN_SDK_DIR}")

include(cmake/rexglue_pregenerated.cmake)

# Sources
set(CRACKDOWN_SOURCES
    src/main.cpp
    src/cache_device.cpp
    src/fullscreen_toggle.cpp
    src/debug_saulo_diagnostics.cpp
)

if(WIN32)
    add_executable(crackdown WIN32 ${CRACKDOWN_SOURCES})
else()
    add_executable(crackdown ${CRACKDOWN_SOURCES})
endif()

rexglue_setup_target(crackdown GPU_PLUGINS xenos)

# The SDK stages its DLLs in a POST_BUILD step, which runs only when crackdown relinks.
set_property(TARGET crackdown APPEND PROPERTY LINK_DEPENDS
    "$<TARGET_FILE:rex::runtime>"
    "$<TARGET_FILE:rex::gpu-xenos>")

# The runtime reads its cvars from <exe dir>/crackdown.toml.
add_custom_command(TARGET crackdown POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/crackdown.toml"
        "$<TARGET_FILE_DIR:crackdown>"
    VERBATIM
)
