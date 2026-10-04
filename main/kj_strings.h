// main/kj_strings.h —— 限定猜拳界面上出现的全部文字（唯一允许出现非 ASCII 显示字面量的文件）。
//
// tools/gen_kj_fonts.py 从本文件提取字符集生成中文字体子集：
//   * 所有字符串字面量 + 可打印 ASCII → 正文字体 kj_zh14 / kj_zh18 / kj_zh26；
//   * KJ_BIG_ 开头的宏 → 大标题字体 kj_big48（只含这些字）；
//   * 牌面手势由 kj_hand36 / kj_hand64（Noto Emoji）提供，见 KJ_HAND_*。
// 改动任何文字后运行 `python3 tools/gen_kj_fonts.py generate ...` 重新生成字体，
// `python3 tools/gen_kj_fonts.py check`（已纳入 tools/validate.sh）会拦下缺字。
#pragma once

// ---- 大标题（kj_big48）----
#define KJ_BIG_TITLE_1      "限定"
#define KJ_BIG_TITLE_2      "猜拳"
#define KJ_BIG_CHALLENGE    "挑战！"
#define KJ_BIG_WIN          "胜"
#define KJ_BIG_LOSE         "负"
#define KJ_BIG_DRAW         "平"
#define KJ_BIG_CLEARED      "过关"
#define KJ_BIG_OUT          "出局"
#define KJ_BIG_FAILED       "失败"
#define KJ_BIG_VS           "VS"

// ---- 牌面手势（kj_hand36 / kj_hand64，Noto Emoji）----
#define KJ_HAND_ROCK        "\xE2\x9C\x8A"   // U+270A ✊
#define KJ_HAND_SCISSORS    "\xE2\x9C\x8C"   // U+270C ✌
#define KJ_HAND_PAPER       "\xE2\x9C\x8B"   // U+270B ✋

// ---- 通用 ----
#define KJ_STR_STAR         "★"
#define KJ_STR_STAR_EMPTY   "☆"
#define KJ_STR_ZAWA         "ざわ… ざわ…"
#define KJ_STR_CARD_ROCK    "石头"
#define KJ_STR_CARD_SCISSORS "剪刀"
#define KJ_STR_CARD_PAPER   "布"
#define KJ_STR_NO_FMT       "%02u号"          // 选手编号，如 07号
#define KJ_STR_BOT          "电脑"
#define KJ_STR_ME           "我"
#define KJ_STR_ROOM_FMT     "赌局 %s"
#define KJ_STR_COUNT_FMT    "×%u"
#define KJ_STR_BATTERY_NA   "--"

// ---- 首页 / 角色 ----
#define KJ_STR_SUBTITLE     "限定猜拳 · 多人对决"
#define KJ_STR_ROLE_PLAYER  "我是选手 · 加入赌局"
#define KJ_STR_ROLE_HOST    "我是庄家 · 开设赌局"
#define KJ_STR_HINT_TITLE   "▲▼ 选择 · OK 确定"

// ---- 找赌局 ----
#define KJ_STR_ROOMS_TITLE  "寻找赌局"
#define KJ_STR_ROOMS_SUB    "选择附近的庄家入座"
#define KJ_STR_ROOMS_EMPTY  "正在搜索附近的赌局"
#define KJ_STR_ROOMS_EMPTY2 "请先让庄家开设赌局"
#define KJ_STR_ROOM_LINE_FMT "%s · %u 人"
#define KJ_STR_JOINING      "入座中…"
#define KJ_STR_HINT_ROOMS   "OK 入座 · 长按 返回"

// ---- 阶段 ----
#define KJ_STR_PHASE_LOBBY  "等待入座"
#define KJ_STR_PHASE_RUN    "赌局进行中"
#define KJ_STR_PHASE_ENDED  "赌局已结束"

// ---- 入座等待 ----
#define KJ_STR_YOUR_NO      "你的编号"
#define KJ_STR_SEATED_FMT   "已入座 %u 人"
#define KJ_STR_WAIT_START   "等待庄家开局"
#define KJ_STR_WAIT_NEXT    "本局已结束，等待下一局"
#define KJ_STR_HINT_SEAT    "长按 离座"

// ---- 手牌主页 ----
#define KJ_STR_STARS_LABEL  "星星"
#define KJ_STR_HAND_INFO_FMT "手牌 %u 张 · %u胜 %u负 %u平"
#define KJ_STR_FIND_OPP     "寻找对手"
#define KJ_STR_AVAIL_FMT    "可挑战 %u 人"
#define KJ_STR_HINT_HAND    "OK 寻找对手"

// ---- 选择对手 ----
#define KJ_STR_OPP_TITLE    "选择对手"
#define KJ_STR_OPP_SUB      "离你越近排得越前"
#define KJ_STR_OPP_EMPTY    "暂时没有可以挑战的人"
#define KJ_STR_OPP_EMPTY2   "对方可能正在对决"
#define KJ_STR_SIG_NEAR     "很近"
#define KJ_STR_SIG_MID      "附近"
#define KJ_STR_SIG_FAR      "较远"
#define KJ_STR_SIG_NONE     "未听到"
#define KJ_STR_HINT_OPP     "OK 挑战 · 长按 返回"

