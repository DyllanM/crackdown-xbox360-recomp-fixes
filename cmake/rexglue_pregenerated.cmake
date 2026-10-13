# Compile wiring for the sources codegen wrote to generated/default.
# generated/rexglue.cmake adds a codegen rule that reruns during cmake --build,
# so it is not included. tools/codegen.ps1 is the only codegen entry point.

find_package(rexglue 0.10.0 REQUIRED CONFIG)

include(${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake)
set(REXGLUE_ENTRYPOINT_GENERATED_SOURCES ${GENERATED_SOURCES})
set(REXGLUE_ENTRYPOINT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated/default")

# Guest SEH scopes need asynchronous EH to trap hardware faults.
set(REXGLUE_RECOMP_OPTIONS "")
if(WIN32)
    if(MSVC)
        list(APPEND REXGLUE_RECOMP_OPTIONS /EHa)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND REXGLUE_RECOMP_OPTIONS -fasync-exceptions)
    endif()
endif()
list(APPEND REXGLUE_RECOMP_OPTIONS
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-gline-tables-only>)

set_source_files_properties(${REXGLUE_ENTRYPOINT_GENERATED_SOURCES}
    PROPERTIES COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS}")

# clang rejects a PCH whose EH mode differs from the including TU, so the options go on the target too.
function(rexglue_apply_recomp_settings target_name pch_header)
    target_precompile_headers(${target_name} PRIVATE "${pch_header}")
    target_compile_options(${target_name} PRIVATE ${REXGLUE_RECOMP_OPTIONS})
endfunction()

macro(rexglue_setup_target target_name)
    if(REXGLUE_ENTRYPOINT_GENERATED_SOURCES)
        add_library(${target_name}_recomp OBJECT ${REXGLUE_ENTRYPOINT_GENERATED_SOURCES})
        target_include_directories(${target_name}_recomp PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${REXGLUE_ENTRYPOINT_INCLUDE_DIR}
        )
        target_link_libraries(${target_name}_recomp PRIVATE rex::runtime)
        rexglue_apply_target_settings(${target_name}_recomp)
        rexglue_apply_recomp_settings(${target_name}_recomp
            "${REXGLUE_ENTRYPOINT_INCLUDE_DIR}/crackdown_pch.h")
        target_link_libraries(${target_name} PRIVATE ${target_name}_recomp)
    endif()
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${REXGLUE_ENTRYPOINT_INCLUDE_DIR}
    )
    target_link_libraries(${target_name} PRIVATE rex::runtime)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/metadata/icons")
        rexglue_embed_metadata(${target_name}
            DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/metadata/icons"
            PREFIX "icons")
    endif()
    rexglue_configure_target(${target_name} ${ARGN})
endmacro()
