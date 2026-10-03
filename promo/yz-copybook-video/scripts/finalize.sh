#!/usr/bin/env bash
# 成片响度：测出原始混音的综合响度，补增益到约 −14 LUFS（短视频平台常用标准），
# 再在 4 倍过采样下限幅到 −5 dBFS。太鼓与咔哒声的瞬态很尖，AAC 编码后会有 3 dB 左右的
# 样本间过冲，留足余量才能让真峰值低于 −1 dBTP。画面流直接复制。
# 用法：scripts/finalize.sh [out/raw.mp4] [out/yz-copybook-promo.mp4]
set -euo pipefail
IN="${1:-out/raw.mp4}"
OUT="${2:-out/yz-copybook-promo.mp4}"
TARGET=-14
LIMIT=-5
measure() { ffmpeg -hide_banner -nostats -i "$1" -af ebur128=peak=true -f null - 2>&1 | grep -E "^\s+(I|Peak):"; }
I=$(measure "$IN" | awk '/I:/{print $2}')
# 限幅会吃掉约 1.8 dB 响度（实测），一并补上。
GAIN=$(python3 -c "print(round($TARGET - ($I) + 1.8, 2))")
ffmpeg -hide_banner -v error -y -i "$IN" -c:v copy \
  -af "volume=${GAIN}dB,aresample=192000,alimiter=limit=${LIMIT}dB:attack=1:release=80:level=false,aresample=48000" \
  -c:a aac -b:a 256k -movflags +faststart "$OUT"
echo "原始 ${I} LUFS，增益 ${GAIN} dB →"
measure "$OUT"
echo "→ $OUT"
