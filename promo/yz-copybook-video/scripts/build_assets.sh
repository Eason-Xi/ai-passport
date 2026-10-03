#!/usr/bin/env bash
# 一键准备全部生成素材（public/gen/）。设备图需要百炼 bl CLI 与 API Key，已有 source.png 时跳过生成。
# 用法：PY=<装有 numpy/Pillow/scipy 的 python> scripts/build_assets.sh
set -euo pipefail
cd "$(dirname "$0")/.."
PY="${PY:-.venv/bin/python}"
"$PY" scripts/extract_glyphs.py
./scripts/render_ui.sh
if [ ! -f public/gen/device/source.png ]; then ./scripts/make_device.sh; fi
"$PY" scripts/process_device.py public/gen/device/source.png
./scripts/make_vo.sh
"$PY" scripts/synth_audio.py
python3 scripts/fetch_fonts.py
mkdir -p public/gen/brand && cp ../../assets/images/logo-wordmark-dark.png public/gen/brand/
