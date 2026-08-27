# components/services/CMakeLists.txt
idf_component_register(
    SRCS "src/audio_service.c" "src/ui_model.c"
         "src/profile_ctl.c" "src/ble_match.c" "src/config_json.c"
         "src/frame_reasm.c" "src/jpeg_rx.c"
         "src/score_rx.c" "src/rtttl.c" "src/square_synth.c" "src/sn_format.c"
         "src/prov_cmd.c"
         "src/token_bcast.c"
         "src/jpeg_probe.c"
         "src/battery.c"
         "src/adpcm.c"
         "src/audio_rx.c"
         "src/factory_test.c"
         "src/screen_capture.c"
         "src/harness_status.c"
         "src/avatar_pack.c"
         "src/profile_bcast.c" "src/profile_batch_bcast.c"
    INCLUDE_DIRS "include"
    REQUIRES ports json)
