# components/platform_esp32/CMakeLists.txt
idf_component_register(
    SRCS "src/disp_st7789.c" "src/lvgl_port_setup.c" "src/screen_capture_lvgl.c"
         "src/btn_iot_button.c"
         "src/audio_es8311.c" "src/platform_sys.c" "src/ble_config.c"
         "src/ble_scan.c" "src/kv_nvs.c" "src/jpeg_store.c" "src/jpeg_view.c"
         "src/png_decode.c" "src/battery_cw2017.c"
         "src/identity_nvs.c" "src/provision_console.c" "src/token_mac_esp.c"
         "src/audio_store.c"
         "src/avatar_store.c"
         "src/tamagotchi_network.c"
         "src/tamagotchi_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES ports services driver esp_lcd
    PRIV_REQUIRES esp_lvgl_port button esp_codec_dev esp_hw_support bt nvs_flash
                  esp_driver_i2c esp_driver_i2s esp_partition esp_new_jpeg esp_adc
                  mbedtls esp_driver_usb_serial_jtag esp_rom spiffs
                  tamalib esp_wifi esp_netif esp_event)
