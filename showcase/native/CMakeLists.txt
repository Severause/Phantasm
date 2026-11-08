cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
endif()

project(
    PhantasmShowcase
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# Engine-free self-tests: the rule units under src/core compiled with plain asserts into one console exe, no game and
# no CommonLib. build.ps1 -Tests turns them on and runs them.
option(PS_BUILD_TESTS "Build the engine-free self-tests" OFF)

if(PS_BUILD_TESTS)
    add_executable(ShowcaseTests tests/TestMain.cpp tests/CoreTests.cpp)
    target_include_directories(ShowcaseTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(ShowcaseTests PRIVATE NOMINMAX)
    if(MSVC)
        target_compile_options(ShowcaseTests PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
        set_property(TARGET ShowcaseTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# CommonLibSSE-NG through the overlay port in ports/commonlibsse-ng, a byte copy of Phantasm's (native/ports), so the
# two share vcpkg's binary cache. Its exported targets re-export DirectXTK, DirectXMath, fmt, spdlog and xbyak, so each
# must be findable here.
find_package(directxtk CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(xbyak CONFIG REQUIRED)
find_package(CommonLibSSE CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(PLUGIN_NAME "${PROJECT_NAME}")
set(PLUGIN_AUTHOR "Severause")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY
)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/Runtime.cpp
    src/Settings.cpp
    src/Ui.cpp
    src/Bridge.cpp
    src/Views.cpp
    src/Shots.cpp
    src/People.cpp
    src/Items.cpp
    src/Anims.cpp
    src/TryOn.cpp
    src/Underlay.cpp
    src/ImGuiPanel.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PLUGIN_NAME="${PLUGIN_NAME}"
    PLUGIN_VERSION="${PROJECT_VERSION}"
    PLUGIN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PLUGIN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PLUGIN_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    # CommonLibVR pulls in windows.h, whose min/max macros break std::min / std::max.
    NOMINMAX
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    # Magelight's public API headers (MIT, extern/magelight/LICENSE.txt), copied unchanged.
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/magelight
    # Phantasm's API header, this repository's own.
    ${CMAKE_CURRENT_SOURCE_DIR}/../../api
    # PrismaUI's API header, as its Nexus distribution hands it to modders to copy.
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/prismaui
)

# SKSE Menu Framework 3's API header (MIT, extern/skse-menu-framework/LICENSE.txt), copied unchanged from
# QTR-Modding/SKSE-Menu-Framework-3-Example aa8effa; a system include, so its own warnings stay out of the build.
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/extern/skse-menu-framework)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
    nlohmann_json::nlohmann_json
    delayimp.lib
    # WIC writes the pictures as PNG (Shots.cpp).
    windowscodecs.lib
    ole32.lib
    # The views drawn under a PrismaUI page (Underlay.cpp).
    d3d11.lib
    d3dcompiler.lib
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4
        /permissive-
        /Zc:preprocessor
        /external:anglebrackets
        /external:W0
        /utf-8
        /Zi
    )
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(${PROJECT_NAME} PRIVATE
        # CommonLibVR links openvr_api.lib for the universal SE/AE/VR build; openvr_api.dll ships only with Skyrim
        # VR, so it is bound on first use instead of at load.
        /DELAYLOAD:openvr_api.dll
        # A PDB with Release codegen kept (/OPT restores the folding /DEBUG turns off), to read crash logs.
        /DEBUG
        /OPT:REF
        /OPT:ICF
        # The DLL names its PDB by file name only, not the build folder's path; debuggers look beside the DLL.
        /PDBALTPATH:%_PDB%
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PLUGIN_NAME}" SUFFIX ".dll")
