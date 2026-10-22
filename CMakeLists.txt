cmake_minimum_required(VERSION 3.25)

project(
    GZFX
    VERSION 0.0.2
    DESCRIPTION "Standalone native graphics framework for Skyrim SE/AE"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "GZFX currently supports Windows only.")
endif()

function(gzfx_apply_common target)
    target_compile_features(${target} PRIVATE cxx_std_20)

    target_compile_definitions(${target}
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            UNICODE
            _UNICODE
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX
            /permissive-
            /EHsc
            /utf-8
        )
    endif()

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(${target} PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/bin"
        )
    endforeach()
endfunction()

add_library(GZFX SHARED
    src/dllmain.cpp
    src/runtime.cpp
    src/version.cpp
)

target_include_directories(GZFX
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(GZFX PRIVATE GZFX_BUILD_DLL)
target_link_libraries(GZFX PRIVATE shell32 ole32)
set_target_properties(GZFX PROPERTIES OUTPUT_NAME "GZFX")
gzfx_apply_common(GZFX)

add_library(GZFXBootstrap SHARED
    bootstrap/dinput8_proxy.cpp
    bootstrap/dinput8.def
)

set_target_properties(GZFXBootstrap PROPERTIES OUTPUT_NAME "dinput8")
gzfx_apply_common(GZFXBootstrap)

add_executable(GZFXSmokeHost
    tests/smoke_host.cpp
)

gzfx_apply_common(GZFXSmokeHost)

enable_testing()

add_test(
    NAME runtime_smoke
    COMMAND
        "$<TARGET_FILE:GZFXSmokeHost>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/test-output"
)
