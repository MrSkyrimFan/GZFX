cmake_minimum_required(VERSION 3.25)
cmake_policy(SET CMP0091 NEW)

# Ship GZFX as a self-contained plugin with respect to the MSVC CRT.
# This removes clean-machine LoadLibrary failures caused by a missing
# separately installed VC++ redistributable.
set(
    CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CACHE STRING
    "Use the statically linked MSVC runtime"
    FORCE
)

project(
    GZFX
    VERSION 0.0.34
    DESCRIPTION "SKSE-hosted native graphics framework for Skyrim SE/AE"
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

# GZFX uses the current MIT CommonLib continuation as a typed Skyrim engine
# access plane. Pin the exact audited revision; do not float to a branch head.
set(ENABLE_SKYRIM_SE OFF CACHE BOOL "CommonLib: disable pre-AE SE" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "CommonLib: enable AE" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "CommonLib: disable VR" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "CommonLib: disable dependency tests" FORCE)
set(COMMONLIB_ENABLE_IPO OFF CACHE BOOL "CommonLib: avoid dependency LTCG" FORCE)
set(REX_OPTION_INI OFF CACHE BOOL "CommonLib: no INI helpers" FORCE)
set(REX_OPTION_JSON OFF CACHE BOOL "CommonLib: no JSON helpers" FORCE)
set(REX_OPTION_TOML OFF CACHE BOOL "CommonLib: no TOML helpers" FORCE)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "CommonLib: no Xbyak trampoline helper" FORCE)

FetchContent_Declare(
    commonlibsse
    GIT_REPOSITORY https://github.com/eddoursul/CommonLibSSE-GG.git
    GIT_TAG 2053e94fd1c147b36eae2b4338118552fba407e2
)
FetchContent_MakeAvailable(commonlibsse)

# CommonLibSSE-GG uses /MP in its own MSVC preset. Preserve that supported
# parallel compile mode when it is embedded under the GZFX build.
if(MSVC)
    target_compile_options(CommonLibSSE PRIVATE /MP)
endif()

function(gzfx_apply_common target)
    target_compile_features(${target} PRIVATE cxx_std_23)

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
    src/engine/commonlib_bridge.cpp
    src/process_lifecycle.cpp
    src/skse_bridge.cpp
    src/paths.cpp
    src/runtime.cpp
    src/settings.cpp
    src/ui/input_api.cpp
    src/ui/directinput_mouse.cpp
    src/ui/settings_menu.cpp
    src/version.cpp
    src/diagnostics/d3d11_lifecycle.cpp
    src/diagnostics/deferred_context_cartography.cpp
    src/diagnostics/engine_epochs.cpp
    src/diagnostics/pipeline_cartography.cpp
    src/diagnostics/renderer_forensics.cpp
    src/diagnostics/resource_intelligence.cpp
    src/diagnostics/semantic_probe.cpp
    src/diagnostics/startup_trace.cpp
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
        CommonLibSSE::CommonLibSSE
)
set_target_properties(GZFX PROPERTIES OUTPUT_NAME "GZFX")
gzfx_apply_common(GZFX)


add_executable(GZFXCommonLibContractHarness
    tests/commonlib_contract_harness.cpp
)
target_include_directories(
    GZFXCommonLibContractHarness
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    GZFXCommonLibContractHarness
    PRIVATE
        CommonLibSSE::CommonLibSSE
)
gzfx_apply_common(GZFXCommonLibContractHarness)

add_executable(GZFXDirectInputMouseContractHarness
    tests/directinput_mouse_contract_harness.cpp
)
target_include_directories(
    GZFXDirectInputMouseContractHarness
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    GZFXDirectInputMouseContractHarness
    PRIVATE
        dxguid
)
gzfx_apply_common(GZFXDirectInputMouseContractHarness)

add_executable(GZFXSmokeHost
    tests/smoke_host.cpp
)
gzfx_apply_common(GZFXSmokeHost)

