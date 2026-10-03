#!/usr/bin/env bash
# 用百炼 qwen-image 以 docs/brand/ai-passport-front.png 为基底重绘高清设备图：
# 外壳、按键、挂绳孔、Logo 保持原样，屏幕区域纯绿平涂、背景纯黑，便于后续抠像与合成真实界面。
# 会产生少量 API 费用；生成后请对照参考图核对硬件细节，不符就重跑。输出 public/gen/device/source.png。
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WORK="$(mktemp -d)"
ffmpeg -v error -y -f lavfi -i color=black:s=1024x1536 -i "$REPO/docs/brand/ai-passport-front.png" \
  -filter_complex "[1]scale=-1:1420:flags=lanczos[d];[0][d]overlay=(W-w)/2:(H-h)/2" -frames:v 1 "$WORK/input.png"
bl image edit --image "$WORK/input.png" --size "1024*1536" --n 2 --watermark false --prompt-extend false \
  --out-dir "$WORK/gen" \
  --prompt "这是 AI Passport 可穿戴设备的正面产品图。请输出同一台设备的高清商业产品渲染图：严格保持透明外壳、内部电路板与元件、顶部挂绳孔、四角螺丝、侧面按键、绿色指示灯、黄色排线、右下扬声器格栅、底部黑色面板以及面板上的图标和 'The Open Wearable AI Passport' 字样，全部与原图一致，正面正视角，设备位置、大小与比例完全不变。把屏幕显示区域（原来显示 FoloToy 界面的圆角矩形发光区域）整体替换为纯绿色 #00FF00 均匀平涂，屏幕内不要任何图案、文字、反光或渐变。背景保持纯黑色 #000000，无阴影、无倒影。画质锐利，细节清晰，影棚柔光。" \
  --negative-prompt "屏幕内容, 文字, 界面, 反光, 渐变, 阴影, 倒影, 变形, 多余按键, 模糊"
mkdir -p "$HERE/public/gen/device"
ls "$WORK/gen"
echo "从 $WORK/gen 挑一张与参考图硬件一致的，复制为 public/gen/device/source.png"
