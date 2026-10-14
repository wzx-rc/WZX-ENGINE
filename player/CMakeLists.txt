# WZX Player: the exported game on every target, drawn by the multi-API Lit renderer (src/lit) on Diligent Engine.
#   Windows : DirectX 12          cmake -S player -B build/player && cmake --build build/player --config Release
#   iOS     : Metal (MoltenVK)    cmake -S player -B build/ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DMOLTENVK_LIBRARY=.../libMoltenVK.a
#   Linux   : Vulkan (test only, renders headless with Mesa lavapipe). Use clang: Diligent built with gcc -O2/-O3
#             loses shader resources (seen with gcc 13).
# The editor (build.bat) is not built here and keeps its own DirectX 12 renderer for path tracing and DDGI.
cmake_minimum_required(VERSION 3.21)
project(WZXPlayer CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(WZX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# ---------------------------------------------------------------- Diligent Engine (core only, pinned)
# Only the backends of our targets: DirectX 12 (Windows) and Vulkan (MoltenVK on iOS, Linux tests).
set(DILIGENT_NO_OPENGL ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_METAL ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_WEBGPU ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_ARCHIVER ON CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DILIGENT_INSTALL_CORE OFF CACHE BOOL "" FORCE)
include(FetchContent)
# A local checkout can be used instead of the download: -DFETCHCONTENT_SOURCE_DIR_DILIGENTCORE=path
FetchContent_Declare(DiligentCore
    GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentCore.git
    GIT_TAG v2.5.6
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(DiligentCore)
FetchContent_GetProperties(DiligentCore SOURCE_DIR DILIGENT_CORE_DIR)

# ---------------------------------------------------------------- engine code shared with the editor
add_library(wzx_engine STATIC
    ${WZX_ROOT}/src/vp3d/Scene.cpp
    ${WZX_ROOT}/src/vp3d/MeshGen.cpp
    ${WZX_ROOT}/src/vp3d/EnvImage.cpp
    ${WZX_ROOT}/src/vp3d/AssetImport.cpp
    ${WZX_ROOT}/src/vp3d/LitPrep.cpp
    ${WZX_ROOT}/src/app/Content.cpp
    ${WZX_ROOT}/src/lit/LitRenderer.cpp
    ${WZX_ROOT}/src/game/Graph.cpp
    ${WZX_ROOT}/src/game/NodeLib.cpp
    ${WZX_ROOT}/src/game/Runtime.cpp
    ${WZX_ROOT}/src/game/Grid.cpp
    ${WZX_ROOT}/src/game/Placement.cpp
    ${WZX_ROOT}/src/game/RtsCamera.cpp)
target_include_directories(wzx_engine PUBLIC ${WZX_ROOT}/src ${DILIGENT_CORE_DIR})
target_compile_definitions(wzx_engine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(wzx_engine PUBLIC Diligent-GraphicsEngine Diligent-GraphicsTools)
if(MSVC)
    target_compile_options(wzx_engine PUBLIC /utf-8)
endif()

# Dear ImGui (third_party/imgui, the editor's copy): the HUD of the game
add_library(wzx_imgui STATIC
    ${WZX_ROOT}/third_party/imgui/imgui.cpp
    ${WZX_ROOT}/third_party/imgui/imgui_draw.cpp
    ${WZX_ROOT}/third_party/imgui/imgui_tables.cpp
    ${WZX_ROOT}/third_party/imgui/imgui_widgets.cpp)
target_include_directories(wzx_imgui PUBLIC ${WZX_ROOT}/third_party/imgui)
target_compile_definitions(wzx_imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
if(APPLE)
    # ImageIO decodes the textures (jpg, png) on iOS (vp3d/AssetImport.cpp)
    target_link_libraries(wzx_engine PUBLIC "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation")
endif()

# Game data next to the executable (Windows) or in the app bundle (iOS): the graph and the house model
set(WZX_GAME_FILES ${WZX_ROOT}/game/main.graph.json)
file(GLOB WZX_HOUSE_FILES ${WZX_ROOT}/assets/HOUSE/*)
set(WZX_PLAYER_SOURCES src/Player.cpp src/Game.cpp src/ImGuiDiligent.cpp)
function(wzx_copy_data target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target}>/game $<TARGET_FILE_DIR:${target}>/assets/HOUSE
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WZX_GAME_FILES} $<TARGET_FILE_DIR:${target}>/game
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WZX_HOUSE_FILES} $<TARGET_FILE_DIR:${target}>/assets/HOUSE)
endfunction()

set(WZX_BACKENDS)
if(D3D12_SUPPORTED)
    list(APPEND WZX_BACKENDS Diligent-GraphicsEngineD3D12-static)
endif()
if(VULKAN_SUPPORTED)
    list(APPEND WZX_BACKENDS Diligent-GraphicsEngineVk-static)
endif()
if(NOT WZX_BACKENDS)
    message(FATAL_ERROR "No graphics backend: DirectX 12 (Windows) or Vulkan (MoltenVK on iOS) is required.")
endif()

# Shader sources read at run time by Diligent (compiled for the backend of the device)
set(WZX_SHADERS
    ${WZX_ROOT}/src/vp3d/shaders/Raster.hlsl
    ${WZX_ROOT}/src/vp3d/shaders/Shared.h
    ${WZX_ROOT}/src/vp3d/shaders/ToneCurves.hlsli
    ${WZX_ROOT}/src/lit/shaders/LitPost.hlsl)

function(wzx_copy_shaders target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target}>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WZX_SHADERS} $<TARGET_FILE_DIR:${target}>/shaders)
endfunction()

# ---------------------------------------------------------------- headless image (parity tests), desktop only
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(WZXPlayerHeadless src/main_headless.cpp ${WZX_PLAYER_SOURCES})
    target_include_directories(WZXPlayerHeadless PRIVATE ${DILIGENT_CORE_DIR}/ThirdParty/stb)
    target_link_libraries(WZXPlayerHeadless PRIVATE wzx_engine wzx_imgui ${WZX_BACKENDS})
    wzx_copy_shaders(WZXPlayerHeadless)
    wzx_copy_data(WZXPlayerHeadless)
endif()

# ---------------------------------------------------------------- windowed player
if(WIN32)
    add_executable(WZXPlayer WIN32 src/main_win32.cpp ${WZX_PLAYER_SOURCES})
    target_link_libraries(WZXPlayer PRIVATE wzx_engine wzx_imgui ${WZX_BACKENDS})
    wzx_copy_shaders(WZXPlayer)
    wzx_copy_data(WZXPlayer)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(WZXPlayer MACOSX_BUNDLE ios/main_ios.mm ${WZX_PLAYER_SOURCES} ${WZX_SHADERS})
    target_include_directories(WZXPlayer PRIVATE src)
    set_source_files_properties(${WZX_SHADERS} PROPERTIES MACOSX_PACKAGE_LOCATION shaders)
    wzx_copy_data(WZXPlayer)   # the .app folder: game/ and assets/HOUSE beside the executable (resourcePath)
    set_target_properties(WZXPlayer PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER com.wzx.player
        MACOSX_BUNDLE_BUNDLE_NAME "WZX Game"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.wzx.player
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
    target_compile_options(WZXPlayer PRIVATE -fobjc-arc)
    target_link_libraries(WZXPlayer PRIVATE wzx_engine wzx_imgui ${WZX_BACKENDS}
        "-framework UIKit" "-framework QuartzCore" "-framework Metal" "-framework IOSurface" "-framework CoreGraphics")
endif()
