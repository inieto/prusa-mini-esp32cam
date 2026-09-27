idf_component_register(SRCS main.cpp REQUIRES core device esp_app_format app_update)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
