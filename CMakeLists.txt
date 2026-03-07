cmake_minimum_required(VERSION 3.27)

project(Velox VERSION 0.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "Velox currently targets Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Velox requires a 64-bit toolchain.")
endif()

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(compiler_flags)
include(cef_import)

set(VELOX_SOURCES
  src/app/main_win.cc
  src/app/app_bootstrap.cc
  src/app/command_line.cc
  src/app/runtime_profile.cc
  src/app/site_predictor.cc
  src/browser/browser_controller.cc
  src/browser/browser_window.cc
  src/browser/win32_controls.cc
  src/cef/browser_policy.cc
  src/cef/render_metrics_bridge.cc
  src/cef/request_context_factory.cc
  src/cef/velox_cef_app.cc
  src/cef/velox_client.cc
  src/platform/win/cache_maintenance.cc
  src/platform/win/crash_handler.cc
  src/platform/win/file_utils.cc
  src/platform/win/logger.cc
  src/profiling/memory_sampler.cc
  src/profiling/metrics_recorder.cc
  src/profiling/perf_timer.cc
  src/settings/app_settings.cc
  src/settings/settings_loader.cc
  resources/velox.rc
)

set(VELOX_HEADERS
  src/app/app_bootstrap.h
  src/app/command_line.h
  src/app/runtime_profile.h
  src/app/site_predictor.h
  src/browser/browser_controller.h
  src/browser/browser_window.h
  src/browser/win32_controls.h
  src/cef/browser_policy.h
  src/cef/render_metrics_bridge.h
  src/cef/request_context_factory.h
  src/cef/velox_cef_app.h
  src/cef/velox_client.h
  src/platform/win/cache_maintenance.h
  src/platform/win/crash_handler.h
  src/platform/win/file_utils.h
  src/platform/win/logger.h
  src/profiling/memory_sampler.h
  src/profiling/metrics_recorder.h
  src/profiling/perf_timer.h
  src/settings/app_settings.h
  src/settings/settings_loader.h
)

add_executable(velox WIN32 ${VELOX_SOURCES} ${VELOX_HEADERS})
target_include_directories(velox PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(
  velox
  PRIVATE
    libcef_lib
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
    comctl32
    dbghelp
    psapi
    shlwapi
    user32
    gdi32
    ole32
    shell32)
target_link_libraries(velox PRIVATE ws2_32)

target_compile_definitions(
  velox
  PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS)

SET_EXECUTABLE_TARGET_PROPERTIES(velox)
apply_velox_compiler_flags(velox)
if(MSVC)
  target_link_options(velox PRIVATE /SUBSYSTEM:WINDOWS)
endif()
velox_copy_cef_runtime(velox)
add_custom_command(
  TARGET velox
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:velox>/config"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/config/settings.json"
          "$<TARGET_FILE_DIR:velox>/config/settings.json"
  VERBATIM)

add_executable(settings_tests
  tests/settings_tests.cc
  src/app/command_line.cc
  src/platform/win/file_utils.cc
  src/platform/win/logger.cc
  src/settings/app_settings.cc
  src/settings/settings_loader.cc)
target_include_directories(settings_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(settings_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(settings_tests PRIVATE shlwapi user32)
apply_velox_compiler_flags(settings_tests)

add_executable(metrics_tests
  tests/metrics_tests.cc
  src/platform/win/file_utils.cc
  src/platform/win/logger.cc
  src/profiling/memory_sampler.cc
  src/profiling/metrics_recorder.cc
  src/profiling/perf_timer.cc)
target_include_directories(metrics_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(metrics_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(metrics_tests PRIVATE psapi user32)
apply_velox_compiler_flags(metrics_tests)

enable_testing()
add_test(NAME settings_tests COMMAND settings_tests)
add_test(NAME metrics_tests COMMAND metrics_tests)
