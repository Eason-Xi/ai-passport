// main/yz_theme.h —— 字帖界面的配色与版式常量（240×320 竖屏，四角有 30 px 圆角遮罩）。
#pragma once

// 宣纸、墨、朱砂、碑石。
#define YZ_C_PAPER        0xEFE6D2
#define YZ_C_PAPER_DEEP   0xE2D5BA
#define YZ_C_INK          0x1E1A16
#define YZ_C_INK_SOFT     0x6B6056
#define YZ_C_VERMILION    0xB8322A
#define YZ_C_TRACE        0xE48C7E   // 描红字色
#define YZ_C_STONE        0x1B1A18
#define YZ_C_STONE_LINE   0x4A4640
#define YZ_C_STONE_TEXT   0xEDE6D6
#define YZ_C_STONE_SOFT   0x9C948A

#define YZ_SCREEN_W 240
#define YZ_SCREEN_H 320

// 临帖页的格子：200×200，字形 176×176 居中。
#define YZ_GRID_X 20
#define YZ_GRID_Y 34
#define YZ_GRID_SIZE 200

// 笔法卡：要点文字按两行、每行不超过 13 个全角字编写（主机预览会检查是否意外折行）。
#define YZ_CARD_H 204
#define YZ_CARD_TEXT_W 190
#define YZ_CARD_LINE_SPACE 3
