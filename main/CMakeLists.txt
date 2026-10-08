# ASMR 声景播放器固件只编译 main.c 与 as_*.c。
# baseline 的 demo_*.c / ui_pixel*.c 仍保留在仓库里（主机测试会用到），但不进固件。
file(GLOB AS_SOURCES "${CMAKE_CURRENT_LIST_DIR}/as_*.c")

idf_component_register(
    SRCS "main.c" ${AS_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash esp_hw_support heap esp_driver_usb_serial_jtag
)

# 中文字体子集（tools/gen_asmr_fonts.py 生成，见 assets/README）。
file(GLOB AS_FONT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/as_*.c")
target_sources(${COMPONENT_LIB} PRIVATE ${AS_FONT_SOURCES})

# 声景合成在音频任务里逐样本运行：即使整体按体积优化，这几个热点文件也用 -O2。
set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/as_dsp.c"
    "${CMAKE_CURRENT_LIST_DIR}/as_sounds.c"
    "${CMAKE_CURRENT_LIST_DIR}/as_mixer.c"
    PROPERTIES COMPILE_OPTIONS "-O2")
