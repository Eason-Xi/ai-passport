// main/yz_ui_practice.c —— 临帖页：格子里的拓本原字、简体与拼音、碑文语境、计时进度、笔法卡。
//
// 三种底色：拓本（黑底白字，碑石原貌）、墨迹（宣纸黑字）、描红（宣纸浅朱字，可对着描摹笔顺）。
#include "yz_ui_internal.h"

#define GRID_BORDER 2
#define BOTTOM_Y 238

typedef struct {
    uint32_t bg, glyph, frame, line, text, soft, accent;
    lv_opa_t line_opa;
} palette_t;

static const palette_t PALETTES[YZ_INK_COUNT] = {
    [YZ_INK_STONE] = { YZ_C_STONE, YZ_C_STONE_TEXT, YZ_C_STONE_LINE, YZ_C_STONE_LINE,
                       YZ_C_STONE_TEXT, YZ_C_STONE_SOFT, YZ_C_TRACE, LV_OPA_COVER },
    [YZ_INK_PAPER] = { YZ_C_PAPER, YZ_C_INK, YZ_C_VERMILION, YZ_C_VERMILION,
                       YZ_C_INK, YZ_C_INK_SOFT, YZ_C_VERMILION, LV_OPA_50 },
    [YZ_INK_TRACE] = { YZ_C_PAPER, YZ_C_TRACE, YZ_C_VERMILION, YZ_C_VERMILION,
                       YZ_C_INK, YZ_C_INK_SOFT, YZ_C_VERMILION, LV_OPA_60 },
};

// 格线端点（相对 200×200 格子）。lv_line 只保存指针，所以必须是静态数据。
#define G0 1
#define G1 (YZ_GRID_SIZE - 2)
#define GM (YZ_GRID_SIZE / 2)
#define T1 (YZ_GRID_SIZE / 3)
#define T2 (YZ_GRID_SIZE * 2 / 3)
static const lv_point_precise_t P_H[] = { { G0, GM }, { G1, GM } };
static const lv_point_precise_t P_V[] = { { GM, G0 }, { GM, G1 } };
static const lv_point_precise_t P_D1[] = { { G0, G0 }, { G1, G1 } };
static const lv_point_precise_t P_D2[] = { { G1, G0 }, { G0, G1 } };
static const lv_point_precise_t P_H1[] = { { G0, T1 }, { G1, T1 } };
static const lv_point_precise_t P_H2[] = { { G0, T2 }, { G1, T2 } };
static const lv_point_precise_t P_V1[] = { { T1, G0 }, { T1, G1 } };
static const lv_point_precise_t P_V2[] = { { T2, G0 }, { T2, G1 } };

enum { L_H = 0, L_V, L_D1, L_D2, L_H1, L_H2, L_V1, L_V2, L_COUNT };
static const lv_point_precise_t *const LINE_POINTS[L_COUNT] = { P_H, P_V, P_D1, P_D2, P_H1, P_H2, P_V1, P_V2 };

static lv_obj_t *s_scr;
static lv_obj_t *s_grid;
static lv_obj_t *s_lines[L_COUNT];
static lv_obj_t *s_glyph;
static lv_obj_t *s_pos;
static lv_obj_t *s_star;
static lv_obj_t *s_simp;
static lv_obj_t *s_pinyin;
static lv_obj_t *s_struct;
static lv_obj_t *s_count;
static lv_obj_t *s_timer;
static lv_obj_t *s_progress;
static lv_obj_t *s_phrase;
static lv_obj_t *s_card;
static lv_obj_t *s_card_focus;
static lv_obj_t *s_card_struct;
static lv_obj_t *s_card_source;
static uint8_t s_ink = 0xFF;

static bool line_visible(int line, uint8_t grid) {
    switch (grid) {
    case YZ_GRID_MI: return line <= L_D2;
    case YZ_GRID_TIAN: return line <= L_V;
    case YZ_GRID_JIU: return line >= L_H1;
    default: return false;
    }
}

