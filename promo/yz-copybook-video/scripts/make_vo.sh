#!/usr/bin/env bash
# 用 MiMo TTS（音色「白桦」）逐句生成旁白，再测时长写入 public/gen/vo/durations.json。
# 文案在 scripts/vo_lines.json；改文案后重跑本脚本，再按新时长调 src/timeline.ts。
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
TTS="${MIMO_TTS:-$HOME/.claude/skills/mimo-tts/scripts/mimo_tts.py}"
OUT="$HERE/public/gen/vo"
mkdir -p "$OUT"
python3 - "$HERE/scripts/vo_lines.json" <<'PY' | while IFS=$'\t' read -r id style text; do
import json, sys
for line in json.load(open(sys.argv[1])):
    print(f"{line['id']}\t{line['style']}\t{line['text']}")
PY
  [ -s "$OUT/$id.wav" ] && [ -z "${FORCE:-}" ] && continue
  python3 "$TTS" -v 白桦 --format wav -s "男声纪录片旁白。$style" "$text" -o "$OUT/$id.wav"
done
python3 - "$OUT" <<'PY'
# 每句：总时长 dur，以及去掉首尾静音后的发声起止 start / end（秒，相对该句音频开头）。
import json, re, subprocess, sys
from pathlib import Path
out = Path(sys.argv[1])
d = {}
for f in sorted(out.glob("vo*.wav")):
    dur = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0",
                                str(f)], capture_output=True, text=True, check=True).stdout)
    log = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", str(f), "-af",
                          "silencedetect=noise=-40dB:d=0.12", "-f", "null", "-"],
                         capture_output=True, text=True).stderr
    starts = [float(x) for x in re.findall(r"silence_start: ([0-9.]+)", log)]
    ends = [float(x) for x in re.findall(r"silence_end: ([0-9.]+)", log)]
    start = ends[0] if starts and starts[0] < 0.01 and ends else 0.0
    end = starts[-1] if starts and (not ends or ends[-1] >= dur - 0.01) and starts[-1] > start else dur
    d[f.stem] = {"dur": round(dur, 3), "start": round(start, 3), "end": round(end, 3)}
(out / "durations.json").write_text(json.dumps(d, indent=2))
print(json.dumps(d))
PY
