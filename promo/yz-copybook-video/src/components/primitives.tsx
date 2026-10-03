// 基础构件：着色字形、背景、颗粒、震屏、粒子、安全区。所有动画只由帧号驱动。
import React from 'react';
import {AbsoluteFill, Img, interpolate, random, staticFile} from 'remotion';
import {C, H, W} from '../theme';

export const clamp01 = (v: number) => Math.min(1, Math.max(0, v));
export const lerp = (a: number, b: number, t: number) => a + (b - a) * t;
export const easeOutCubic = (t: number) => 1 - (1 - clamp01(t)) ** 3;
export const easeInOutCubic = (t: number) => {
  const x = clamp01(t);
  return x < 0.5 ? 4 * x * x * x : 1 - (-2 * x + 2) ** 3 / 2;
};
// 0 → 1，在 at 帧瞬间到 1，再按 dur 帧衰减回 0（用于推镜、闪光）。
export const pulse = (frame: number, at: number, dur: number) => {
  const t = frame - at;
  if (t < 0 || t > dur) return 0;
  return (1 - t / dur) ** 2;
};

const TINTS: Record<string, string> = {
  cream: C.cream,
  ink: C.ink,
  red: C.red,
  trace: C.trace,
  vermilion: C.vermilion,
};
const hexRgb = (hex: string) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255);

// 白字透明底的字形 PNG 通过 SVG 颜色矩阵着色（保留 alpha）。
export const TintDefs: React.FC = () => (
  <svg width={0} height={0} style={{position: 'absolute'}}>
    <defs>
      {Object.entries(TINTS).map(([k, hex]) => {
        const [r, g, b] = hexRgb(hex);
        return (
          <filter key={k} id={`tint-${k}`} colorInterpolationFilters="sRGB">
            <feColorMatrix type="matrix" values={`0 0 0 0 ${r} 0 0 0 0 ${g} 0 0 0 0 ${b} 0 0 0 1 0`} />
          </filter>
        );
      })}
    </defs>
  </svg>
);

export type Tint = keyof typeof TINTS;

export const Glyph: React.FC<{
  file: string;
  size: number;
  x: number;
  y: number;
  tint?: Tint;
  opacity?: number;
  rotate?: number;
}> = ({file, size, x, y, tint = 'cream', opacity = 1, rotate = 0}) => (
  <Img
    src={staticFile(file)}
    style={{
      position: 'absolute',
      left: x - size / 2,
      top: y - size / 2,
      width: size,
      height: size,
      filter: `url(#tint-${tint})`,
      opacity,
      transform: rotate ? `rotate(${rotate}deg)` : undefined,
    }}
  />
);

export const Fill: React.FC<{color: string; opacity?: number}> = ({color, opacity = 1}) => (
  <AbsoluteFill style={{backgroundColor: color, opacity}} />
);

// 全片统一的静态颗粒（固定种子），石面 / 纸面质感；首尾帧一致，循环无接缝。
export const Grain: React.FC<{opacity?: number; width?: number; height?: number}> = ({opacity = 0.14, width = W, height = H}) => (
  <AbsoluteFill style={{mixBlendMode: 'overlay', opacity, pointerEvents: 'none'}}>
    <svg width={width} height={height}>
      <filter id="grain-fine">
        <feTurbulence type="fractalNoise" baseFrequency="0.85" numOctaves={2} seed={7} stitchTiles="stitch" />
        <feColorMatrix type="saturate" values="0" />
      </filter>
      <filter id="grain-coarse">
        <feTurbulence type="fractalNoise" baseFrequency="0.008" numOctaves={3} seed={11} />
        <feColorMatrix type="saturate" values="0" />
      </filter>
      <rect width={width} height={height} filter="url(#grain-fine)" />
      <rect width={width} height={height} filter="url(#grain-coarse)" opacity={0.6} />
    </svg>
  </AbsoluteFill>
);

