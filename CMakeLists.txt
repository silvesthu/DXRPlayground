cmake_minimum_required(VERSION 3.25)

# Visual Studio generators: restore NuGet package references before build.
set(CMAKE_VS_NUGET_PACKAGE_RESTORE ON CACHE BOOL "Auto-restore NuGet packages for VS generators" FORCE)

project(DXRPlayground LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "DXRPlayground currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build DirectXTex as a dependency target and link it normally.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(BUILD_DX11 OFF CACHE BOOL "" FORCE)
add_subdirectory(Source/Thirdparty/DirectXTex)

# Build meshoptimizer as a dependency target.
set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(Source/Thirdparty/meshoptimizer)

file(GLOB_RECURSE DXRPLAYGROUND_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cxx"
)

# Exclude all third-party implementation sources.
list(FILTER DXRPLAYGROUND_SOURCES EXCLUDE REGEX "/Source/Thirdparty/")

# Keep selected third-party source(s) compiled.
list(APPEND DXRPLAYGROUND_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/ArHosekSkyModel/ArHosekSkyModel.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/ArPragueSkyModelGround/ArPragueSkyModelGround.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/imgui/imgui.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/imgui/imgui_draw.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/imgui/imgui_tables.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/imgui/imgui_widgets.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/implot/implot.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/implot/implot_items.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/tinyxml2/tinyxml2.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/Thirdparty.cpp"
)

file(GLOB_RECURSE DXRPLAYGROUND_PROJECT_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.inl"
  "${CMAKE_CURRENT_SOURCE_DIR}/Shader/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/Shader/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Shader/*.hlsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/Shader/*.hlsli"
)
list(APPEND DXRPLAYGROUND_PROJECT_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/dpi.manifest"
  "${CMAKE_CURRENT_SOURCE_DIR}/PostBuild.bat"
)

add_executable(DXRPlayground WIN32
  ${DXRPLAYGROUND_SOURCES}
  ${DXRPLAYGROUND_PROJECT_FILES}
)

# Keep project-only files visible in VS Solution Explorer, but never build them.
set_source_files_properties(
  ${DXRPLAYGROUND_PROJECT_FILES}
  PROPERTIES HEADER_FILE_ONLY ON
)

# VS-specific: prevent any shader helper files from being sent to fxc/dxc.
set(_DXRPLAYGROUND_HLSL_FILES)
foreach(_f IN LISTS DXRPLAYGROUND_PROJECT_FILES)
  if(_f MATCHES "\\.(hlsl|hlsli)$")
    list(APPEND _DXRPLAYGROUND_HLSL_FILES "${_f}")
  endif()
endforeach()
if(_DXRPLAYGROUND_HLSL_FILES)
  set_source_files_properties(
    ${_DXRPLAYGROUND_HLSL_FILES}
    PROPERTIES
      VS_TOOL_OVERRIDE "None"
      HEADER_FILE_ONLY ON
  )
endif()

target_include_directories(DXRPlayground PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Source
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/imgui
)

target_compile_definitions(DXRPlayground PRIVATE
  IMGUI_USER_CONFIG=\"../imgui_config.h\"
  _CRT_SECURE_NO_WARNINGS
  UNICODE
  _UNICODE
)

if(MSVC)
  target_compile_options(DXRPlayground PRIVATE
    /W4
    /WX
    /sdl
    /permissive-
    /MP
  )

  target_link_options(DXRPlayground PRIVATE
    /MANIFEST:EMBED
    /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/dpi.manifest
  )
endif()

target_link_directories(DXRPlayground PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/Thirdparty/nvapi/amd64
)

target_link_libraries(DXRPlayground PRIVATE
  DirectXTex
  meshoptimizer
  nvapi64.lib
)

# Keep NuGet settings from the original project via PackageReference.
set_property(TARGET DXRPlayground PROPERTY VS_PACKAGE_REFERENCES
  "Microsoft.Direct3D.D3D12_1.619.2;Microsoft.Direct3D.DXC_1.9.2602.17;WinPixEventRuntime_1.0.240308001"
)

target_compile_features(DXRPlayground PRIVATE cxx_std_20)

# Keep generated VS post-build commands aligned with the original vcxproj.
add_custom_command(TARGET DXRPlayground POST_BUILD
  COMMAND call "$(ProjectDir)..\\PostBuild.bat" "$(ProjectDir)" "$(TargetDir)"
  VERBATIM
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
  ${DXRPLAYGROUND_SOURCES}
  ${DXRPLAYGROUND_PROJECT_FILES}
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT DXRPlayground)
set_target_properties(DXRPlayground PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "$(TargetDir)"
)
