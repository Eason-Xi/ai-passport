// main/yz_catalog.c —— 字目查询（数据见生成的 yz_catalog_data.c）。
#include "yz_catalog.h"

int yz_catalog_chapter_of(int entry) {
    if (entry < 0 || entry >= YZ_ENTRY_COUNT) return -1;
    for (int c = 0; c < YZ_CHAPTER_COUNT; c++) {
        if (entry < YZ_CHAPTERS[c].first + YZ_CHAPTERS[c].count) return c;
    }
    return -1;
}
