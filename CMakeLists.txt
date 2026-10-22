cmake_minimum_required(VERSION 3.25)

project(
    GZFX
    VERSION 0.0.6
    DESCRIPTION "Standalone native graphics framework for Skyrim SE/AE"
    LANGUAGES C CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "GZFX currently supports Windows only.")
endif()

include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(minhook)

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
    src/paths.cpp
    src/runtime.cpp
    src/version.cpp
    src/diagnostics/d3d11_lifecycle.cpp
    src/diagnostics/renderer_forensics.cpp
    src/diagnostics/resource_intelligence.cpp
    src/render/visible_pass.cpp
)

target_include_directories(GZFX
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(GZFX PRIVATE GZFX_BUILD_DLL)
target_link_libraries(
    GZFX
    PRIVATE
        shell32
        ole32
        user32
        d3dcompiler
        minhook
)
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

add_executable(GZFXD3D11ProbeHarness
    tests/d3d11_probe_harness.cpp
)
target_link_libraries(GZFXD3D11ProbeHarness PRIVATE d3d11)
gzfx_apply_common(GZFXD3D11ProbeHarness)

add_executable(GZFXRendererForensicsHarness
    tests/renderer_forensics_harness.cpp
)
target_link_libraries(GZFXRendererForensicsHarness PRIVATE d3d11 dxgi user32)
gzfx_apply_common(GZFXRendererForensicsHarness)

add_executable(GZFXResourceIntelligenceHarness
    tests/resource_intelligence_harness.cpp
)
target_link_libraries(GZFXResourceIntelligenceHarness PRIVATE d3d11 dxgi user32)
gzfx_apply_common(GZFXResourceIntelligenceHarness)

add_executable(GZFXVisiblePassHarness
    tests/visible_pass_harness.cpp
)
target_link_libraries(GZFXVisiblePassHarness PRIVATE d3d11 dxgi user32)
gzfx_apply_common(GZFXVisiblePassHarness)

enable_testing()

add_test(
    NAME runtime_smoke
    COMMAND
        "$<TARGET_FILE:GZFXSmokeHost>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/test-output"
)

add_test(
    NAME d3d11_lifecycle_probe
    COMMAND
        "$<TARGET_FILE:GZFXD3D11ProbeHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/probe-test-output"
)

add_test(
    NAME renderer_forensics
    COMMAND
        "$<TARGET_FILE:GZFXRendererForensicsHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/renderer-forensics-output"
)

add_test(
    NAME resource_intelligence
    COMMAND
        "$<TARGET_FILE:GZFXResourceIntelligenceHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/resource-intelligence-output"
)

add_test(
    NAME visible_pass
    COMMAND
        "$<TARGET_FILE:GZFXVisiblePassHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/visible-pass-output"
)
