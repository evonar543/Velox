if(NOT DEFINED CEF_ROOT OR CEF_ROOT STREQUAL "")
  message(FATAL_ERROR "Set CEF_ROOT to an extracted official CEF Windows x64 binary distribution.")
endif()

if(NOT EXISTS "${CEF_ROOT}/include/cef_app.h")
  message(FATAL_ERROR "CEF_ROOT does not look like a valid CEF SDK: ${CEF_ROOT}")
endif()

file(TO_CMAKE_PATH "${CEF_ROOT}" CEF_ROOT)

set(_CEF_ROOT_EXPLICIT ON)
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

if(NOT TARGET libcef_lib)
  ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
endif()

if(NOT TARGET libcef_dll_wrapper)
  add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)
endif()

function(velox_copy_cef_runtime target_name)
  # Keep runtime staging to two copy steps instead of dozens of individual
  # commands. This avoids MSBuild post-build command bloat and keeps CEF assets
  # colocated with the executable where Chromium expects them.
  add_custom_command(
    TARGET ${target_name}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_BINARY_DIR}" "$<TARGET_FILE_DIR:${target_name}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_RESOURCE_DIR}" "$<TARGET_FILE_DIR:${target_name}>"
    VERBATIM)
endfunction()
