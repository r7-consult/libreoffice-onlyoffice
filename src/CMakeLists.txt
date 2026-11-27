cmake_minimum_required(VERSION 3.20)
project(pluginhost LANGUAGES C CXX)

# DRAFT: not compiled yet. Written on Windows without a compiler; expect fixes on first build.
#
# Required:
#   -DLO_SDK=<path to unpacked LibreOffice SDK>
# Optional:
#   -DPH_GEN_DIR=<generated UNO headers, default build/gen>
#   -DPH_WEBVIEW2_DIR=<third_party/webview2/pkg/build/native>
#   -DPH_QUICKJS_DIR=<third_party/quickjs-ng>

if(NOT DEFINED LO_SDK)
  message(FATAL_ERROR "Set -DLO_SDK=<path to LibreOffice SDK>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

set(PH_GEN_DIR "${CMAKE_BINARY_DIR}/gen" CACHE PATH "Generated UNO headers (scripts/gen_uno_headers.ps1)")
set(PH_WEBVIEW2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/webview2/pkg/build/native" CACHE PATH
    "Unpacked Microsoft.Web.WebView2 nupkg (build/native)")
set(PH_QUICKJS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/quickjs-ng" CACHE PATH
    "quickjs-ng 0.18.0 source tree")

# --- QuickJS-ng (static) ----------------------------------------------------
# Use upstream's own CMake: it carries the MSVC flags QuickJS needs (/experimental:c11atomics,
# _GNU_SOURCE, warning suppressions). A hand-written source list drifted already (0.18.0 has no cutils.c).
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_LIBC OFF CACHE BOOL "" FORCE)
add_subdirectory(${PH_QUICKJS_DIR} ${CMAKE_BINARY_DIR}/quickjs-ng EXCLUDE_FROM_ALL)

# --- pluginhost.dll --------------------------------------------------------
set(PH_SOURCES
  component.cxx
  log.cxx
  paths.cxx
  json_mini.cxx
  plugin_config.cxx
  host_service.cxx
  protocol_adapter.cxx
  document_bridge.cxx
  command_engine.cxx
  api_bridge.cxx
  context_menu.cxx
  menu_dispatch.cxx
  panel_factory.cxx
  plugin_panel.cxx
)
if(WIN32)
  list(APPEND PH_SOURCES webhost_win.cxx)
else()
  list(APPEND PH_SOURCES webhost_stub.cxx)
endif()

add_library(pluginhost SHARED ${PH_SOURCES})

target_include_directories(pluginhost PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PH_GEN_DIR}"
  "${LO_SDK}/include"
  "${PH_WEBVIEW2_DIR}/include"
)

if(WIN32)
  # Same as sdk/settings/settings.mk for 64-bit MSVC: the UNO bridge ABI is "mscx" (msci = 32-bit).
  target_compile_definitions(pluginhost PRIVATE WIN32 WNT CPPU_ENV=mscx WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(pluginhost PRIVATE /EHsc /Zc:__cplusplus /utf-8)
else()
  target_compile_definitions(pluginhost PRIVATE UNX CPPU_ENV=gcc3)
endif()

target_link_directories(pluginhost PRIVATE "${LO_SDK}/lib" "${PH_WEBVIEW2_DIR}/x64")
target_link_libraries(pluginhost PRIVATE qjs)
if(WIN32)
  # Windows import libraries in sdk/lib carry an "i" prefix.
  target_link_libraries(pluginhost PRIVATE isal icppu icppuhelper isalhelper
                        WebView2LoaderStatic user32 ole32 shlwapi version advapi32)
else()
  target_link_libraries(pluginhost PRIVATE uno_sal uno_cppu uno_cppuhelpergcc3 uno_salhelpergcc3)
endif()

set_target_properties(pluginhost PROPERTIES
  PREFIX ""
  OUTPUT_NAME "pluginhost"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
