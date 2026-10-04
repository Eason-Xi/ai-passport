// main/mn_strings.h —— 节拍器界面的全部中文 / 非 ASCII 文案（唯一来源）。
//
// tools/gen_metronome_fonts.py 从本文件的字符串字面量提取字符集生成字体子集；
// 其它 mn_*.c 里出现非 ASCII 字面量（日志除外）会被 check 拒绝。
// 修改任何文案后必须重新运行 generate，否则 validate.sh 会报字体过期。
#pragma once

#include "mn_model.h"
#include "mn_sound.h"
#include "mn_tempo.h"

// 速度术语（下标 = mn_tempo_term_t）。
static const char *const MN_TEMPO_IT[MN_TEMPO_COUNT] = {
    "Grave", "Largo", "Larghetto", "Adagio", "Andante",
    "Moderato", "Allegro", "Vivace", "Presto", "Prestissimo",
};
static const char *const MN_TEMPO_ZH[MN_TEMPO_COUNT] = {
    "庄板", "广板", "小广板", "柔板", "行板", "中板", "快板", "活板", "急板", "最急板",
};

// 细分名称（下标 = subdiv - 1）。
static const char *const MN_SUBDIV_NAME[4] = { "不细分", "八分", "三连音", "十六分" };

// 音色名称（下标 = mn_sound_t）。
static const char *const MN_SOUND_NAME[MN_SOUND_COUNT] = { "电子", "木块", "牛铃" };

// 设置页行标题（下标 = mn_row_t）。
static const char *const MN_ROW_LABEL[MN_ROW_COUNT] = {
    "拍号", "首拍重音", "细分", "音色", "音量", "敲击测速",
};

#define MN_STR_SEP " · "
#define MN_STR_ON "开"
#define MN_STR_OFF "关"
#define MN_STR_MUTE "静音"
#define MN_STR_ENTER "进入"
#define MN_STR_EDIT_L "◀ "
#define MN_STR_EDIT_R " ▶"
#define MN_STR_BPM "BPM"
#define MN_STR_RUNNING "演奏中"
#define MN_STR_STOPPED "已停止"
#define MN_STR_AUDIO_FAIL "音频不可用"
#define MN_STR_SETTINGS "设置"

#define MN_STR_HINT_STOPPED "OK 开始 · 双击 测速 · 长按 设置"
#define MN_STR_HINT_RUNNING "OK 停止 · ▲▼ 调速 · 长按 设置"
#define MN_STR_HINT_BROWSE "▲▼ 选择 · OK 编辑 · 长按 返回"
#define MN_STR_HINT_EDIT "▲▼ 调整 · OK 确定"
#define MN_STR_HINT_TAP_ROW "▲▼ 选择 · OK 进入 · 长按 返回"

#define MN_STR_TAP_TITLE "敲击测速"
#define MN_STR_TAP_HINT "跟着节奏按任意键"
#define MN_STR_TAP_COUNT_FMT "已敲 %u 下"
#define MN_STR_TAP_NEED "至少敲 3 下"
#define MN_STR_TAP_WAIT "停下后自动应用"
#define MN_STR_TAP_DONE "已设定"
#define MN_STR_TAP_CANCEL "长按 OK 取消"
