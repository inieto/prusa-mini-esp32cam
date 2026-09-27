# ESP-IDF adapters implementing the core ports (ADR-0002). Only this component and main/
# may include ESP-IDF headers.
idf_component_register(
  SRCS src/camera.cpp src/clocks.cpp src/connect_client.cpp src/log.cpp src/network.cpp
       src/settings_store.cpp
  INCLUDE_DIRS include
  REQUIRES core esp_driver_gpio esp_driver_ledc esp_http_client esp_netif esp_wifi
           esp_timer mbedtls nvs_flash esp32-camera mdns)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
