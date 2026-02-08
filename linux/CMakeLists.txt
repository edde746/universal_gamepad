# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to be incompatible with some combintions of Flutter, Linux,
# and CMake versions.
cmake_minimum_required(VERSION 3.10)

# Project-level configuration.
set(PROJECT_NAME "universal_gamepad")
project(${PROJECT_NAME} LANGUAGES CXX)

# This value is used when generating builds using this plugin, so it must
# not be changed.
set(PLUGIN_NAME "universal_gamepad_plugin")

# Option to bundle SDL3 from source instead of using system-installed SDL3.
option(BUNDLE_SDL3 "Fetch and statically link SDL3 from source" OFF)

# Find required dependencies.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0)

if(BUNDLE_SDL3)
  if(CMAKE_VERSION VERSION_LESS "3.11")
    message(FATAL_ERROR "BUNDLE_SDL3 requires CMake 3.11 or later (for FetchContent).")
  endif()

  include(FetchContent)
  FetchContent_Declare(
    SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.8/SDL3-3.2.8.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)

  set(SDL3_BUNDLED TRUE)
else()
  # Attempt to find SDL3 via pkg-config.
  pkg_check_modules(SDL3 IMPORTED_TARGET sdl3)

  if(NOT SDL3_FOUND)
    message(WARNING "SDL3 (sdl3) not found via pkg-config. "
      "The gamepad plugin will build but gamepad support will be disabled. "
      "Install SDL3 development libraries to enable gamepad support, "
      "or re-run with -DBUNDLE_SDL3=ON to build SDL3 from source.")
  endif()
endif()

# Define the plugin library target.
add_library(${PLUGIN_NAME} SHARED
  "gamepad_plugin.cc"
  "gamepad_stream_handler.cc"
  "sdl_manager.cc"
  "button_mapping.cc"
)

# Apply standard settings to the plugin library (symbol visibility, etc).
apply_standard_settings(${PLUGIN_NAME})

# Symbols are hidden by default to reduce the chance of symbol collisions
# between plugins. This should not be removed; any symbols that should be
# exported should be explicitly exported with the FLUTTER_PLUGIN_EXPORT macro.
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  CXX_STANDARD 17
)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# Source include directories and library dependencies.
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::GTK)

if(SDL3_BUNDLED)
  target_compile_definitions(${PLUGIN_NAME} PRIVATE HAVE_SDL3=1)
  target_link_libraries(${PLUGIN_NAME} PRIVATE SDL3::SDL3-static)
elseif(SDL3_FOUND)
  target_compile_definitions(${PLUGIN_NAME} PRIVATE HAVE_SDL3=1)
  target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::SDL3)
else()
  target_compile_definitions(${PLUGIN_NAME} PRIVATE HAVE_SDL3=0)
endif()