// ---- 等待应战 ----
#define KJ_STR_WAIT_TITLE   "已发出挑战"
#define KJ_STR_WAIT_FMT     "等待应战 %u 秒"
#define KJ_STR_HINT_WAIT    "长按 撤回挑战"

// ---- 收到挑战 ----
#define KJ_STR_CHAL_FROM    "号选手向你发起对决"
#define KJ_STR_CHAL_BOT     "电脑选手向你发起对决"
#define KJ_STR_CHAL_LEFT_FMT "%u 秒内应战"
#define KJ_STR_ACCEPT       "OK 应战"
#define KJ_STR_DECLINE      "长按 拒绝"

// ---- 出牌 ----
#define KJ_STR_DUEL_VS_FMT  "对决 vs %02u号"
#define KJ_STR_OPP_THINKING "对方思考中"
#define KJ_STR_OPP_LOCKED   "对方已出牌"
#define KJ_STR_PICK_PROMPT  "选一张牌，同时亮出"
#define KJ_STR_LOCKED_WAIT  "已出牌，等待亮牌"
#define KJ_STR_CARD_LEFT_FMT "%s · 剩 %u 张"
#define KJ_STR_HINT_CHOOSE  "▲▼ 选牌 · OK 出牌 · 长按 放弃"
#define KJ_STR_HINT_LOCKED  "暗牌已扣下，不能反悔"

// ---- 亮牌 ----
#define KJ_STR_STAR_PLUS    "+1 ★"
#define KJ_STR_STAR_MINUS   "-1 ★"
#define KJ_STR_STAR_SAME    "星星不变"
#define KJ_STR_HINT_REVEAL  "OK 继续"

// ---- 终局 ----
#define KJ_STR_CLEARED_FMT  "手牌出完，携 %u ★ 离场"
#define KJ_STR_OUT_SUB      "星星输光，被送往别室"
#define KJ_STR_FAIL_NOCARD  "手牌出完，星星不足 3 颗"
#define KJ_STR_FAIL_TIMEUP  "时间到，手牌没有出完"
#define KJ_STR_RECORD_FMT   "%u胜 %u负 %u平"
#define KJ_STR_FINAL_WAIT   "等待庄家宣布下一局"

// ---- 提示（toast）----
#define KJ_STR_T_DECLINED   "对方拒绝了挑战"
#define KJ_STR_T_CANCELLED  "对方撤回了挑战"
#define KJ_STR_T_TIMEOUT    "挑战超时，已作废"
#define KJ_STR_T_WITHDRAWN  "对方放弃了对决"
#define KJ_STR_T_ABORTED    "对方断线，对决作废"
#define KJ_STR_T_BUSY       "对方正忙，换个人吧"
#define KJ_STR_T_NOT_RUN    "赌局还没开始"
#define KJ_STR_T_NO_CARD    "这种牌已经用完了"
#define KJ_STR_T_INVALID    "现在不能这样操作"
#define KJ_STR_T_FULL       "赌局已满"
#define KJ_STR_T_KICKED     "你已离开这个赌局"
#define KJ_STR_T_NO_REPLY   "庄家没有回应，请靠近再试"
#define KJ_STR_T_LOST       "与庄家失联，重连中"
#define KJ_STR_T_RESTORED   "已恢复上次的赌局"
#define KJ_STR_T_NEED_TWO   "至少 2 人入座才能开局"
#define KJ_STR_T_DONE       "已执行"
#define KJ_STR_T_RADIO_FAIL "无线启动失败"

// ---- 庄家 ----
#define KJ_STR_HOST_TAG     "庄家"
#define KJ_STR_HOST_ROOM    "赌局号"
#define KJ_STR_STAT_SEATED  "入座"
#define KJ_STR_STAT_ONLINE  "在线"
#define KJ_STR_STAT_DUELS   "对决中"
#define KJ_STR_STAT_DONE    "已离场"
#define KJ_STR_M_START      "开始赌局"
#define KJ_STR_M_END        "结束赌局"
#define KJ_STR_M_NEW        "新一局"
#define KJ_STR_M_BOT_ADD    "添加电脑选手"
#define KJ_STR_M_BOT_DEL    "移除电脑选手"
#define KJ_STR_M_RESET      "清空所有选手"
#define KJ_STR_CONFIRM_FMT  "确定%s？"
#define KJ_STR_CONFIRM_HINT "OK 确定 · 长按 取消"
#define KJ_STR_USB_ON       "电脑已连接 · 看板见 USB 串口"
#define KJ_STR_USB_OFF      "未连接电脑 · 看板不可用"
#define KJ_STR_HINT_HOST    "▲▼ 选择 · OK 执行"
