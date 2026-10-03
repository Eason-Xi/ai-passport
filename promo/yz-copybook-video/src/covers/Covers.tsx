// 视频封面：两套风格 × 四种比例（16:9、4:3、3:4、9:16）。
//   A「碑林」：拓本底 + 1412 字碑墙 + 原碑集字「顏真卿」+ 设备主页
//   B「描红」：纸色底 + 米字格描红「永」+ 设备描红界面
// 各比例的版式写成显式坐标表，便于逐张微调；文字都避开各平台的角标与裁切区（见 README）。
import React from 'react';
import {AbsoluteFill, Img, staticFile} from 'remotion';
import {Device} from '../components/Device';
import {Glyph, Grain, TintDefs, Vignette, type Tint} from '../components/primitives';
import {Seal, Tag} from '../components/Text';
import {C, serif} from '../theme';
import {GLYPHS} from '../timeline';

export type Ratio = '16x9' | '4x3' | '3x4' | '9x16';
export const RATIOS: Record<Ratio, {w: number; h: number}> = {
  '16x9': {w: 1920, h: 1080},
  '4x3': {w: 1440, h: 1080},
  '3x4': {w: 1080, h: 1440},
  '9x16': {w: 1080, h: 1920},
};

type Align = 'left' | 'center' | 'right';
type At = {x: number; y: number; size: number; align?: Align};

// 单行文字：(x, y) 为行的垂直中心；align 决定 x 是左端、中点还是右端。
const Line: React.FC<{
  at: At;
  color: string;
  weight?: number;
  font?: string;
  spacing?: number;
  shadow?: string;
  children: React.ReactNode;
}> = ({at, color, weight = 900, font = serif, spacing = 0.06, shadow, children}) => {
  const align = at.align ?? 'left';
  const left = align === 'left' ? at.x : align === 'center' ? at.x - 2000 : at.x - 4000;
  return (
    <div
      style={{
        position: 'absolute',
        left,
        top: at.y - at.size * 0.6,
        width: 4000,
        height: at.size * 1.2,
        display: 'flex',
        alignItems: 'center',
        justifyContent: align === 'left' ? 'flex-start' : align === 'center' ? 'center' : 'flex-end',
        fontFamily: font,
        fontWeight: weight,
        fontSize: at.size,
        letterSpacing: `${spacing}em`,
        color,
        lineHeight: 1,
        whiteSpace: 'pre',
        textShadow: shadow,
      }}
    >
      {children}
    </div>
  );
};

// 「1412 个碑帖原字」：数字大、量词小，基线对齐。
const Count: React.FC<{at: At; num: string; rest: string; numColor: string; color: string}> = ({at, num, rest, numColor, color}) => (
  <Line at={at} color={color}>
    <span style={{color: numColor, fontVariantNumeric: 'tabular-nums'}}>{num}</span>
    <span style={{fontSize: at.size * 0.5, marginLeft: at.size * 0.12, alignSelf: 'flex-end', marginBottom: at.size * 0.12}}>
      {rest}
    </span>
  </Line>
);

const Tags: React.FC<{at: At}> = ({at}) => (
  <Line at={at} color={C.cream}>
    <span style={{display: 'flex', gap: at.size * 0.35}}>
      {['拓本', '墨迹', '描红'].map((t) => (
        <Tag key={t} text={t} size={at.size} />
      ))}
    </span>
  </Line>
);

const Brand: React.FC<{at: At; dark: boolean}> = ({at, dark}) => {
  const h = at.size * 0.82;
  return (
    <Line at={at} color={dark ? 'rgba(239,231,214,0.85)' : C.ink} weight={600} spacing={0.08}>
      <Img
        src={staticFile(`gen/brand/${dark ? 'logo-wordmark-dark' : 'logo-wordmark'}.png`)}
        style={{height: h, width: (h * 1648) / 336, marginRight: at.size * 0.5}}
      />
      AI Passport
    </Line>
  );
};

// 原碑集字「顏」「真」「卿」竖排。
const GlyphColumn: React.FC<{x: number; y0: number; size: number; gap: number; tint?: Tint}> = ({x, y0, size, gap, tint = 'cream'}) => (
  <>
    {['yan', 'zhen', 'qing'].map((key, i) => {
      const g = GLYPHS.heroes.find((h) => h.key === key)!;
      return <Glyph key={key} file={g.file} size={size} x={x} y={y0 + i * gap} tint={tint} />;
    })}
  </>
);