static void apply_palette(uint8_t ink) {
    const palette_t *p = &PALETTES[ink < YZ_INK_COUNT ? ink : 0];
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(p->bg), 0);
    lv_obj_set_style_border_color(s_grid, lv_color_hex(p->frame), 0);
    for (int i = 0; i < L_COUNT; i++) {
        lv_obj_set_style_line_color(s_lines[i], lv_color_hex(p->line), 0);
        lv_obj_set_style_line_opa(s_lines[i], p->line_opa, 0);
    }
    lv_obj_set_style_image_recolor(s_glyph, lv_color_hex(p->glyph), 0);
    lv_obj_set_style_text_color(s_pos, lv_color_hex(p->soft), 0);
    lv_obj_set_style_text_color(s_star, lv_color_hex(p->accent), 0);
    lv_obj_set_style_text_color(s_simp, lv_color_hex(p->text), 0);
    lv_obj_set_style_text_color(s_pinyin, lv_color_hex(p->accent), 0);
    lv_obj_set_style_text_color(s_struct, lv_color_hex(p->soft), 0);
    lv_obj_set_style_text_color(s_count, lv_color_hex(p->soft), 0);
    lv_obj_set_style_text_color(s_timer, lv_color_hex(p->accent), 0);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(p->accent), 0);
    lv_obj_set_style_text_color(s_phrase, lv_color_hex(p->text), 0);
    yz_ui_battery_theme(ink == YZ_INK_STONE);
}

