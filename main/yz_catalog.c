// main/yz_catalog.c —— 字目查询（数据见生成的 yz_catalog_data.c）。
#include "yz_catalog.h"

int yz_catalog_chapter_of(int entry, int *pos) {
    if (entry < 0 || entry >= YZ_ENTRY_COUNT) return -1;
    for (int c = 0; c < YZ_CHAPTER_COUNT; c++) {
        for (int i = 0; i < YZ_CHAPTERS[c].count; i++) {
            if (yz_chapter_entry(c, i) == entry) {
                if (pos) *pos = i;
                return c;
            }
        }
    }
    return -1;
}

int yz_catalog_book_of(int entry) {
    if (entry < 0 || entry >= YZ_ENTRY_COUNT) return -1;
    for (int b = 0; b < YZ_BOOK_COUNT; b++) {
        if (entry >= YZ_BOOKS[b].first_entry && entry < YZ_BOOKS[b].first_entry + YZ_BOOKS[b].entry_count) return b;
    }
    return -1;
}
