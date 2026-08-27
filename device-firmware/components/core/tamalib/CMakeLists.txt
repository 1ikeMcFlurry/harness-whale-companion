idf_component_register(
    SRCS "src/tamalib.c" "src/cpu.c" "src/hw.c"
    INCLUDE_DIRS "include" "include/tamalib")

# CPU instruction dispatch is the dominant cost during offline catch-up. Keep
# this isolated emulator optimized even when the legacy application is built
# with the project's debug-friendly global optimization level.
target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
