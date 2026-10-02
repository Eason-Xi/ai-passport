# 颜真卿字帖（多宝塔碑）固件只编译 main.c 与 yz_*.c。
# baseline 的 demo_*.c / ui_pixel*.c 仍保留在仓库里（主机测试会用到），但不进固件。
file(GLOB YZ_SOURCES "${CMAKE_CURRENT_LIST_DIR}/yz_*.c")

idf_component_register(
    SRCS "main.c" ${YZ_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash
    # 拓本字形包（tools/gen_yz_assets.py 生成），符号 _binary_yz_glyphs_bin_start/_end。
    EMBED_FILES "../assets/images/yz_glyphs.bin"
)

# 中文字体子集（tools/gen_yz_fonts.py 生成，见 assets/README）。
file(GLOB YZ_FONT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/yz_*.c")
target_sources(${COMPONENT_LIB} PRIVATE ${YZ_FONT_SOURCES})
