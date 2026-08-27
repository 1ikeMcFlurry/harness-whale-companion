# components/services/test/CMakeLists.txt
idf_component_register(
    SRCS "test_profile_ctl.c" "test_ble_match.c" "test_config_json.c" "test_frame_reasm.c" "test_jpeg_rx.c" "test_score_rx.c" "test_rtttl.c" "test_square_synth.c" "test_sn_format.c" "test_prov_cmd.c" "test_token_bcast.c" "test_screen_capture.c" "test_harness_status.c" "test_avatar_pack.c" "test_profile_bcast.c" "test_profile_batch_bcast.c"
    INCLUDE_DIRS "."
    REQUIRES services unity)