add_executable(GZFXD3D11ProbeHarness
    tests/d3d11_probe_harness.cpp
)
target_link_libraries(GZFXD3D11ProbeHarness PRIVATE d3d11)
gzfx_apply_common(GZFXD3D11ProbeHarness)


add_executable(GZFXSKSEFrontDoorHarness
    tests/skse_frontdoor_harness.cpp
)
target_include_directories(
    GZFXSKSEFrontDoorHarness
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    GZFXSKSEFrontDoorHarness
    PRIVATE
        d3d11
        dxgi
        user32
)
add_dependencies(
    GZFXSKSEFrontDoorHarness
    GZFX
)
gzfx_apply_common(GZFXSKSEFrontDoorHarness)


add_executable(GZFXNaturalExitChild
    tests/natural_exit_child.cpp
)
target_include_directories(
    GZFXNaturalExitChild
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    GZFXNaturalExitChild
    PRIVATE
        d3d11
        dxgi
        user32
)
add_dependencies(
    GZFXNaturalExitChild
    GZFX
)
gzfx_apply_common(GZFXNaturalExitChild)

add_executable(GZFXNaturalExitHarness
    tests/natural_exit_harness.cpp
)
add_dependencies(
    GZFXNaturalExitHarness
    GZFXNaturalExitChild
)
gzfx_apply_common(GZFXNaturalExitHarness)


add_executable(GZFXStartupTraceHarness
    tests/startup_trace_harness.cpp
)
add_dependencies(
    GZFXStartupTraceHarness
    GZFX
)
gzfx_apply_common(GZFXStartupTraceHarness)

add_executable(GZFXRuntimeCatalogHarness
    tests/runtime_catalog_harness.cpp
)
add_dependencies(
    GZFXRuntimeCatalogHarness
    GZFX
)
gzfx_apply_common(GZFXRuntimeCatalogHarness)

add_executable(GZFXEngineEpochHarness
    tests/engine_epoch_harness.cpp
)
gzfx_apply_common(GZFXEngineEpochHarness)

add_executable(GZFXEngineEpochDiscoveryHarness
    tests/engine_epoch_discovery_harness.cpp
)
gzfx_apply_common(GZFXEngineEpochDiscoveryHarness)

add_executable(GZFXTelemetryDetailGateHarness
    tests/telemetry_detail_gate_harness.cpp
)
target_include_directories(
    GZFXTelemetryDetailGateHarness
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
gzfx_apply_common(GZFXTelemetryDetailGateHarness)

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
    NAME commonlib_engine_contract
    COMMAND
        "$<TARGET_FILE:GZFXCommonLibContractHarness>"
)

add_test(
    NAME directinput_mouse_contract
    COMMAND
        "$<TARGET_FILE:GZFXDirectInputMouseContractHarness>"
)

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
    NAME skse_single_dll_frontdoor
    COMMAND
        "$<TARGET_FILE:GZFXSKSEFrontDoorHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/skse-frontdoor-output"
)

set_tests_properties(
    skse_single_dll_frontdoor
    PROPERTIES
        TIMEOUT 45
)

add_test(
    NAME natural_process_exit_completion
    COMMAND
        "$<TARGET_FILE:GZFXNaturalExitHarness>"
        "$<TARGET_FILE:GZFXNaturalExitChild>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/natural-exit-output"
)

set_tests_properties(
    natural_process_exit_completion
    PROPERTIES
        TIMEOUT 45
)


add_test(
    NAME startup_trace_process_scope
    COMMAND
        "$<TARGET_FILE:GZFXStartupTraceHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/startup-trace-output"
)

add_test(
    NAME skyrim_runtime_catalog
    COMMAND
        "$<TARGET_FILE:GZFXRuntimeCatalogHarness>"
        "$<TARGET_FILE:GZFX>"
)

add_test(
    NAME engine_epoch_signatures
    COMMAND
        "$<TARGET_FILE:GZFXEngineEpochHarness>"
        "$<TARGET_FILE:GZFX>"
)

