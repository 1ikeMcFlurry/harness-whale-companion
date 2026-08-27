# components/presentation/CMakeLists.txt
idf_component_register(
    SRCS "src/ui_profile.c" "src/ui_game.c" "src/ui_mario.c" "src/ui_reward.c"
         "src/ui_factory.c" "src/ui_selftest.c" "src/ui_pet.c" "src/ui_button_guide.c"
         "src/ui_harness.c"
         "src/lv_font_cn_16.c" "src/lv_font_cn_24.c"
    INCLUDE_DIRS "include"
    REQUIRES services
    PRIV_REQUIRES lvgl)