// 碑墙铺满画面（cover），作暗纹背景。
const WallBg: React.FC<{w: number; h: number; opacity: number}> = ({w, h, opacity}) => {
  const wall = GLYPHS.wall;
  const ww = wall.cols * wall.tile;
  const wh = wall.rows * wall.tile;
  const s = Math.max(w / ww, h / wh) * 1.02;
  return (
    <Img
      src={staticFile(wall.file)}
      style={{position: 'absolute', left: (w - ww * s) / 2, top: (h - wh * s) / 2, width: ww * s, height: wh * s, opacity}}
    />
  );
};

// 米字格描红：红色外框、虚线中线与对角线，格内是描红色的拓本「永」。
const PracticeCell: React.FC<{x: number; y: number; size: number}> = ({x, y, size}) => {
  const yong = GLYPHS.heroes.find((h) => h.key === 'yong')!;
  const dash = `${size * 0.018} ${size * 0.014}`;
  const sw = Math.max(2, size * 0.004);
  return (
    <>
      <svg style={{position: 'absolute', left: x, top: y}} width={size} height={size}>
        <rect x={0} y={0} width={size} height={size} fill="rgba(255,255,255,0.18)" />
        <g stroke={C.trace} strokeWidth={sw} strokeDasharray={dash} opacity={0.9}>
          <line x1={size / 2} y1={0} x2={size / 2} y2={size} />
          <line x1={0} y1={size / 2} x2={size} y2={size / 2} />
          <line x1={0} y1={0} x2={size} y2={size} />
          <line x1={size} y1={0} x2={0} y2={size} />
        </g>
        <rect x={size * 0.006} y={size * 0.006} width={size * 0.988} height={size * 0.988} fill="none" stroke={C.red} strokeWidth={size * 0.012} />
      </svg>
      <Glyph file={yong.file} size={size * 0.98} x={x + size / 2} y={y + size / 2} tint="trace" />
    </>
  );
};

// ---------------------------------------------------------------- A「碑林」
type LayoutA = {
  col: {x: number; y0: number; size: number; gap: number};
  seal: {x: number; y: number; size: number};
  title: At;
  count: At;
  sub: At;
  tags: At;
  brand: At;
  device: {x: number; y: number; k: number};
};
const LA: Record<Ratio, LayoutA> = {
  '16x9': {
    col: {x: 255, y0: 205, size: 310, gap: 330},
    seal: {x: 428, y: 905, size: 76},
    title: {x: 560, y: 285, size: 132},
    count: {x: 560, y: 470, size: 112},
    sub: {x: 560, y: 600, size: 58},
    tags: {x: 560, y: 725, size: 44},
    brand: {x: 560, y: 870, size: 40},
    device: {x: 1625, y: 548, k: 0.68},
  },
  '4x3': {
    col: {x: 175, y0: 215, size: 270, gap: 300},
    seal: {x: 315, y: 960, size: 66},
    title: {x: 350, y: 255, size: 100},
    count: {x: 350, y: 420, size: 92},
    sub: {x: 350, y: 535, size: 48},
    tags: {x: 350, y: 645, size: 38},
    brand: {x: 350, y: 790, size: 34},
    device: {x: 1185, y: 560, k: 0.56},
  },
  '3x4': {
    col: {x: 230, y0: 650, size: 260, gap: 280},
    seal: {x: 385, y: 1262, size: 70},
    title: {x: 540, y: 190, size: 148, align: 'center'},
    count: {x: 540, y: 345, size: 92, align: 'center'},
    sub: {x: 540, y: 450, size: 52, align: 'center'},
    tags: {x: 540, y: 1385, size: 40, align: 'center'},
    brand: {x: 540, y: 62, size: 32, align: 'center'},
    device: {x: 735, y: 905, k: 0.6},
  },
  '9x16': {
    col: {x: 235, y0: 935, size: 290, gap: 300},
    seal: {x: 395, y: 1640, size: 76},
    title: {x: 540, y: 440, size: 150, align: 'center'},
    count: {x: 540, y: 605, size: 96, align: 'center'},
    sub: {x: 540, y: 715, size: 54, align: 'center'},
    tags: {x: 540, y: 1790, size: 44, align: 'center'},
    brand: {x: 540, y: 290, size: 36, align: 'center'},
    device: {x: 735, y: 1230, k: 0.66},
  },
};

