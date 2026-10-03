// main/yz_catalog.c —— 字目查询（数据见生成的 yz_catalog_data.c）。
#include "yz_catalog.h"

int yz_catalog_chapter_of(int entry, int *pos) {
    if (entry < 0 || entry >= YZ_ENTRY_COUNT) return -1;
    // 碑文卷按字目顺序首尾相接：第 c 卷是 [first_entry, first_entry + count) 这一段。
    for (int c = 0; c < YZ_TEXT_CHAPTER_COUNT; c++) {
        const int first = yz_chapter_entry(c, 0);
        if (entry >= first && entry < first + YZ_CHAPTERS[c].count) {
            if (pos) *pos = entry - first;
            return c;
        }
    }
    return -1;
}