export const Vignette: React.FC<{strength?: number}> = ({strength = 0.55}) => (
  <AbsoluteFill
    style={{
      background: `radial-gradient(ellipse 75% 60% at 50% 50%, rgba(0,0,0,0) 55%, rgba(0,0,0,${strength}) 100%)`,
      pointerEvents: 'none',
    }}
  />
);

// 震屏：多个冲击叠加，每个冲击按 dur 帧二次衰减。确定性正弦，不用随机数。
export const shake = (frame: number, hits: Array<[number, number, number?]>) => {
  let x = 0;
  let y = 0;
  for (const [at, amp, dur = 12] of hits) {
    const t = frame - at;
    if (t <= 0 || t > dur) continue;
    const d = (1 - t / dur) ** 2;
    x += Math.sin(t * 2.9 + at) * amp * d;
    y += Math.cos(t * 3.7 + at * 0.7) * amp * d;
  }
  return {x, y};
};

// 冲击粒子：石屑 / 墨点，从 (x, y) 向外迸散，带重力与淡出。
export const Burst: React.FC<{
  frame: number;
  at: number;
  x: number;
  y: number;
  count: number;
  color: string;
  spread: number;
  seed: string;
  life?: number;
  ring?: number; // 起点半径（从外框边缘迸出）
}> = ({frame, at, x, y, count, color, spread, seed, life = 26, ring = 0}) => {
  const t = (frame - at) / life;
  if (t < 0 || t > 1) return null;
  return (
    <>
      {Array.from({length: count}).map((_, i) => {
        const a = random(`${seed}-a-${i}`) * Math.PI * 2;
        const v = (0.35 + 0.65 * random(`${seed}-v-${i}`)) * spread;
        const size = 3 + random(`${seed}-s-${i}`) * 11;
        const d = easeOutCubic(t) * v;
        const px = x + Math.cos(a) * (ring + d);
        const py = y + Math.sin(a) * (ring + d) + 260 * t * t;
        return (
          <div
            key={i}
            style={{
              position: 'absolute',
              left: px - size / 2,
              top: py - size / 2,
              width: size,
              height: size * (0.6 + 0.4 * random(`${seed}-r-${i}`)),
              borderRadius: '50%',
              background: color,
              opacity: (1 - t) * (0.5 + 0.5 * random(`${seed}-o-${i}`)),
            }}
          />
        );
      })}
    </>
  );
};

// 冲击波：一圈细线从中心扩散。
export const Shockwave: React.FC<{frame: number; at: number; x: number; y: number; color: string; max?: number}> = ({
  frame,
  at,
  x,
  y,
  color,
  max = 900,
}) => {
  const t = (frame - at) / 18;
  if (t < 0 || t > 1) return null;
  const r = interpolate(t, [0, 1], [120, max], {easing: (v) => 1 - (1 - v) ** 3});
  return (
    <div
      style={{
        position: 'absolute',
        left: x - r,
        top: y - r,
        width: r * 2,
        height: r * 2,
        borderRadius: '50%',
        border: `${6 * (1 - t)}px solid ${color}`,
        opacity: 0.7 * (1 - t),
      }}
    />
  );
};

export const SafeArea: React.FC = () => (
  <AbsoluteFill style={{pointerEvents: 'none'}}>
    <div style={{position: 'absolute', left: 90, top: 260, width: 900, height: 1400, outline: '3px dashed #0f0'}} />
    <div style={{position: 'absolute', left: 0, top: 0, width: W, height: 120, background: 'rgba(255,0,0,0.25)'}} />
    <div style={{position: 'absolute', left: 0, top: H - 320, width: W, height: 320, background: 'rgba(255,0,0,0.25)'}} />
    <div style={{position: 'absolute', left: W - 120, top: 0, width: 120, height: H, background: 'rgba(255,0,0,0.18)'}} />
  </AbsoluteFill>
);
