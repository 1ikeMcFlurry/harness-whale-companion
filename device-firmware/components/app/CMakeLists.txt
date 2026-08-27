# components/app/CMakeLists.txt
idf_component_register(
    SRCS "src/app.c"
    INCLUDE_DIRS "include"
    REQUIRES ports services presentation platform_esp32 esp_timer esp_app_format esp_hw_support)
