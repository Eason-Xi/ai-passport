// 文字层：逐字跟随旁白弹出的标题 / 字幕、计数器、印章。全部居中在 9:16 安全区内。
import React from 'react';
import {interpolate} from 'remotion';
import {C, serif, sans, W} from '../theme';
import type {Span} from '../timeline';

type Common = {
  frame: number;
  text: string;
  y: number; // 文字行中心的画面 y
  size: number;
  color: string;
  accent?: string; // 强调色
  accentChars?: string; // 用强调色的字符
  weight?: number;
  font?: 'serif' | 'sans';
  letterSpacing?: number;
  show: Span; // 可见区间 [from, to)
  pill?: string; // 字幕底板颜色
};

// 逐字弹出：字符 i 在 reveal.from + i/n·(reveal.to − reveal.from) 处弹出（放大 1.35 → 1，4 帧）。
// 不给 reveal 时整行在 show.from 一次出现。
export const Karaoke: React.FC<Common & {reveal?: Span}> = ({
  frame,
  text,
  y,
  size,
  color,
  accent,
  accentChars = '',
  weight = 900,
  font = 'serif',
  letterSpacing = 0.04,
  show,
  reveal,
  pill,
}) => {
  if (frame < show.from || frame >= show.to) return null;
  const chars = Array.from(text);
  const visible = chars.filter((c) => c !== ' ').length;
  const out = interpolate(frame, [show.to - 5, show.to], [1, 0], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});
  let k = -1;
  return (
    <div
      style={{
        position: 'absolute',
        left: 120,
        width: W - 240,
        top: y - size * 0.75,
        height: size * 1.5,
        display: 'flex',
        justifyContent: 'center',
        alignItems: 'center',
        opacity: out,
      }}
    >
      <div
        style={{
          display: 'flex',
          alignItems: 'center',
          padding: pill ? `${size * 0.18}px ${size * 0.5}px` : 0,
          borderRadius: size * 0.3,
          background: pill,
          fontFamily: font === 'serif' ? serif : sans,
          fontWeight: weight,
          fontSize: size,
          letterSpacing: `${letterSpacing}em`,
          whiteSpace: 'pre',
          lineHeight: 1,
        }}
      >
        {chars.map((c, i) => {
          if (c !== ' ') k += 1;
          // 不给 reveal：整行在 show.from 当帧就完整可见（第 0 帧的钩子文案必须立刻上屏），只做轻微回弹。
          const at = reveal ? reveal.from + ((reveal.to - reveal.from) * k) / Math.max(1, visible) : show.from;
          const t = reveal ? interpolate(frame, [at, at + 4], [0, 1], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'}) : 1;
          const s = reveal
            ? interpolate(t, [0, 1], [1.35, 1])
            : interpolate(frame, [at, at + 4], [1.12, 1], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});
          return (
            <span
              key={i}
              style={{
                display: 'inline-block',
                color: accentChars.includes(c) && accent ? accent : color,
                opacity: t,
                transform: `scale(${s})`,
              }}
            >
              {c}
            </span>
          );
        })}
      </div>
    </div>
  );
};

// 计数器：等宽数字，滚动到目标值。
export const Counter: React.FC<{
  value: number;
  y: number;
  size: number;
  color: string;
  suffix?: string;
  suffixColor?: string;
  opacity?: number;
  scale?: number;
}> = ({value, y, size, color, suffix, suffixColor = C.cream, opacity = 1, scale = 1}) => (
  <div
    style={{
      position: 'absolute',
      left: 120,
      width: W - 240,
      top: y - size * 0.6,
      height: size * 1.2,
      display: 'flex',
      justifyContent: 'center',
      alignItems: 'baseline',
      gap: size * 0.12,
      opacity,
      transform: `scale(${scale})`,
    }}
  >
    <span style={{fontFamily: serif, fontWeight: 900, fontSize: size, color, fontVariantNumeric: 'tabular-nums', lineHeight: 1}}>
      {Math.round(value)}
    </span>
    {suffix ? (
      <span style={{fontFamily: serif, fontWeight: 900, fontSize: size * 0.36, color: suffixColor, lineHeight: 1}}>{suffix}</span>
    ) : null}
  </div>
);

// 朱红印章「鲁公」（与设备主页的印章同字）：盖下时从 1.7 倍压到 1 倍并略旋转。
export const Seal: React.FC<{frame: number; at: number; x: number; y: number; size: number; text?: string}> = ({
  frame,
  at,
  x,
  y,
  size,
  text = '鲁公',
}) => {
  if (frame < at) return null;
  const t = interpolate(frame, [at, at + 5], [0, 1], {extrapolateRight: 'clamp'});
  const s = interpolate(t, [0, 1], [1.7, 1]);
  return (
    <div
      style={{
        position: 'absolute',
        left: x - size / 2,
        top: y - size * 0.6,
        width: size,
        height: size * 1.2,
        background: C.red,
        borderRadius: size * 0.1,
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'center',
        transform: `scale(${s}) rotate(-5deg)`,
        opacity: t,
        boxShadow: '0 0 0 3px rgba(189,48,41,0.35)',
      }}
    >
      {Array.from(text).map((c) => (
        <span key={c} style={{fontFamily: serif, fontWeight: 900, fontSize: size * 0.42, color: C.cream, lineHeight: 1.08}}>
          {c}
        </span>
      ))}
    </div>
  );
};

// 红底白字小标签（与设备笔法卡的标签同样式）。
export const Tag: React.FC<{text: string; size: number; opacity?: number; scale?: number}> = ({text, size, opacity = 1, scale = 1}) => (
  <span
    style={{
      display: 'inline-block',
      background: C.red,
      color: C.cream,
      fontFamily: serif,
      fontWeight: 900,
      fontSize: size,
      lineHeight: 1,
      padding: `${size * 0.16}px ${size * 0.28}px`,
      borderRadius: size * 0.14,
      opacity,
      transform: `scale(${scale})`,
    }}
  >
    {text}
  </span>
);
