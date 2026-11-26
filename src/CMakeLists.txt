cmake_minimum_required(VERSION 3.20)
project(pluginhost LANGUAGES CXX)

# LO_SDK: path to an unpacked LibreOffice SDK (see config/versions.json).
# Example: cmake -G Ninja -DLO_SDK=C:/LO-A-SDK ...
if(NOT DEFINED LO_SDK)
  message(FATAL_ERROR "Set -DLO_SDK=<path to LibreOffice SDK>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Generated UNO C++ headers go to build/gen. Generation is done by the SDK tool
# cppumaker, run once from scripts/gen_uno_headers.ps1 before building.
set(PH_GEN_DIR "${CMAKE_BINARY_DIR}/gen" CACHE PATH "Generated UNO headers")

set(PH_WEBVIEW2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/webview2/pkg/build/native"
    CACHE PATH "Unpacked Microsoft.Web.WebView2 nupkg (build/native)")

add_library(pluginhost SHARED
  component.cxx
  log.cxx
  panel_factory.cxx
  plugin_panel.cxx
  webhost_win.cxx
)

target_include_directories(pluginhost PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PH_GEN_DIR}"
  "${LO_SDK}/include"
  "${PH_WEBVIEW2_DIR}/include"
)

target_compile_definitions(pluginhost PRIVATE
  CPPU_ENV=msci
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

target_link_directories(pluginhost PRIVATE "${LO_SDK}/lib" "${PH_WEBVIEW2_DIR}/x64")
target_link_libraries(pluginhost PRIVATE
  sal cppu cppuhelper salhelper
  WebView2LoaderStatic
  user32 ole32 shlwapi
)

# Output name must match extension/PluginHost.components and manifest.xml.
set_target_properties(pluginhost PROPERTIES
  PREFIX ""
  OUTPUT_NAME "pluginhost"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
