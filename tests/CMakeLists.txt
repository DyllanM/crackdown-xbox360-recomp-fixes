# Pure-logic tests: no game files, no ReXGlue runtime. tools/test.ps1 and tools/test.sh configure and run this project.

cmake_minimum_required(VERSION 3.25)
project(crackdown_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    set(_crackdown_sdk_platform win-amd64)
else()
    set(_crackdown_sdk_platform linux-amd64)
endif()
set(CRACKDOWN_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tools/rexglue/${_crackdown_sdk_platform}" CACHE PATH "Unpacked ReXGlue SDK")
set(CRACKDOWN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

enable_testing()
add_custom_target(crackdown_tests)

# crackdown_add_test(<name> SOURCES <files...> [SDK_HEADERS])
function(crackdown_add_test name)
    cmake_parse_arguments(ARG "SDK_HEADERS" "" "SOURCES" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE "${CRACKDOWN_SRC_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${name} PRIVATE CRACKDOWN_SRC_DIR="${CRACKDOWN_SRC_DIR}")
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    if(ARG_SDK_HEADERS)
        target_include_directories(${name} SYSTEM PRIVATE "${CRACKDOWN_SDK_DIR}/include")
    endif()
    add_test(NAME ${name} COMMAND ${name})
    add_dependencies(crackdown_tests ${name})
endfunction()

file(GLOB _fragments CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake")
foreach(_fragment ${_fragments})
    include("${_fragment}")
endforeach()
