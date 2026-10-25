cmake_minimum_required(VERSION 3.25)

project(
    GZFX
    VERSION 0.0.26
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
    src/input_bridge.cpp
    src/paths.cpp
    src/runtime.cpp
    src/settings.cpp
    src/ui/settings_menu.cpp
    src/version.cpp
    src/diagnostics/d3d11_lifecycle.cpp
    src/diagnostics/deferred_context_cartography.cpp
    src/diagnostics/engine_epochs.cpp
    src/diagnostics/pipeline_cartography.cpp
    src/diagnostics/renderer_forensics.cpp
    src/diagnostics/resource_intelligence.cpp
    src/diagnostics/semantic_probe.cpp
    src/diagnostics/telemetry_listener.cpp
    src/render/runtime_state.cpp
    src/render/semantic_authority.cpp
    src/render/stage_scheduler.cpp
    src/render/renderer_execution_service.cpp
    src/render/native_input_broker.cpp
    src/render/wave3_pipeline.cpp
    src/render/ambient_visibility_v2.cpp
    src/render/ambient_visibility_stack.cpp
    src/render/scene_effects.cpp
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
        gdi32
        version
        d3dcompiler
        dxguid
        minhook
)
set_target_properties(GZFX PROPERTIES OUTPUT_NAME "GZFX")
gzfx_apply_common(GZFX)

add_library(GZFXBootstrap SHARED
    bootstrap/dinput8_proxy.cpp
    bootstrap/dinput8.def
)
target_link_libraries(
    GZFXBootstrap
    PRIVATE
        minhook
        dxguid
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

add_executable(GZFXEngineEpochHarness
    tests/engine_epoch_harness.cpp
)
gzfx_apply_common(GZFXEngineEpochHarness)

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

add_executable(GZFXMenuOnlyHarness
    tests/menu_only_harness.cpp
)
target_link_libraries(GZFXMenuOnlyHarness PRIVATE d3d11 dxgi user32)
gzfx_apply_common(GZFXMenuOnlyHarness)

add_executable(GZFXPreUiEffectsHarness
    tests/pre_ui_effects_harness.cpp
)
target_link_libraries(GZFXPreUiEffectsHarness PRIVATE d3d11 dxgi user32)
gzfx_apply_common(GZFXPreUiEffectsHarness)

add_executable(GZFXAmbientDepthFunctionalHarness
    tests/ambient_depth_functional_harness.cpp
)
target_link_libraries(GZFXAmbientDepthFunctionalHarness PRIVATE d3d11 dxgi user32)
gzfx_apply_common(GZFXAmbientDepthFunctionalHarness)

add_executable(GZFXAmbientVisibilityNeutralityHarness
    tests/ambient_visibility_neutrality_harness.cpp
)
target_link_libraries(
    GZFXAmbientVisibilityNeutralityHarness
    PRIVATE
        d3d11
        dxgi
        user32
)
gzfx_apply_common(GZFXAmbientVisibilityNeutralityHarness)

add_executable(GZFXSemanticProbeHarness
    tests/semantic_probe_harness.cpp
)
target_link_libraries(
    GZFXSemanticProbeHarness
    PRIVATE
        d3d11
        d3dcompiler
        dxgi
        user32
)
gzfx_apply_common(GZFXSemanticProbeHarness)

add_executable(GZFXSemanticAuthorityHarness
    tests/semantic_authority_harness.cpp
)
target_link_libraries(
    GZFXSemanticAuthorityHarness
    PRIVATE
        d3d11
        d3dcompiler
        dxgi
        user32
)
gzfx_apply_common(GZFXSemanticAuthorityHarness)

add_executable(GZFXNativeSurfaceTemporalHarness
    tests/native_surface_temporal_harness.cpp
)
target_link_libraries(
    GZFXNativeSurfaceTemporalHarness
    PRIVATE
        d3d11
        dxgi
        user32
)
gzfx_apply_common(GZFXNativeSurfaceTemporalHarness)

add_executable(GZFXWave3MathHarness
    tests/wave3_math_harness.cpp
)
target_include_directories(
    GZFXWave3MathHarness
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
gzfx_apply_common(GZFXWave3MathHarness)

add_executable(GZFXWave3PipelineHarness
    tests/wave3_pipeline_harness.cpp
)
target_link_libraries(
    GZFXWave3PipelineHarness
    PRIVATE
        d3d11
        dxgi
        user32
)
gzfx_apply_common(GZFXWave3PipelineHarness)

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
    NAME engine_epoch_signatures
    COMMAND
        "$<TARGET_FILE:GZFXEngineEpochHarness>"
        "$<TARGET_FILE:GZFX>"
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

add_test(
    NAME menu_only
    COMMAND
        "$<TARGET_FILE:GZFXMenuOnlyHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/menu-only-output"
)

add_test(
    NAME pre_ui_effects
    COMMAND
        "$<TARGET_FILE:GZFXPreUiEffectsHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/pre-ui-effects-output"
)

add_test(
    NAME ambient_depth_ao
    COMMAND
        "$<TARGET_FILE:GZFXAmbientDepthFunctionalHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/ambient-depth-ao-output"
        "ao"
)

add_test(
    NAME ambient_depth_ssil
    COMMAND
        "$<TARGET_FILE:GZFXAmbientDepthFunctionalHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/ambient-depth-ssil-output"
        "ssil"
)

add_test(
    NAME ambient_visibility_flat_ao
    COMMAND
        "$<TARGET_FILE:GZFXAmbientVisibilityNeutralityHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/ambient-visibility-flat-ao-output"
        "flat_ao"
)

add_test(
    NAME ambient_visibility_uniform_ssil
    COMMAND
        "$<TARGET_FILE:GZFXAmbientVisibilityNeutralityHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/ambient-visibility-uniform-ssil-output"
        "uniform_ssil"
)


add_test(
    NAME renderer_semantic_probe
    COMMAND
        "$<TARGET_FILE:GZFXSemanticProbeHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/semantic-probe-output"
)


add_test(
    NAME semantic_authority_foundation
    COMMAND
        "$<TARGET_FILE:GZFXSemanticAuthorityHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/semantic-authority-output"
)

add_test(
    NAME native_surface_temporal
    COMMAND
        "$<TARGET_FILE:GZFXNativeSurfaceTemporalHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/native-surface-temporal-output"
)

add_test(
    NAME wave3_math
    COMMAND
        "$<TARGET_FILE:GZFXWave3MathHarness>"
)

add_test(
    NAME wave3_pipeline
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/wave3-pipeline-output"
)

add_test(
    NAME ambient_visibility_v2
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/ambient-visibility-v2-output"
)

add_test(
    NAME telemetry_listener
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/telemetry-listener-output"
)

set_tests_properties(
    telemetry_listener
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_TELEMETRY_ACTIVE=1"
)

add_test(
    NAME telemetry_listener_overflow
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/telemetry-listener-overflow-output"
)

set_tests_properties(
    telemetry_listener_overflow
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_TELEMETRY_ACTIVE=1;GZFX_TEST_TELEMETRY_RING_CAPACITY=64;GZFX_TEST_TELEMETRY_FORCE_OVERFLOW=1;GZFX_TEST_TELEMETRY_EXPECT_OVERFLOW=1"
)

add_test(
    NAME late_stack_cartography
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/late-stack-cartography-output"
)

set_tests_properties(
    late_stack_cartography
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_LATE_STACK_ACTIVE=1"
)
