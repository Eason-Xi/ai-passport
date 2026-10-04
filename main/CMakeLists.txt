# 乐器节拍器固件只编译 main.c 与 mn_*.c。
# baseline 的 demo_*.c / ui_pixel*.c 仍保留在仓库里（主机测试会用到），但不进固件。
file(GLOB MN_SOURCES "${CMAKE_CURRENT_LIST_DIR}/mn_*.c")

idf_component_register(
    SRCS "main.c" ${MN_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash esp_hw_support heap
)

# 中文字体子集（tools/gen_metronome_fonts.py 生成，见 assets/README）。
file(GLOB MN_FONT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/mn_*.c")
target_sources(${COMPONENT_LIB} PRIVATE ${MN_FONT_SOURCES})
