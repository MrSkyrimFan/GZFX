cmake_minimum_required(VERSION 3.25)

project(
    GZFX
    VERSION 0.0.1
    DESCRIPTION "Standalone native graphics framework for Skyrim SE/AE"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "GZFX currently supports Windows only.")
endif()

add_library(GZFX SHARED
    src/dllmain.cpp
    src/version.cpp
)

target_include_directories(GZFX
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_features(GZFX PRIVATE cxx_std_20)

target_compile_definitions(GZFX
    PRIVATE
        GZFX_BUILD_DLL
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
)

if(MSVC)
    target_compile_options(GZFX PRIVATE
        /W4
        /WX
        /permissive-
        /EHsc
        /utf-8
    )
endif()

set_target_properties(GZFX PROPERTIES
    OUTPUT_NAME "GZFX"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(GZFX PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/bin"
    )
endforeach()
