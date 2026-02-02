# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to be incompatible with some
# temporary versions of Flutter.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "gamepad")
project(${PROJECT_NAME} LANGUAGES CXX)

# Invoke flutter tooling for plugin setup.
# This must be included before any other rules.
# It defines the flutter_plugin_add_* functions used below.
include(${EPHEMERAL_DIR}/generated_plugin_registrant.cmake OPTIONAL)

# Define the plugin library target.
add_library(${PROJECT_NAME} SHARED
  "gamepad_plugin.cpp"
  "gamepad_stream_handler.cpp"
  "xinput_manager.cpp"
  "button_mapping.cpp"
)

# Apply standard Flutter plugin settings.
apply_standard_settings(${PROJECT_NAME})

# Symbols are hidden by default to reduce the chance of accidental conflicts
# between plugins. Explicitly export the plugin registration symbol.
set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

target_compile_definitions(${PROJECT_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# Source include directories and library dependencies.
target_include_directories(${PROJECT_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  xinput
)
