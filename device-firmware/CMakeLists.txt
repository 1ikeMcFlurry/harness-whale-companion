# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# components/ 按架构分层分组存放,ESP-IDF 默认只扫描 components/ 的直接子目录,
# 因此显式把各分组目录加入组件搜索路径。组件名仍是各叶子目录名(ports/services/
# presentation/platform_esp32/app),REQUIRES 依赖链不受影响。
set(EXTRA_COMPONENT_DIRS
    components/core
    components/ui
    components/platform
    components/app)

# 固件版本(供 release.py 命名与 esp_app_desc 使用)。发版时按需递增。
set(PROJECT_VER "1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(trae_card)

# Built-in named avatars are immutable release resources.  Generate the raw
# partition image as part of every normal build and register it with `flash`.
partition_table_get_partition_info(imgava_offset "--partition-name imgava" "offset")
partition_table_get_partition_info(imgava_size "--partition-name imgava" "size")
if(imgava_size)
    set(imgava_bin "${CMAKE_BINARY_DIR}/imgava.bin")
    set(avatar_catalog "${CMAKE_BINARY_DIR}/avatar_catalog.json")
    file(GLOB avatar_sources CONFIGURE_DEPENDS "${PROJECT_DIR}/assets/avatars/*.png")
    add_custom_command(
        OUTPUT "${imgava_bin}" "${avatar_catalog}"
        COMMAND "${PYTHON}" "${PROJECT_DIR}/tools/avatar_pack.py"
                --input "${PROJECT_DIR}/assets/avatars"
                --output "${imgava_bin}"
                --catalog-output "${avatar_catalog}"
                --size "${imgava_size}"
        DEPENDS
            "${PROJECT_DIR}/tools/avatar_pack.py"
            ${avatar_sources}
        VERBATIM)
    add_custom_target(avatar_pack_bin ALL DEPENDS "${imgava_bin}" "${avatar_catalog}")
    esptool_py_flash_target_image(flash imgava "${imgava_offset}" "${imgava_bin}")
    add_dependencies(flash avatar_pack_bin)
endif()
