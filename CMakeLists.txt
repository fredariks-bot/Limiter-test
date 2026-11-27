cmake_minimum_required(VERSION 3.20)
project(LimiterTest VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_library(limiter_dsp STATIC src/dsp/LimiterDsp.cpp)
target_include_directories(limiter_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(limiter_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(limiter_plugin_obj OBJECT
  src/plugin/Parameters.cpp
  src/plugin/LimiterPlugin.cpp
  src/plugin/EditorView.cpp
  src/plugin/PluginEntry.cpp
  src/plugin/vst3/Vst3Mini.cpp
)
target_link_libraries(limiter_plugin_obj PUBLIC limiter_dsp)
set_target_properties(limiter_plugin_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(VST3_BUNDLE_DIR ${CMAKE_BINARY_DIR}/dist/Limiter.vst3)
if(WIN32)
  set(VST3_MODULE_DIR ${VST3_BUNDLE_DIR}/Contents/x86_64-win)
  set(VST3_MODULE_NAME Limiter.vst3)
else()
  set(VST3_MODULE_DIR ${VST3_BUNDLE_DIR}/Contents/x86_64-linux)
  set(VST3_MODULE_NAME Limiter.so)
endif()

add_library(Limiter MODULE $<TARGET_OBJECTS:limiter_plugin_obj>)
set_target_properties(Limiter PROPERTIES
  PREFIX ""
  SUFFIX ""
  OUTPUT_NAME "${VST3_MODULE_NAME}"
  LIBRARY_OUTPUT_DIRECTORY "${VST3_MODULE_DIR}"
  RUNTIME_OUTPUT_DIRECTORY "${VST3_MODULE_DIR}"
)
if(WIN32)
  target_link_libraries(Limiter PRIVATE user32 gdi32)
endif()

file(MAKE_DIRECTORY ${VST3_BUNDLE_DIR}/Contents)
file(WRITE ${VST3_BUNDLE_DIR}/Contents/PkgInfo
"<?xml version=\"1.0\" encoding=\"utf-8\"?>
<PkgInfo>
  <bundle id=\"com.vibe.limiter-test\" name=\"Limiter\" vendor=\"Vibe\" category=\"Fx\" version=\"1.0.0.0\" sdkVersion=\"VST 3\"/>
</PkgInfo>
")

enable_testing()

add_executable(limiter_tests
  tests/main.cpp
  tests/test_dsp.cpp
  tests/test_plugin.cpp
  $<TARGET_OBJECTS:limiter_plugin_obj>
)
target_link_libraries(limiter_tests PRIVATE limiter_dsp)
add_test(NAME limiter_tests COMMAND limiter_tests)

add_executable(limiter_render tools/render.cpp $<TARGET_OBJECTS:limiter_plugin_obj>)
target_link_libraries(limiter_render PRIVATE limiter_dsp)
