cmake_minimum_required(VERSION 3.24)

project(StarCraftCombat LANGUAGES CXX)

# OpenBW's headers use std::result_of/std::is_literal_type, so this wrapper
# project must compile them in C++17 mode even though SparCraft itself uses C++23.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/EHsc)
endif()

# Put all executable outputs in <repo>/bin, matching SparCraft.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
if(CMAKE_CONFIGURATION_TYPES)
  foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${config}" config_upper)
    set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_SOURCE_DIR}/bin")
  endforeach()
endif()

set(STARCRAFT_COMBAT_ROOT "${CMAKE_SOURCE_DIR}")
set(STARCRAFT_COMBAT_SRC_DIR "${CMAKE_SOURCE_DIR}/src")

set(SFML_ROOT "C:/dev/libraries/SFML-3.0.1" CACHE PATH "Path to SFML 3")
list(APPEND CMAKE_PREFIX_PATH "${SFML_ROOT}")
if(DEFINED ENV{SFML_DIR})
  list(APPEND CMAKE_PREFIX_PATH "$ENV{SFML_DIR}")
endif()

find_package(SFML 3 COMPONENTS System Window Graphics REQUIRED)
find_package(OpenGL REQUIRED)

add_library(StarCraftCombatCore STATIC
  "${STARCRAFT_COMBAT_SRC_DIR}/GameState.cpp"
  "${STARCRAFT_COMBAT_SRC_DIR}/Scripts.hpp"
)

target_include_directories(StarCraftCombatCore
  PUBLIC
    "${STARCRAFT_COMBAT_SRC_DIR}"
)

target_compile_definitions(StarCraftCombatCore
  PUBLIC
    NOMINMAX
)

if(MSVC)
  set_target_properties(StarCraftCombatCore PROPERTIES
    VS_GLOBAL_VcpkgEnabled false
    VS_GLOBAL_VcpkgApplocalDeps false
  )
  target_compile_options(StarCraftCombatCore PRIVATE /bigobj)
endif()

add_executable(StarCraftCombatVisualizer
  "${STARCRAFT_COMBAT_SRC_DIR}/Renderer.cpp"
  "${STARCRAFT_COMBAT_SRC_DIR}/main.cpp"
  "${STARCRAFT_COMBAT_SRC_DIR}/imgui/imgui.cpp"
  "${STARCRAFT_COMBAT_SRC_DIR}/imgui/imgui_draw.cpp"
  "${STARCRAFT_COMBAT_SRC_DIR}/imgui/imgui_tables.cpp"
  "${STARCRAFT_COMBAT_SRC_DIR}/imgui/imgui_widgets.cpp"
  "${STARCRAFT_COMBAT_SRC_DIR}/imgui/imgui-SFML.cpp"
)

set_target_properties(StarCraftCombatVisualizer PROPERTIES
  OUTPUT_NAME StarCraftCombat
  UNITY_BUILD ON
)

set_source_files_properties(
  "${STARCRAFT_COMBAT_SRC_DIR}/imgui/imgui.cpp"
  "${STARCRAFT_COMBAT_SRC_DIR}/imgui/imgui_draw.cpp"
  "${STARCRAFT_COMBAT_SRC_DIR}/imgui/imgui_tables.cpp"
  "${STARCRAFT_COMBAT_SRC_DIR}/imgui/imgui_widgets.cpp"
  "${STARCRAFT_COMBAT_SRC_DIR}/imgui/imgui-SFML.cpp"
  PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

if(MSVC)
  set_target_properties(StarCraftCombatVisualizer PROPERTIES
    VS_GLOBAL_VcpkgEnabled false
    VS_GLOBAL_VcpkgApplocalDeps false
  )
endif()

target_include_directories(StarCraftCombatVisualizer
  PRIVATE
    "${STARCRAFT_COMBAT_SRC_DIR}"
    "${STARCRAFT_COMBAT_SRC_DIR}/imgui"
)

target_compile_definitions(StarCraftCombatVisualizer
  PRIVATE
    STARCRAFT_COMBAT_ROOT="${STARCRAFT_COMBAT_ROOT}"
)

if(MSVC)
  target_compile_options(StarCraftCombatVisualizer PRIVATE /bigobj)
endif()

target_link_libraries(StarCraftCombatVisualizer
  PRIVATE
    StarCraftCombatCore
    SFML::System
    SFML::Window
    SFML::Graphics
    OpenGL::GL
)