add_test(
    NAME engine_epoch_discovery_contract
    COMMAND
        "$<TARGET_FILE:GZFXEngineEpochDiscoveryHarness>"
        "$<TARGET_FILE:GZFX>"
)

add_test(
    NAME telemetry_physical_present_detail_gate
    COMMAND
        "$<TARGET_FILE:GZFXTelemetryDetailGateHarness>"
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

add_test(
    NAME deferred_telemetry_outside_detail_gate
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/deferred-outside-detail-output"
)

set_tests_properties(
    deferred_telemetry_outside_detail_gate
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_LATE_STACK_ACTIVE=1;GZFX_TEST_DEFERRED_OUTSIDE_DETAIL_ACTIVE=1"
)

add_test(
    NAME global_taa_without_hdr_completion
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/global-taa-without-hdr-output"
)

set_tests_properties(
    global_taa_without_hdr_completion
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_LATE_STACK_ACTIVE=1;GZFX_TEST_TAA_WITHOUT_HDR_ACTIVE=1"
)

add_test(
    NAME hdr_descendant_reduction_contract
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/hdr-descendant-reduction-output"
)

set_tests_properties(
    hdr_descendant_reduction_contract
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_LATE_STACK_ACTIVE=1;GZFX_TEST_HDR_DESCENDANT_ACTIVE=1"
)


add_test(
    NAME hdr_late_descendant_recovery_contract
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/hdr-late-descendant-recovery-output"
)

set_tests_properties(
    hdr_late_descendant_recovery_contract
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_LATE_STACK_ACTIVE=1;GZFX_TEST_HDR_LATE_DESCENDANT_ACTIVE=1"
)

add_test(
    NAME hdr_unrelated_fullres_rejected
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/hdr-unrelated-fullres-output"
)

set_tests_properties(
    hdr_unrelated_fullres_rejected
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_LATE_STACK_ACTIVE=1;GZFX_TEST_HDR_UNRELATED_FULLRES_ACTIVE=1"
)


add_test(
    NAME hdr_source_capacity_fourth_accepted
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/hdr-source-capacity-fourth-output"
)

set_tests_properties(
    hdr_source_capacity_fourth_accepted
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_LATE_STACK_ACTIVE=1;GZFX_TEST_HDR_CAPACITY_FOURTH_ACTIVE=1"
)

add_test(
    NAME hdr_source_capacity_fifth_rejected
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/hdr-source-capacity-fifth-output"
)

set_tests_properties(
    hdr_source_capacity_fifth_rejected
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_LATE_STACK_ACTIVE=1;GZFX_TEST_HDR_CAPACITY_FIFTH_ACTIVE=1"
)

add_test(
    NAME hdr_source_depth_three_accepted
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/hdr-source-depth-three-output"
)

set_tests_properties(
    hdr_source_depth_three_accepted
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_LATE_STACK_ACTIVE=1;GZFX_TEST_HDR_DEPTH_THREE_ACTIVE=1"
)

add_test(
    NAME hdr_source_depth_four_rejected
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/hdr-source-depth-four-output"
)

set_tests_properties(
    hdr_source_depth_four_rejected
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_LATE_STACK_ACTIVE=1;GZFX_TEST_HDR_DEPTH_FOUR_ACTIVE=1"
)

add_test(
    NAME hdr_source_frame_isolation
    COMMAND
        "$<TARGET_FILE:GZFXWave3PipelineHarness>"
        "$<TARGET_FILE:GZFX>"
        "${CMAKE_BINARY_DIR}/hdr-source-frame-isolation-output"
)

set_tests_properties(
    hdr_source_frame_isolation
    PROPERTIES
        ENVIRONMENT "GZFX_TEST_LATE_STACK_ACTIVE=1;GZFX_TEST_HDR_FRAME_ISOLATION_ACTIVE=1"
)
