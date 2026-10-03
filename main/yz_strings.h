// main/yz_strings.h —— 界面上所有非 ASCII 文字都集中在这里（另有生成的 yz_catalog_data.c）。
//
// 帖名、简介、拓本来源、卷名、单字释文与出处等字帖资料在 tools/yz_catalog.json 里。
// tools/gen_yz_fonts.py 从本文件与 tools/yz_catalog.json 收集字符生成中文字体子集；
// 在其他源文件里写中文字面量会被 `gen_yz_fonts.py check` 拒绝。改动本文件后需重新生成字体。
#pragma once

// ---- 主页 ----
#define YZ_STR_SEAL             "鲁公"
#define YZ_STR_HOME_CONTINUE    "继续临帖"
#define YZ_STR_HOME_CATALOG     "目录选字"
#define YZ_STR_HOME_FAVORITES   "我的收藏"
#define YZ_STR_HOME_VOLUMES     "分卷目录"
#define YZ_STR_HOME_ABOUT       "碑帖简介"
#define YZ_STR_HOME_SETTINGS    "设置"
#define YZ_STR_HOME_CONT_FMT    "「%s」%u/%u"
#define YZ_STR_HOME_FAV_FMT     "%u 字"
#define YZ_STR_HOME_VOL_FMT     "%u 卷"
#define YZ_STR_HOME_STATS_FMT   "已临 %lu 遍 · %lu 分钟"

// ---- 分卷目录 ----
#define YZ_STR_VOL_TITLE        "分卷目录"
#define YZ_STR_VOL_TOTAL_FMT    "全碑已临 %u / %u 字"
#define YZ_STR_VOL_COUNT_FMT    "%u/%u"
#define YZ_STR_VOL_HINT         "确定跳到未临的字 · 长按返回"

// ---- 目录 ----
#define YZ_STR_FAV_TAB          "收藏"
#define YZ_STR_CAT_HEAD_FMT     "%s · %u 字"
#define YZ_STR_CAT_EMPTY        "还没有收藏\n临帖时双击确定键即可收藏"
#define YZ_STR_COUNT_FMT        "已临 %u 遍"
#define YZ_STR_COUNT_NONE       "尚未临写"

// ---- 临帖 ----
#define YZ_STR_POS_FMT          "%s %u/%u"
#define YZ_STR_FAV_MARK         "★"
#define YZ_STR_PHRASE_FMT       "%s「%s」"    // 前缀取自字帖资料：“碑文”
#define YZ_STR_PREVIEW_PHRASE_FMT "「%s」"
#define YZ_STR_TIMER_FMT        "%u:%02u"
#define YZ_STR_CARD_FOCUS       "笔法"
#define YZ_STR_CARD_STRUCT      "结构"
#define YZ_STR_CARD_SOURCE      "出处"

#define YZ_STR_STRUCT_SINGLE    "独体字"
#define YZ_STR_STRUCT_LR        "左右结构"
#define YZ_STR_STRUCT_TB        "上下结构"
#define YZ_STR_STRUCT_EN        "包围结构"

#define YZ_STR_GRID_MI          "米字格"
#define YZ_STR_GRID_TIAN        "田字格"
#define YZ_STR_GRID_JIU         "九宫格"
#define YZ_STR_GRID_NONE        "无格线"
#define YZ_STR_INK_STONE        "拓本"
#define YZ_STR_INK_PAPER        "墨迹"
#define YZ_STR_INK_TRACE        "描红"

// ---- 提示条 ----
#define YZ_STR_TOAST_FAV_ON     "已收藏"
#define YZ_STR_TOAST_FAV_OFF    "已取消收藏"
#define YZ_STR_TOAST_GRID_FMT   "格线：%s"
#define YZ_STR_TOAST_INK_FMT    "底色：%s"
#define YZ_STR_TOAST_TIMER_FMT  "计时临写 %u 秒"
#define YZ_STR_TOAST_TIMER_OFF  "计时已停止"
#define YZ_STR_TOAST_DONE_FMT   "临写完成 · 共 %u 遍"
#define YZ_STR_TOAST_MARK_FMT   "已记一遍 · 共 %u 遍"
#define YZ_STR_TOAST_RESET      "进度已清除"

// ---- 设置 ----
#define YZ_STR_SET_TITLE        "设置"
#define YZ_STR_SET_GRID         "格线"
#define YZ_STR_SET_INK          "底色"
#define YZ_STR_SET_TIMER        "计时时长"
#define YZ_STR_SET_AUTO         "自动翻页"
#define YZ_STR_SET_SOUND        "提示音"
#define YZ_STR_SET_BRIGHT       "屏幕亮度"
#define YZ_STR_SET_RESET        "清除进度"
#define YZ_STR_SECONDS_FMT      "%u 秒"
#define YZ_STR_ON               "开"
#define YZ_STR_OFF              "关"
#define YZ_STR_PERCENT_FMT      "%u%%"
#define YZ_STR_RESET_ASK        "全部"
#define YZ_STR_RESET_CONFIRM    "再按确定"
#define YZ_STR_SET_HINT         "确定切换 · 长按确定返回"

