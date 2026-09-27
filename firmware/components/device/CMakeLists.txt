# ESP-IDF adapters implementing the core ports (ADR-0002). Only this component and main/
# may include ESP-IDF headers.
idf_component_register(
  SRCS src/auth.cpp src/camera.cpp src/clocks.cpp src/connect_client.cpp src/diagnostics.cpp
       src/log.cpp src/network.cpp src/settings_store.cpp src/web_server.cpp
  INCLUDE_DIRS include
  REQUIRES core esp_driver_gpio esp_driver_ledc esp_http_client esp_http_server esp_netif
           esp_wifi esp_timer json mbedtls nvs_flash esp32-camera mdns)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

# The web UI (web/src) is packed into one gzip bundle at build time and embedded in flash.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  idf_build_get_property(python PYTHON)
  set(repo_root ${CMAKE_CURRENT_LIST_DIR}/../../..)
  set(web_bundle ${CMAKE_CURRENT_BINARY_DIR}/web_bundle.bin)
  file(GLOB_RECURSE web_sources CONFIGURE_DEPENDS ${repo_root}/web/src/*)
  add_custom_command(
    OUTPUT ${web_bundle}
    COMMAND ${python} ${repo_root}/tools/pack_web.py ${repo_root}/web/src ${web_bundle}
    DEPENDS ${web_sources} ${repo_root}/tools/pack_web.py
    VERBATIM)
  add_custom_target(web_bundle DEPENDS ${web_bundle})
  target_add_binary_data(${COMPONENT_LIB} ${web_bundle} BINARY DEPENDS web_bundle)
endif()
