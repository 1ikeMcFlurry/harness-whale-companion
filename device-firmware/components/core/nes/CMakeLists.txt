idf_component_register(
    SRCS "src/nes_core.c" "vendor/agnes.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "vendor")

# The production ESP32-C3 has no PSRAM.  Keep only one 256-byte palette-index
# scanline instead of Agnes' default 61,440-byte framebuffer.
target_compile_definitions(${COMPONENT_LIB} PRIVATE AGNES_ENABLE_FRAMEBUFFER=0)

# Emulator hot paths need a release optimization level even while the rest of
# the firmware remains on the project's debug-friendly default.
target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
