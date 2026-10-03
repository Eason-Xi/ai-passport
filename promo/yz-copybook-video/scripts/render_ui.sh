#!/usr/bin/env bash
# 用字帖分支的真实 LVGL 界面代码重渲全部页面截图（4 倍，960×1280，含屏幕圆角遮挡）。
# 需要字帖分支所在检出目录里已有 managed_components/lvgl__lvgl（idf.py build 过一次即可）。
# 只写到本项目的 public/gen/ui，不改动那个检出目录。
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
CHECKOUT="${YZ_CHECKOUT:-$(git -C "$HERE" worktree list --porcelain | awk '/^worktree /{w=$2} /^branch refs\/heads\/feature\/yan-zhenqing-copybook$/{print w}')}"
[ -n "$CHECKOUT" ] || { echo "找不到 feature/yan-zhenqing-copybook 的检出目录，请设置 YZ_CHECKOUT" >&2; exit 1; }
WORK="${TMPDIR:-/tmp}/yz-preview-build"
python3 "$CHECKOUT/tools/render_yz_preview.py" --out "$WORK" --scale 4
mkdir -p "$HERE/public/gen/ui"
cp "$WORK"/shots/*.png "$HERE/public/gen/ui/"
ls "$HERE/public/gen/ui" | wc -l
