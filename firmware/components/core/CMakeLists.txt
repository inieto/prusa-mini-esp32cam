# Portable core: builds as an ESP-IDF component on the device and as a plain static
# library for host tests (test/host). Must not depend on ESP-IDF headers (ADR-0002).
set(CORE_SOURCES
    src/backoff.cpp
    src/base64.cpp
    src/camera_settings.cpp
    src/exif.cpp
    src/frame_pool.cpp
    src/ports.cpp
    src/prusa_connect.cpp
    src/recovery.cpp
    src/scheduler.cpp
    src/snapshot_service.cpp
)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${CORE_SOURCES} INCLUDE_DIRS include REQUIRES json)
  target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
else()
  add_library(core STATIC ${CORE_SOURCES})
  target_include_directories(core PUBLIC include)
  target_compile_features(core PUBLIC cxx_std_23)
  target_link_libraries(core PUBLIC cjson)
endif()
