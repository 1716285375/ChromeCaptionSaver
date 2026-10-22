cmake_minimum_required(VERSION 3.26)
project(ChromeCaptionSaver VERSION 0.3.0 LANGUAGES CXX)

option(CCS_STATIC_RUNTIME "Use the static MSVC runtime" OFF)
option(CCS_USE_LOCAL_APP_DATA "Store user data in LocalAppData" OFF)
if(CCS_STATIC_RUNTIME)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- Global settings -------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")
configure_file(
  "include/ChromeCaptionSaver/core/Paths.h.in"
  "generated/ChromeCaptionSaver/core/Paths.h"
  @ONLY
)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---- Dependencies (vcpkg manifest mode) -----------------------------------
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ---- Common compile flags --------------------------------------------------
add_compile_options(
  /W4 /permissive- /utf-8 /EHsc
  $<$<CONFIG:Debug>:/Zi>
)
add_compile_definitions(
  UNICODE _UNICODE
  NOMINMAX WIN32_LEAN_AND_MEAN
)

# ---- ccs_core: business logic, storage, export (no Win32 / no ImGui) ------
file(GLOB_RECURSE CCS_CORE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/transcript/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/export/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/config/*.cpp"
)
add_library(ccs_core STATIC ${CCS_CORE_SOURCES})
target_include_directories(ccs_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(ccs_core PUBLIC
  spdlog::spdlog
  nlohmann_json::nlohmann_json
)

# ---- ccs_windows: Win32 + UIA platform layer ------------------------------
file(GLOB_RECURSE CCS_WINDOWS_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/capture/*.cpp"
)
add_library(ccs_windows STATIC ${CCS_WINDOWS_SOURCES})
target_include_directories(ccs_windows PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(ccs_windows PUBLIC
  ccs_core
  ole32
  oleaut32
  user32
  advapi32
  shell32
  uiautomationcore
  ws2_32 bcrypt
)

# ---- caption_probe: standalone CLI ----------------------------------------
add_executable(caption_probe
  apps/caption_probe/main.cpp
)
target_link_libraries(caption_probe PRIVATE
  ccs_windows ccs_core
)

# ---- Tests (GoogleTest) ----------------------------------------------------
option(CCS_BUILD_TESTS "Build unit tests" ON)
if(CCS_BUILD_TESTS)
  enable_testing()
  find_program(CCS_NODE_EXECUTABLE node)
  if(CCS_NODE_EXECUTABLE)
    add_test(NAME Browser.QueueProtocol COMMAND "${CCS_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/browser-extension/test.cjs")
  endif()
  find_package(GTest CONFIG REQUIRED)
  file(GLOB CCS_TEST_SOURCES CONFIGURE_DEPENDS "tests/test_*.cpp")
  list(FILTER CCS_TEST_SOURCES EXCLUDE REGEX "test_gui_layout\\.cpp$")
  add_executable(ccs_tests ${CCS_TEST_SOURCES})
  target_link_libraries(ccs_tests PRIVATE
    ccs_core ccs_windows
    GTest::gtest GTest::gtest_main
  )
  include(GoogleTest)
  gtest_discover_tests(ccs_tests DISCOVERY_TIMEOUT 30 PROPERTIES TIMEOUT 45)
  foreach(args IN ITEMS "--duration;abc;--probe" "--duration;-1;--record"
                        "--depth;999;--dump-tree" "--hwnd;0;--record"
                        "--record;--outdir" "--unknown" "--record;--probe"
                        "--probe;--duration;1;--duration;2" "--version;--outdir;ignored"
                        "--list-windows;--include-text" "--probe;--hwnd;xyz"
                        "--export" "--export;input.jsonl;--duration;1")
    string(MAKE_C_IDENTIFIER "cli_${args}" test_name)
    add_test(NAME ${test_name} COMMAND ${CMAKE_COMMAND}
      "-DPROBE=$<TARGET_FILE:caption_probe>" "-DARGS=${args}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.cmake")
  endforeach()
endif()

# ---- Desktop GUI (Dear ImGui + Win32 + DX11) ----------------------------
option(CCS_BUILD_GUI "Build Dear ImGui desktop GUI" ON)
if(CCS_BUILD_GUI)
  set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
  add_library(ccs_imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
  )
  target_include_directories(ccs_imgui PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
  )
  target_link_libraries(ccs_imgui PUBLIC d3d11 dxgi)

  add_executable(ChromeCaptionSaver WIN32 apps/desktop/main.cpp)
  target_link_libraries(ChromeCaptionSaver PRIVATE
    ccs_imgui ccs_core ccs_windows
    d3d11 dxgi user32 shell32 comdlg32 windowscodecs
  )
  add_custom_command(TARGET ChromeCaptionSaver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/browser-extension"
      "$<TARGET_FILE_DIR:ChromeCaptionSaver>/ChatGPTBridge"
  )
  if(CCS_BUILD_TESTS)
    add_executable(ccs_gui_layout_tests tests/test_gui_layout.cpp)
    target_include_directories(ccs_gui_layout_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/apps/desktop")
    target_link_libraries(ccs_gui_layout_tests PRIVATE ccs_imgui ccs_core)
    add_test(NAME Desktop.TranscriptLayoutRegression COMMAND ccs_gui_layout_tests)
    set_tests_properties(Desktop.TranscriptLayoutRegression PROPERTIES TIMEOUT 20)
    add_test(NAME Desktop.RenderSmoke COMMAND ChromeCaptionSaver --smoke-test)
    set_tests_properties(Desktop.RenderSmoke PROPERTIES TIMEOUT 20)
    foreach(resolution IN ITEMS "1280;720" "1440;900" "1600;900" "1920;1080")
      list(GET resolution 0 ui_width)
      list(GET resolution 1 ui_height)
      foreach(dpi IN ITEMS 1 1.25 1.5)
        add_test(NAME Desktop.Layout_${ui_width}_${ui_height}_${dpi}
          COMMAND ChromeCaptionSaver --smoke-test ${ui_width} ${ui_height} ${dpi})
        set_tests_properties(Desktop.Layout_${ui_width}_${ui_height}_${dpi} PROPERTIES TIMEOUT 20)
      endforeach()
    endforeach()
  endif()
endif()
