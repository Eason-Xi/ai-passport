// main/as_strings.h —— 界面上的全部中文文字（唯一允许出现非 ASCII 字符串字面量的界面文件）。
//
// tools/gen_asmr_fonts.py 从本文件提取字符集生成字体子集；改动这里的文字后必须重新
// generate，否则 check 会失败（防止界面出现缺字方块）。
#pragma once

#include "as_cfg.h"
#include "as_sounds.h"

// 声音名称与一句话描述（顺序同 as_sound_id_t）。
static const char *const AS_STR_SOUND[AS_SOUND_COUNT] = {
    "细雨", "海浪", "篝火", "溪流", "山风", "虫鸣", "颂钵", "白噪音", "粉噪音", "棕噪音",
};
static const char *const AS_STR_SOUND_DESC[AS_SOUND_COUNT] = {
    "屋檐下沙沙的雨声",
    "远处一浪接一浪",
    "木柴噼啪作响",
    "山涧里水泡咕嘟",
    "林间呼啸的风",
    "夏夜草丛里的蟋蟀",
    "悠长的钵声余韵",
    "均匀遮住环境杂音",
    "柔和、偏暖的沙声",
    "低沉如远处闷雷",
};
#define AS_STR_SOUND_NONE "空"
#define AS_STR_SOUND_NONE_DESC "关闭这一轨"

// 聆听页
#define AS_STR_PLAYING "正在播放"
#define AS_STR_PAUSED "已暂停 · 按 OK 播放"
#define AS_STR_NO_MIX "还没有选择声音"
#define AS_STR_MIX_SEP " · "
#define AS_STR_NO_TIMER "不定时"
#define AS_STR_HOME_HINT "▲▼ 音量   长按 OK 菜单"
#define AS_STR_VOLUME "音量"
#define AS_STR_AUDIO_FAIL "音频不可用"
#define AS_STR_PLAY_GLYPH "▶"

// 菜单
#define AS_STR_MENU_TITLE "菜单"
#define AS_STR_MENU_MIXER "调音台"
#define AS_STR_MENU_TIMER "睡眠定时"
#define AS_STR_MENU_BREATH "呼吸引导"
#define AS_STR_MENU_BACK "返回"
#define AS_STR_MENU_HINT "▲▼ 选择   OK 进入"

// 调音台 / 选声音
#define AS_STR_MIXER_TITLE "调音台"
#define AS_STR_TRACK_FMT "轨道 %d"
#define AS_STR_DONE "完成"
#define AS_STR_MIXER_HINT "▲▼ 选择   OK 换声音"
#define AS_STR_MIXER_EDIT_HINT "▲▼ 调节音量   OK 确定"
#define AS_STR_PICKER_TITLE_FMT "选择声音 · 轨道 %d"
#define AS_STR_PICKER_HINT "OK 确定   长按取消"

// 睡眠定时
#define AS_STR_TIMER_TITLE "睡眠定时"
#define AS_STR_MINUTES "分钟"
#define AS_STR_MINUTES_FMT "%u 分钟"
#define AS_STR_TIMER_ON_INFO "到点前 1 分钟渐弱后停止"
#define AS_STR_TIMER_OFF_INFO "一直播放，直到手动暂停"
#define AS_STR_TIMER_HINT "▲▼ 选择   OK 确定"

// 呼吸引导（顺序同 as_breath_pattern_t / as_breath_phase_t）
static const char *const AS_STR_BREATH_NAME[AS_BREATH_COUNT] = {
    "4-7-8 呼吸", "均衡呼吸", "方块呼吸",
};
static const char *const AS_STR_BREATH_SHORT[AS_BREATH_COUNT] = {
    "4-7-8", "5-5", "4-4-4-4",
};
static const char *const AS_STR_BREATH_DESC[AS_BREATH_COUNT] = {
    "吸 4 秒 · 屏 7 秒 · 呼 8 秒",
    "吸 5 秒 · 呼 5 秒",
    "吸 · 屏 · 呼 · 屏，各 4 秒",
};
static const char *const AS_STR_PHASE[4] = { "吸气", "屏息", "呼气", "屏息" };
#define AS_STR_BREATH_HINT "▲▼ 换节奏   OK 返回"

// 晚安 / 低电量
#define AS_STR_NIGHT "晚安"
#define AS_STR_NIGHT_SUB "定时已结束，好梦"
#define AS_STR_LOWBATT "电量过低"
#define AS_STR_LOWBATT_SUB "即将关机，请充电"