export const CoverA: React.FC<{ratio: Ratio}> = ({ratio}) => {
  const {w, h} = RATIOS[ratio];
  const L = LA[ratio];
  const glow = '0 0 40px rgba(24,24,24,0.9)';
  return (
    <AbsoluteFill style={{backgroundColor: C.stone, overflow: 'hidden'}}>
      <TintDefs />
      <WallBg w={w} h={h} opacity={0.2} />
      {/* 文字区压暗，保证缩略图里也读得清 */}
      <AbsoluteFill
        style={{
          background:
            w > h
              ? 'linear-gradient(90deg, rgba(24,24,24,0.55) 0%, rgba(24,24,24,0.92) 28%, rgba(24,24,24,0.92) 62%, rgba(24,24,24,0.35) 100%)'
              : 'linear-gradient(180deg, rgba(24,24,24,0.92) 0%, rgba(24,24,24,0.88) 38%, rgba(24,24,24,0.45) 70%, rgba(24,24,24,0.8) 100%)',
        }}
      />
      <GlyphColumn {...L.col} />
      <Seal frame={100} at={0} x={L.seal.x} y={L.seal.y} size={L.seal.size} />
      <Device x={L.device.x} y={L.device.y} k={L.device.k} screen="01_home_new" glow={0.22} />
      <Line at={L.title} color={C.cream} spacing={0.08} shadow={glow}>
        颜真卿字帖
      </Line>
      <Count at={L.count} num="1412" rest="个碑帖原字" numColor={C.vermilion} color={C.cream} />
      <Line at={L.sub} color={C.cream} weight={600} spacing={0.1} shadow={glow}>
        装进口袋 · 随身临帖
      </Line>
      <Tags at={L.tags} />
      <Brand at={L.brand} dark />
      <Vignette strength={0.4} />
      <Grain opacity={0.12} width={w} height={h} />
    </AbsoluteFill>
  );
};

// ---------------------------------------------------------------- B「描红」
type LayoutB = {
  cell: {x: number; y: number; size: number};
  device: {x: number; y: number; k: number; rot: number};
  title: At;
  sub: At;
  tags: At;
  count: At;
  brand: At;
};
const LB: Record<Ratio, LayoutB> = {
  '16x9': {
    cell: {x: 80, y: 90, size: 900},
    device: {x: 1712, y: 770, k: 0.4, rot: 6},
    title: {x: 1040, y: 230, size: 150},
    sub: {x: 1040, y: 385, size: 60},
    tags: {x: 1040, y: 500, size: 46},
    count: {x: 1040, y: 650, size: 70},
    brand: {x: 1040, y: 860, size: 38},
  },
  '4x3': {
    cell: {x: 60, y: 215, size: 700},
    device: {x: 1130, y: 712, k: 0.44, rot: 6},
    title: {x: 720, y: 115, size: 128, align: 'center'},
    sub: {x: 1100, y: 275, size: 44, align: 'center'},
    tags: {x: 410, y: 1000, size: 38, align: 'center'},
    count: {x: 1100, y: 352, size: 56, align: 'center'},
    brand: {x: 1100, y: 212, size: 26, align: 'center'},
  },
  '3x4': {
    cell: {x: 60, y: 385, size: 660},
    device: {x: 880, y: 1080, k: 0.4, rot: 6},
    title: {x: 540, y: 160, size: 150, align: 'center'},
    sub: {x: 540, y: 300, size: 56, align: 'center'},
    tags: {x: 64, y: 1150, size: 40},
    count: {x: 64, y: 1265, size: 64},
    brand: {x: 64, y: 1375, size: 30},
  },
  '9x16': {
    cell: {x: 150, y: 640, size: 780},
    device: {x: 900, y: 1560, k: 0.36, rot: 6},
    title: {x: 540, y: 400, size: 150, align: 'center'},
    sub: {x: 540, y: 545, size: 58, align: 'center'},
    tags: {x: 150, y: 1500, size: 42},
    count: {x: 150, y: 1610, size: 64},
    brand: {x: 540, y: 270, size: 34, align: 'center'},
  },
};

export const CoverB: React.FC<{ratio: Ratio}> = ({ratio}) => {
  const {w, h} = RATIOS[ratio];
  const L = LB[ratio];
  return (
    <AbsoluteFill style={{backgroundColor: C.cream, overflow: 'hidden'}}>
      <TintDefs />
      <PracticeCell {...L.cell} />
      <Device x={L.device.x} y={L.device.y} k={L.device.k} rotate={L.device.rot} screen="10_practice_trace_jiu_timer" glow={0} />
      <Line at={L.title} color={C.ink} spacing={0.08}>
        颜真卿字帖
      </Line>
      <Line at={L.sub} color={C.ink} weight={600} spacing={0.1}>
        随身临帖 · 一字一遍
      </Line>
      <Tags at={L.tags} />
      <Count at={L.count} num="1412" rest="个碑帖原字" numColor={C.red} color={C.ink} />
      <Brand at={L.brand} dark={false} />
      <Vignette strength={0.12} />
      <Grain opacity={0.16} width={w} height={h} />
    </AbsoluteFill>
  );
};

