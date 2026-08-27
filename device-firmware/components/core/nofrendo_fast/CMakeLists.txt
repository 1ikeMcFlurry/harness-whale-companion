file(GLOB_RECURSE NOFRENDO_FAST_SOURCES "vendor/*.c")

idf_component_register(
    SRCS ${NOFRENDO_FAST_SOURCES} "src/nofrendo_fast.c"
    INCLUDE_DIRS
        "include"
        "vendor"
        "vendor/cpu"
        "vendor/libsnss"
        "vendor/nes"
        "vendor/sndhrdw")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    NOFRENDO_EMBEDDED=1
    NOFRENDO_STREAM_LINES=20
    NES_VISIBLE_HEIGHT=240)

# Nofrendo uses a scanline-oriented PPU and a register-cached 6502 loop.  Keep
# the whole isolated component optimized independently from the UI firmware.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -Wno-char-subscripts
    -Wno-attributes
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
    -Wno-error=stringop-truncation)
