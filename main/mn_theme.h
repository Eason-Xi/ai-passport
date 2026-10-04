// main/mn_theme.h —— 节拍器"暗色舞台"配色。
//
// 深色背景省电、在排练室的暗光下不刺眼；琥珀色只用于小节首拍重音与正在编辑的值，
// 青色用于普通拍与运行状态，二者在色盲常见类型下仍可由亮度区分。
#pragma once

#define MN_C_BG 0x0E1116          // 屏幕背景
#define MN_C_PANEL 0x1A1F27       // 设置面板
#define MN_C_PANEL_HI 0x29313D    // 选中行
#define MN_C_TEXT 0xF2EFE8        // 主文字（暖白）
#define MN_C_TEXT_DIM 0x8B93A1    // 次要文字
#define MN_C_TEXT_FAINT 0x5A6270  // 提示文字
#define MN_C_ACCENT 0xFFB23F      // 首拍重音 / 编辑中（琥珀）
#define MN_C_BEAT 0x3DD6C4        // 普通拍 / 运行中（青）
#define MN_C_SUB 0x9FE8DF         // 细分音
#define MN_C_DOT_OFF 0x2B323D     // 未亮的节拍点
#define MN_C_ROD 0xC9CED6         // 摆杆
#define MN_C_DANGER 0xFF6B5E      // 告警（音频不可用、低电量）
#define MN_C_OK 0x6BD48A          // 电量正常