static void update(const yz_book_t *book) {
    const int entry = yz_book_entry(book);
    if (entry < 0) return;
    const yz_entry_t *e = &YZ_ENTRIES[entry];

    if (s_ink != book->cfg.ink) {
        s_ink = book->cfg.ink;
        apply_palette(s_ink);
    }
    for (int i = 0; i < L_COUNT; i++) {
        if (line_visible(i, book->cfg.grid)) lv_obj_remove_flag(s_lines[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_lines[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text_fmt(s_pos, YZ_STR_POS_FMT, yz_ui_tab_name(book, book->tab),
                          (unsigned)(book->pos + 1), (unsigned)book->list_len);
    if (yz_book_is_fav(book, entry)) lv_obj_remove_flag(s_star, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_star, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(s_pos);
    lv_obj_align_to(s_star, s_pos, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    lv_label_set_text_static(s_simp, e->simp);
    lv_label_set_text_static(s_pinyin, e->pinyin);
    lv_label_set_text_static(s_struct, yz_ui_struct_name(e->structure));
    lv_label_set_text_fmt(s_phrase, YZ_STR_PHRASE_FMT, e->phrase);

    if (book->timing) {
        const unsigned left_s = (unsigned)((book->timer_left_ms + 999u) / 1000u);
        lv_label_set_text_fmt(s_timer, YZ_STR_TIMER_FMT, left_s / 60u, left_s % 60u);
        lv_obj_remove_flag(s_timer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_count, LV_OBJ_FLAG_HIDDEN);
        const uint32_t total = book->timer_total_ms ? book->timer_total_ms : 1;
        const int w = (int)((uint64_t)YZ_GRID_SIZE * (total - book->timer_left_ms) / total);
        lv_obj_set_width(s_progress, w > 0 ? w : 1);
        lv_obj_remove_flag(s_progress, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_timer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_progress, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_count, LV_OBJ_FLAG_HIDDEN);
        if (book->prog.count[entry]) {
            lv_label_set_text_fmt(s_count, YZ_STR_COUNT_FMT, (unsigned)book->prog.count[entry]);
        } else {
            lv_label_set_text_static(s_count, YZ_STR_COUNT_NONE);
        }
    }

    if (book->card) {
        lv_label_set_text_static(s_card_focus, yz_ui_focus_tip(e->focus));
        lv_label_set_text_static(s_card_struct, yz_ui_struct_tip(e->structure));
        lv_label_set_text_static(s_card_source, e->source);
        lv_obj_remove_flag(s_card, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *card_section(lv_obj_t *card, const char *tag) {
    lv_obj_t *t = yz_ui_label(card, &yz_zh14, YZ_C_PAPER, 0, 0);
    lv_obj_set_style_bg_color(t, lv_color_hex(YZ_C_VERMILION), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, 3, 0);
    lv_obj_set_style_pad_hor(t, 6, 0);
    lv_obj_set_style_pad_ver(t, 1, 0);
    lv_label_set_text_static(t, tag);
    lv_obj_t *body = yz_ui_label(card, &yz_zh14, YZ_C_INK, 0, 0);
    lv_obj_set_width(body, YZ_CARD_TEXT_W);
    lv_obj_set_style_text_line_space(body, YZ_CARD_LINE_SPACE, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP);
    return body;
}

static bool build(lv_obj_t *scr, const yz_book_t *book) {
    s_scr = scr;
    s_ink = 0xFF;

    s_pos = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 24, 11);
    s_star = yz_ui_label(scr, &yz_zh14, YZ_C_VERMILION, 0, 11);
    lv_label_set_text_static(s_star, YZ_STR_FAV_MARK);

    s_grid = yz_ui_box(scr, YZ_GRID_X, YZ_GRID_Y, YZ_GRID_SIZE, YZ_GRID_SIZE, YZ_C_PAPER, 0);
    lv_obj_set_style_bg_opa(s_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_grid, GRID_BORDER, 0);
    for (int i = 0; i < L_COUNT; i++) {
        s_lines[i] = lv_line_create(s_grid);
        lv_obj_remove_style_all(s_lines[i]);
        lv_line_set_points(s_lines[i], LINE_POINTS[i], 2);
        lv_obj_set_style_line_width(s_lines[i], 1, 0);
        // 横竖线用虚线（米字格、田字格的中线习惯画虚线）；对角线保持实线。
        if (i != L_D1 && i != L_D2) {
            lv_obj_set_style_line_dash_width(s_lines[i], 4, 0);
            lv_obj_set_style_line_dash_gap(s_lines[i], 4, 0);
        }
    }
    s_glyph = yz_ui_glyph_image(s_grid, LV_SCALE_NONE, YZ_C_STONE_TEXT);
    lv_obj_align(s_glyph, LV_ALIGN_CENTER, 0, 0);

    s_progress = yz_ui_box(scr, YZ_GRID_X, YZ_GRID_Y + YZ_GRID_SIZE + 2, 1, 3, YZ_C_VERMILION, 1);

    s_simp = yz_ui_label(scr, &yz_zh32, YZ_C_INK, 22, BOTTOM_Y);
    s_pinyin = yz_ui_label(scr, &yz_zh18, YZ_C_VERMILION, 64, BOTTOM_Y + 1);
    s_struct = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 64, BOTTOM_Y + 23);
    s_count = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 120, BOTTOM_Y + 5);
    lv_obj_set_width(s_count, 98);
    lv_obj_set_style_text_align(s_count, LV_TEXT_ALIGN_RIGHT, 0);
    s_timer = yz_ui_label(scr, &yz_zh24, YZ_C_VERMILION, 130, BOTTOM_Y);
    lv_obj_set_width(s_timer, 88);
    lv_obj_set_style_text_align(s_timer, LV_TEXT_ALIGN_RIGHT, 0);
    s_phrase = yz_ui_label(scr, &yz_zh14, YZ_C_INK, 22, BOTTOM_Y + 48);
    lv_obj_set_width(s_phrase, 196);
    lv_label_set_long_mode(s_phrase, LV_LABEL_LONG_MODE_CLIP);

    // 笔法卡：覆盖在格子上（不压住下方的简体与拼音），确定键单击开合。
    s_card = yz_ui_box(scr, 14, 30, 212, YZ_CARD_H, YZ_C_PAPER, 8);
    lv_obj_set_style_border_color(s_card, lv_color_hex(YZ_C_VERMILION), 0);
    lv_obj_set_style_border_width(s_card, 1, 0);
    lv_obj_set_style_pad_all(s_card, 10, 0);
    lv_obj_set_style_pad_row(s_card, 4, 0);
    lv_obj_set_flex_flow(s_card, LV_FLEX_FLOW_COLUMN);
    s_card_focus = card_section(s_card, YZ_STR_CARD_FOCUS);
    s_card_struct = card_section(s_card, YZ_STR_CARD_STRUCT);
    s_card_source = card_section(s_card, YZ_STR_CARD_SOURCE);

    update(book);
    return book->cfg.ink == YZ_INK_STONE;
}

static void forget(void) {
    s_scr = s_grid = s_glyph = s_pos = s_star = s_simp = s_pinyin = s_struct = NULL;
    s_count = s_timer = s_progress = s_phrase = NULL;
    s_card = s_card_focus = s_card_struct = s_card_source = NULL;
    for (int i = 0; i < L_COUNT; i++) s_lines[i] = NULL;
    s_ink = 0xFF;
}

const yz_page_t YZ_PAGE_PRACTICE = { build, update, forget };
