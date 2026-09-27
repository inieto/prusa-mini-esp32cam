cmake_minimum_required(VERSION 3.24)
project(prusacam_host_tests LANGUAGES C CXX)
enable_testing()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Tests run with sanitizers: data races and lifetime bugs were v1.1.2's main failure mode.
set(SANITIZERS -fsanitize=address,undefined -fno-omit-frame-pointer)
add_compile_options(${SANITIZERS})
set(WARNINGS -Wall -Wextra -Wpedantic -Werror)
add_link_options(${SANITIZERS})

include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
# Same cJSON version ESP-IDF v5.5.5 bundles (components/json).
FetchContent_Declare(cjson
  URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SOURCE_SUBDIR do-not-build-upstream-cmake)
FetchContent_MakeAvailable(googletest cjson)

add_library(cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
target_include_directories(cjson SYSTEM PUBLIC ${cjson_SOURCE_DIR})
target_compile_options(cjson PRIVATE -w)

add_subdirectory(../../firmware/components/core core)
target_compile_options(core PRIVATE ${WARNINGS})

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
add_executable(core_tests ${TEST_SOURCES})
target_compile_options(core_tests PRIVATE ${WARNINGS})
target_link_libraries(core_tests PRIVATE core GTest::gtest_main)

# Pack the real UI so the bundle format is tested end to end.
set(WEB_BUNDLE ${CMAKE_CURRENT_BINARY_DIR}/web_bundle.bin)
file(GLOB_RECURSE WEB_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../web/src/*)
add_custom_command(OUTPUT ${WEB_BUNDLE}
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/pack_web.py
          ${CMAKE_CURRENT_SOURCE_DIR}/../../web/src ${WEB_BUNDLE}
  DEPENDS ${WEB_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/pack_web.py)
add_custom_target(web_bundle DEPENDS ${WEB_BUNDLE})
add_dependencies(core_tests web_bundle)
target_compile_definitions(core_tests PRIVATE WEB_BUNDLE_PATH="${WEB_BUNDLE}")

include(GoogleTest)
gtest_discover_tests(core_tests DISCOVERY_MODE PRE_TEST)
