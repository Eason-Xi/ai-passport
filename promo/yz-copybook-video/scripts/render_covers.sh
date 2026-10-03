#!/usr/bin/env bash
# 渲染全部视频封面（两套风格 × 16:9 / 4:3 / 3:4 / 9:16），各出 PNG 与 JPG（质量 95，约 200–300 KB）。
# 输出 out/covers/cover-<a|b>-<比例>.{png,jpg}。版式在 src/covers/Covers.tsx。
set -euo pipefail
cd "$(dirname "$0")/.."
npx remotion bundle --out-dir build --log=error >/dev/null
mkdir -p out/covers
for v in a b; do
  V=$(echo "$v" | tr a-z A-Z)
  for r in 16x9 4x3 3x4 9x16; do
    npx remotion still build "Cover$V-$r" "out/covers/cover-$v-$r.png" --log=error
    ffmpeg -v error -y -i "out/covers/cover-$v-$r.png" -q:v 2 "out/covers/cover-$v-$r.jpg"
  done
done
ls out/covers