// ---- 简介（每行不超过 13 个全角字，显式换行：LVGL 不做中文避头尾）----
// 第 1 页（简介）与第 5 页（拓本来源）的正文来自字帖资料：YZ_BOOK.intro / source_text。
#define YZ_STR_ABOUT_TITLE_1    "碑帖简介"
#define YZ_STR_ABOUT_TITLE_2    "颜体要诀"
#define YZ_STR_ABOUT_BODY_2 \
    "横轻竖重：\n" \
    "横画细劲，竖画粗壮。\n" \
    "蚕头燕尾：\n" \
    "捺画起笔圆厚，一波三折。\n" \
    "外拓取势：\n" \
    "竖画向外微弧，字形宽博。\n" \
    "\n" \
    "先读帖，再对临；\n" \
    "一字多遍，由形入神。"
// 按键说明两页：左栏按键、右栏功能，两栏逐行对齐。
#define YZ_STR_ABOUT_TITLE_3    "临帖按键"
#define YZ_STR_ABOUT_BODY_3 \
    "上 / 下  单击\n" \
    "确定  单击\n" \
    "确定  双击\n" \
    "确定  长按\n" \
    "上键  长按\n" \
    "上键  双击\n" \
    "下键  长按\n" \
    "下键  双击"
#define YZ_STR_ABOUT_BODY_3R \
    "换字\n" \
    "笔法卡\n" \
    "收藏\n" \
    "返回目录\n" \
    "换格线\n" \
    "换底色\n" \
    "计时临写\n" \
    "记一遍"
#define YZ_STR_ABOUT_TITLE_4    "目录与主页"
#define YZ_STR_ABOUT_BODY_4 \
    "目录  单击\n" \
    "目录  双击\n" \
    "目录  长按上 / 下\n" \
    "分卷目录  确定\n" \
    "任意页  长按确定\n" \
    "主页  上 / 下\n" \
    "设置  确定\n" \
    "息屏时  任意键"
#define YZ_STR_ABOUT_BODY_4R \
    "逐字移动\n" \
    "跳一行\n" \
    "换卷\n" \
    "跳到未临字\n" \
    "返回上一层\n" \
    "选择\n" \
    "切换取值\n" \
    "唤醒屏幕"
#define YZ_STR_ABOUT_TITLE_5    "拓本来源"
#define YZ_STR_PAGE_FMT         "%u/%u"

// ---- 笔法要点（顺序同 yz_focus_t；两行，每行不超过 13 个全角字）----
#define YZ_STR_FOCUS_HENG   "横画逆锋起笔，行笔轻提，\n收笔重按回锋：横细竖粗。"
#define YZ_STR_FOCUS_SHU    "竖画粗壮挺拔，如屋漏痕；\n左右两竖向外微弧取势。"
#define YZ_STR_FOCUS_PIE    "撇画起笔重按，由重渐轻，\n出锋爽利而不飘浮。"
#define YZ_STR_FOCUS_NA     "捺画“蚕头燕尾”，\n一波三折，捺脚厚重饱满。"
#define YZ_STR_FOCUS_DIAN   "点如高峰坠石，\n轻入重按，圆满有力。"
#define YZ_STR_FOCUS_GOU    "钩画先顿后提，蓄势而出，\n钩短而锐，力聚锋尖。"
#define YZ_STR_FOCUS_ZHE    "转折处提笔另起，\n方中寓圆，不露圭角。"
#define YZ_STR_FOCUS_ZOUZHI "走之平捺舒展，一波三折，\n稳稳托住上部。"
#define YZ_STR_FOCUS_BAOGAI "宝盖宽覆，左点右钩呼应，\n下部收于盖内。"
#define YZ_STR_FOCUS_FANFU  "笔画繁多宜细，\n横竖间距匀称，密而不挤。"
#define YZ_STR_FOCUS_YONG   "永字八法：侧勒努趯策掠\n啄磔，八法俱备，练字总纲。"

// ---- 结构要点（顺序同 yz_struct_t；两行）----
#define YZ_STR_TIP_SINGLE   "独体字中宫饱满，\n重心端正，四面停匀。"
#define YZ_STR_TIP_LR       "左收右放，相向而抱，\n字形宽博，左右呼应。"
#define YZ_STR_TIP_TB       "中轴对正，上紧下松，\n重心平稳，上下相承。"
#define YZ_STR_TIP_EN       "外框宽博，内部紧凑，\n四面撑开，内外相称。"
