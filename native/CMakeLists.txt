cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
endif()

project(
    Phantasm
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
option(PHANTASM_BUILD_TESTS "Build the engine-free self-tests" OFF)

if(PHANTASM_BUILD_TESTS)
    add_executable(PhantasmTests
        tests/TestMain.cpp
        tests/ActorCollectCoreTest.cpp
        tests/AsciiTextTest.cpp
        tests/CastingArtCoreTest.cpp
        tests/CompanionCutoutCoreTest.cpp
        tests/ControllerKeysCoreTest.cpp
        tests/FaceMorphCoreTest.cpp
        tests/HkxPackfileCoreTest.cpp
        tests/ImageHostProbeCoreTest.cpp
        tests/ItemCollectCoreTest.cpp
        tests/LastCaptureCoreTest.cpp
        tests/LivePhysicsCoreTest.cpp
        tests/LiveTextureCacheCoreTest.cpp
        tests/LiveViewBudgetCoreTest.cpp
        tests/LiveViewLifecycleCoreTest.cpp
        tests/LiveViewSizingCoreTest.cpp
        tests/FreezeCoreTest.cpp
        tests/MannequinClipCoreTest.cpp
        tests/MannequinPhysicsCoreTest.cpp
        tests/MannequinSpringCoreTest.cpp
        tests/MirrorCameraCoreTest.cpp
        tests/MirrorLightingCoreTest.cpp
        tests/NifParticleCoreTest.cpp
        tests/PresentHandoffCoreTest.cpp
        tests/RendererFxCoreTest.cpp
        tests/RendererMaterialCoreTest.cpp
        tests/RestPoseCoreTest.cpp
        tests/ShaderContractCoreTest.cpp
        tests/StillShotCoreTest.cpp
        tests/ViewRegistryCoreTest.cpp
    )
    target_include_directories(PhantasmTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../api
    )
    # ShaderContractCoreTest compiles the renderer's own shader text, read from its source file.
    target_compile_definitions(PhantasmTests PRIVATE NOMINMAX
        PHANTASM_RENDERER_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/MannequinRenderer.cpp")
    target_link_libraries(PhantasmTests PRIVATE d3dcompiler.lib)
    if(MSVC)
        target_compile_options(PhantasmTests PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
        set_property(TARGET PhantasmTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# CommonLibSSE-NG (MinLL/CommonLibVR v4.39.5, MIT, the overlay port in ports/commonlibsse-ng; projects that keep a byte
# copy of the port share vcpkg's binary cache). Its exported targets re-export DirectXTK, DirectXMath, fmt, spdlog and
# xbyak, so each must be findable here.
find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
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
    src/Api.cpp
    src/Host.cpp
    src/MannequinRenderer.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PLUGIN_NAME="${PLUGIN_NAME}"
    PLUGIN_VERSION="${PROJECT_VERSION}"
    PLUGIN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PLUGIN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PLUGIN_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    PHANTASM_EXPORTS
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    # CommonLibVR pulls in windows.h, whose min/max macros break std::min / std::max.
    NOMINMAX
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    # Magelight's public API headers (MIT; extern/magelight/LICENSE.txt), copied unchanged from the host's api/.
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/magelight
    # Phantasm's own public API header, the one clients copy.
    ${CMAKE_CURRENT_SOURCE_DIR}/../api
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
    nlohmann_json::nlohmann_json
    Microsoft::DirectXTex
    d3dcompiler.lib
    delayimp.lib
)

# The development probe: a second SKSE plugin that is a Phantasm client through the public header only, with its own
# Magelight page (testclient/). Never shipped.
option(PHANTASM_BUILD_PROBE "Build the PhantasmProbe test client" ON)
if(PHANTASM_BUILD_PROBE)
    add_library(PhantasmProbe SHARED ../testclient/src/Probe.cpp)
    target_compile_definitions(PhantasmProbe PRIVATE NOMINMAX _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
    target_include_directories(PhantasmProbe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/magelight
        ${CMAKE_CURRENT_SOURCE_DIR}/../api
    )
    target_link_libraries(PhantasmProbe PRIVATE CommonLibSSE::CommonLibSSE nlohmann_json::nlohmann_json delayimp.lib)
endif()

if(MSVC)
    set(PHANTASM_DLL_TARGETS ${PROJECT_NAME})
    if(PHANTASM_BUILD_PROBE)
        list(APPEND PHANTASM_DLL_TARGETS PhantasmProbe)
    endif()
    foreach(target IN LISTS PHANTASM_DLL_TARGETS)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:preprocessor
            /external:anglebrackets
            /external:W0
            /utf-8
            /Zi
        )
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_link_options(${target} PRIVATE
            # CommonLibVR links openvr_api.lib for the universal SE/AE/VR build; openvr_api.dll ships only with
            # Skyrim VR, so it is bound on first use instead of at load.
            /DELAYLOAD:openvr_api.dll
            # A PDB with Release codegen kept (/OPT restores the folding /DEBUG turns off), to read crash logs.
            /DEBUG
            /OPT:REF
            /OPT:ICF
            # The DLL names its PDB by file name only, not the build folder's path; debuggers look beside the DLL.
            /PDBALTPATH:%_PDB%
        )
        set_target_properties(${target} PROPERTIES SUFFIX ".dll")
    endforeach()
endif()
