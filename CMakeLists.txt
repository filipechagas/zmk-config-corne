# Custom ZMK module: keyboard status over a vendor BLE GATT service.

if(CONFIG_ZMK_STATUS_GATT)
  # Firmware revision string: the config repo's short SHA, plus "-dirty" when
  # the work tree has uncommitted changes (tracked or untracked, non-ignored).
  # Captured at CMake configure time. `-c safe.directory=*` lets git read a repo
  # owned by another user (bind mount in Docker, container job in GH Actions).
  set(ZMK_STATUS_GATT_FW_VERSION "unknown")
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} -c safe.directory=* -C ${CMAKE_CURRENT_SOURCE_DIR}
              rev-parse --short HEAD
      OUTPUT_VARIABLE zmk_status_gatt_sha
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE zmk_status_gatt_sha_rc
      ERROR_QUIET)
    if(zmk_status_gatt_sha_rc EQUAL 0 AND NOT zmk_status_gatt_sha STREQUAL "")
      set(ZMK_STATUS_GATT_FW_VERSION "${zmk_status_gatt_sha}")
      execute_process(
        COMMAND ${GIT_EXECUTABLE} -c safe.directory=* -C ${CMAKE_CURRENT_SOURCE_DIR}
                status --porcelain
        OUTPUT_VARIABLE zmk_status_gatt_dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE zmk_status_gatt_dirty_rc
        ERROR_QUIET)
      if(zmk_status_gatt_dirty_rc EQUAL 0 AND NOT zmk_status_gatt_dirty STREQUAL "")
        set(ZMK_STATUS_GATT_FW_VERSION "${ZMK_STATUS_GATT_FW_VERSION}-dirty")
      endif()
    endif()
  endif()
  message(STATUS "ZMK status GATT firmware revision: ${ZMK_STATUS_GATT_FW_VERSION}")

  set(zmk_status_gatt_gen_dir ${CMAKE_CURRENT_BINARY_DIR}/zmk_status_gatt_generated)
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/zmk_status_gatt_version.h.in
    ${zmk_status_gatt_gen_dir}/zmk_status_gatt_version.h
    @ONLY)

  # Added to ZMK's `app` target so the source sees ZMK's include paths.
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/zmk_status_gatt.c)
  target_include_directories(app PRIVATE ${zmk_status_gatt_gen_dir})
endif()
