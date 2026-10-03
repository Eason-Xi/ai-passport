// 前 10 秒「千字瀑布」：砸字 → 闪切计数 → 碑墙拉远 → 缩进设备屏幕 → 拓本/墨迹/描红三连切 → 原碑集字「顏真卿」。
// 全部用绝对帧计算，跨阶段的镜头（碑墙 → 设备）才能逐帧连续。
import React from 'react';
import {AbsoluteFill, Img, interpolate, random, spring, staticFile} from 'remotion';
import {Burst, Fill, Glyph, Shockwave, clamp01, lerp, pulse, shake} from '../components/primitives';
import {Device, ScreenFade, SCREEN_CENTER, deviceCenterFor, ui} from '../components/Device';
import {Counter, Karaoke, Seal} from '../components/Text';
import {C, DEVICE_K, DEVICE_Y, serif} from '../theme';
import {DEVICE, FPS, GLYPHS, TL, speech} from '../timeline';

const h = TL.hook;
// 开场「永」与闪切字的显示尺寸：字形画布 1000 px，中心 (540, 1000)。
export const HERO = {size: 1000, x: 540, y: 1000, slamFrom: 1.18};
// 闪切字略小、略靠上，给计数器留出位置；碑墙拉远从这里开始。
const FLASH = {size: 900, x: 540, y: 950};

const wall = GLYPHS.wall;
const WALL_W = wall.cols * wall.tile;
const WALL_H = wall.rows * wall.tile;
const FOCUS = {x: (wall.focus.col + 0.5) * wall.tile, y: (wall.focus.row + 0.5) * wall.tile};
const WALL_CENTER = {x: WALL_W / 2, y: WALL_H / 2};
const S0 = FLASH.size / (wall.tile - 8); // 碑墙一格的字形区放大到与闪切字同大
const S1 = 0.34; // 整面碑墙铺满画面
const S_LAND = S1 * 0.97;
const K_LAND0 = (WALL_W * S_LAND) / DEVICE.screen.w; // 落屏开始时设备的缩放：屏幕宽 = 碑墙宽
// 落定后屏幕中心的画面坐标（设备中心在 (540, DEVICE_Y)）。
const SCREEN_END = {
  x: 540 + DEVICE_K * (SCREEN_CENTER.x - DEVICE.w / 2),
  y: DEVICE_Y + DEVICE_K * (SCREEN_CENTER.y - DEVICE.h / 2),
};

const flashIndex = (f: number) => {
  let k = 0;
  h.flashCuts.forEach((c, i) => {
    if (f >= c) k = i;
  });
  return k;
};

// 碑墙图集按「画面上 C 点对准图集上 P 点、缩放 s」绘制，只画可见部分。
const WallImage: React.FC<{s: number; P: {x: number; y: number}; C: {x: number; y: number}}> = ({s, P, C: c}) => (
  <Img
    src={staticFile(wall.file)}
    style={{position: 'absolute', left: c.x - s * P.x, top: c.y - s * P.y, width: WALL_W * s, height: WALL_H * s}}
  />
);

export const Hook: React.FC<{frame: number}> = ({frame: f}) => {
  if (f >= h.end) return null;

  // ---------------- 背景色：三连切时随屏幕底色翻转
  let bg = C.stone;
  if (f >= h.triple[1] && f < h.title[0]) bg = C.cream;

  const hits: Array<[number, number, number?]> = [
    [h.slam, 26, 14],
    [h.land, 22, 12],
    ...h.triple.map((t) => [t, 10, 8] as [number, number, number]),
    ...h.title.map((t) => [t, 18, 10] as [number, number, number]),
  ];
  const sh = shake(f, hits);

  return (
    <AbsoluteFill style={{backgroundColor: bg, overflow: 'hidden'}}>
      {f >= h.triple[2] && f < h.title[0] ? <TraceGrid /> : null}
      <AbsoluteFill style={{transform: `translate(${sh.x}px, ${sh.y}px)`}}>
        {f < h.flashStart ? <Slam f={f} /> : null}
        {f >= h.flashStart && f < h.flashEnd ? <Flash f={f} /> : null}
        {f >= h.wallStart && f < h.landStart - 6 ? <WallZoom f={f} /> : null}
        {f >= h.landStart - 6 && f < h.title[0] ? <Landing f={f} /> : null}
        {f >= h.title[0] ? <Title f={f} /> : null}
      </AbsoluteFill>
      <HookText f={f} />
    </AbsoluteFill>
  );
};

// ---------------- 0–1 s：拓本「永」砸入
const Slam: React.FC<{f: number}> = ({f}) => {
  const spr = spring({frame: f, fps: FPS, config: {damping: 13, stiffness: 320, mass: 0.6}});
  const s = lerp(HERO.slamFrom, 1, spr) * (1 + 0.04 * clamp01((f - 8) / 22));
  const yong = GLYPHS.heroes.find((x) => x.key === 'yong')!;
  const glow = 0.28 * (1 - clamp01(f / 18));
  return (
    <>
      <div
        style={{
          position: 'absolute',
          left: HERO.x - 700,
          top: HERO.y - 700,
          width: 1400,
          height: 1400,
          background: `radial-gradient(circle, rgba(239,231,214,${glow}) 0%, rgba(239,231,214,0) 60%)`,
        }}
      />
      <Glyph file={yong.file} size={HERO.size * s} x={HERO.x} y={HERO.y} />
      <Burst frame={f} at={1} x={HERO.x} y={HERO.y} count={70} color={C.cream} spread={620} seed="slam" ring={180} life={28} />
    </>
  );
};

// ---------------- 1–3.5 s：原碑字闪切，越切越快
// 前半段每隔几刀插一次「描红纸」反色闪，作视觉打断；最后几刀保持拓本，与碑墙衔接。
const flashInverted = (f: number) => {
  if (f < h.flashStart || f >= h.flashEnd) return false;
  const k = flashIndex(f);
  return k < 20 && k % 5 === 3;
};

const Flash: React.FC<{f: number}> = ({f}) => {
  const k = flashIndex(f);
  const last = k === h.flashCuts.length - 1;
  const g = GLYPHS.flash[last ? GLYPHS.flash.length - 1 : k];
  const invert = flashInverted(f);
  const jitter = last ? 0 : (random(`fj-${k}`) - 0.5) * 0.12;
  const s = 1 + jitter + 0.05 * pulse(f, h.flashCuts[k], 3);
  return (
    <>
      {invert ? <Fill color={C.cream} /> : null}
      <Glyph file={g.file} size={FLASH.size * s} x={FLASH.x} y={FLASH.y} tint={invert ? 'red' : 'cream'} />
    </>
  );
};

// ---------------- 3.5–5.8 s：最后一个字变成碑墙的一格，急速拉远
const wallCamera = (f: number) => {
  if (f < h.wallFull) {
    const u = (f - h.wallStart) / (h.wallFull - h.wallStart);
    const e = 1 - (1 - clamp01(u)) ** 2.5;
    const s = Math.exp(lerp(Math.log(S0), Math.log(S1), e));
    return {
      s,
      e,
      P: {x: lerp(FOCUS.x, WALL_CENTER.x, e), y: lerp(FOCUS.y, WALL_CENTER.y, e)},
      C: {x: 540, y: lerp(FLASH.y, 960, e)},
    };
  }
  const u = clamp01((f - h.wallFull) / (h.landStart - h.wallFull));
  return {s: S1 * (1 - 0.03 * u), e: 1, P: WALL_CENTER, C: {x: 540, y: 960}};
};

const WallZoom: React.FC<{f: number}> = ({f}) => {
  const cam = wallCamera(f);
  const focusGlyph = GLYPHS.flash[GLYPHS.flash.length - 1];
  const heroOpacity = clamp01((cam.s - 3) / 3);
  const tile = {x: cam.C.x + cam.s * (FOCUS.x - cam.P.x), y: cam.C.y + cam.s * (FOCUS.y - cam.P.y)};
  const glyphBox = (wall.tile - 8) * cam.s;
  // 一道斜向扫光掠过整面碑墙
  const sweep = interpolate(f, [h.wallFull - 6, h.landStart], [-0.3, 1.3], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});
  return (
    <>
      <WallImage s={cam.s} P={cam.P} C={cam.C} />
      {heroOpacity > 0 ? (
        <>
          <div
            style={{
              position: 'absolute',
              left: tile.x - glyphBox / 2,
              top: tile.y - glyphBox / 2,
              width: glyphBox,
              height: glyphBox,
              background: C.stone,
              opacity: heroOpacity,
            }}
          />
          <Glyph file={focusGlyph.file} size={glyphBox} x={tile.x} y={tile.y} opacity={heroOpacity} />
        </>
      ) : null}
      {f >= h.wallFull - 6 ? (
        <AbsoluteFill
          style={{
            background: `linear-gradient(115deg, rgba(239,231,214,0) ${sweep * 100 - 12}%, rgba(239,231,214,0.22) ${sweep * 100}%, rgba(239,231,214,0) ${sweep * 100 + 12}%)`,
            mixBlendMode: 'screen',
          }}
        />
      ) : null}
    </>
  );
};

// ---------------- 5.8–7 s：设备出现在碑墙四周，带着碑墙一起缩小、落定；7–8.5 s 三连切
const SCREENS = ['07_practice_stone_mi', '09_practice_paper_tian_fav', '10_practice_trace_jiu_timer'];

const Landing: React.FC<{f: number}> = ({f}) => {
  const u = clamp01((f - h.landStart) / (h.land - h.landStart));
  const e = u ** 2.2;
  const k0 = K_LAND0;
  let k = Math.exp(lerp(Math.log(k0), Math.log(DEVICE_K), e));
  const screenAt = {x: lerp(540, SCREEN_END.x, e), y: lerp(960, SCREEN_END.y, e)};
  // 三连切每拍推镜
  const beat = h.triple.reduce((acc, t) => acc + pulse(f, t, 9), 0);
  k *= 1 + 0.06 * beat + 0.035 * pulse(f, h.land, 8);
  const center = f >= h.land ? {x: 540, y: DEVICE_Y} : deviceCenterFor(SCREEN_CENTER, screenAt.x, screenAt.y, k);
  const opacity = interpolate(f, [h.landStart - 6, h.landStart], [0, 1], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});

  // 屏幕内容：碑墙（按屏宽铺满）→ 千字文目录真实界面 → 三连切
  const wallInScreen = (
    <Img
      src={staticFile(wall.file)}
      style={{
        position: 'absolute',
        left: 0,
        top: (DEVICE.screen.h - (DEVICE.screen.w * WALL_H) / WALL_W) / 2,
        width: DEVICE.screen.w,
        height: (DEVICE.screen.w * WALL_H) / WALL_W,
      }}
    />
  );
  let content: React.ReactNode;
  if (f < h.triple[0]) {
    const t = interpolate(f, [h.land - 12, h.land], [0, 1], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});
    content = <ScreenFade from={wallInScreen} to="40_catalog_qianzi_full" t={t} />;
  } else {
    const i = h.triple.filter((t) => f >= t).length - 1;
    content = <Img src={ui(SCREENS[i])} style={{width: '100%', height: '100%'}} />;
  }

  return (
    <>
      {f < h.landStart ? (
        // 落屏开始前：设备淡入，碑墙仍在原位（盖住屏幕区）
        <>
          <Device x={center.x} y={center.y} k={k} opacity={opacity} glow={0} />
          <WallZoom f={f} />
        </>
      ) : (
        <Device x={center.x} y={center.y} k={k} glow={f >= h.land ? 0.16 : 0}>
          {content}
        </Device>
      )}
      <Shockwave frame={f} at={h.land} x={540} y={DEVICE_Y} color={C.cream} max={1000} />
      <Burst frame={f} at={h.land} x={540} y={DEVICE_Y} count={60} color={C.cream} spread={520} seed="land" ring={420} life={24} />
    </>
  );
};

// 描红纸底：整屏淡红九宫格
const TraceGrid: React.FC = () => (
  <AbsoluteFill>
    {[1, 2].map((i) => (
      <React.Fragment key={i}>
        <div style={{position: 'absolute', left: (1080 * i) / 3, top: 0, width: 3, height: 1920, background: C.trace, opacity: 0.45}} />
        <div style={{position: 'absolute', left: 0, top: (1920 * i) / 3, width: 1080, height: 3, background: C.trace, opacity: 0.45}} />
      </React.Fragment>
    ))}
    <div style={{position: 'absolute', inset: 40, border: `6px solid ${C.red}`, opacity: 0.5}} />
  </AbsoluteFill>
);

// ---------------- 8.5–10 s：原碑集字「顏」「真」「卿」逐个砸出，盖「鲁公」印
const TITLE = ['yan', 'zhen', 'qing'];
const Title: React.FC<{f: number}> = ({f}) => {
  const size = 400;
  const ys = [560, 930, 1300];
  return (
    <>
      <div style={{position: 'absolute', inset: 0, opacity: 0.08}}>
        <WallImage s={0.5} P={{x: WALL_CENTER.x + (f - h.title[0]) * 2, y: WALL_CENTER.y}} C={{x: 540, y: 960}} />
      </div>
      {TITLE.map((key, i) => {
        const at = h.title[i];
        if (f < at) return null;
        const spr = spring({frame: f - at, fps: FPS, config: {damping: 12, stiffness: 380, mass: 0.5}});
        const s = lerp(1.6, 1, spr);
        const g = GLYPHS.heroes.find((x) => x.key === key)!;
        return (
          <React.Fragment key={key}>
            <Glyph file={g.file} size={size * s} x={540} y={ys[i]} opacity={clamp01((f - at + 1) / 2)} />
            <Burst frame={f} at={at} x={540} y={ys[i]} count={26} color={C.cream} spread={340} seed={`t-${key}`} ring={120} life={20} />
          </React.Fragment>
        );
      })}
      <Seal frame={f} at={h.seal} x={790} y={1430} size={96} />
    </>
  );
};

// ---------------- 文字层（不随震屏）
const HookText: React.FC<{f: number}> = ({f}) => {
  // 计数：闪切期间加速滚到 1412，碑墙期间停在 1412。
  const u = clamp01((f - h.flashStart) / (h.flashEnd - 1 - h.flashStart));
  const count = f >= h.flashEnd - 1 ? GLYPHS.total : Math.floor(GLYPHS.total * u ** 2.4);
  const counterIn = interpolate(f, [h.flashStart, h.flashStart + 4], [0, 1], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});
  const counterOut = interpolate(f, [h.landStart - 8, h.landStart], [1, 0], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});
  const v3 = speech('vo03');
  const labels = ['拓本', '墨迹', '描红'];
  const ti = h.triple.filter((t) => f >= t).length - 1;
  const labelColor = [C.cream, C.ink, C.red][Math.max(0, ti)];
  const inv = flashInverted(f);
  return (
    <>
      <Karaoke
        frame={f}
        text="1274 年前的字"
        y={300}
        size={124}
        color={inv ? C.ink : C.cream}
        accent={C.vermilion}
        accentChars="1274"
        show={{from: 0, to: h.wallStart}}
      />
      <Karaoke
        frame={f}
        text="唐天宝十一载 · 颜真卿《多宝塔碑》"
        y={1560}
        size={38}
        weight={600}
        color="rgba(239,231,214,0.72)"
        letterSpacing={0.08}
        show={{from: 0, to: h.flashStart}}
      />
      {f >= h.flashStart && f < h.landStart ? (
        <>
          {f >= h.wallStart ? (
            <div
              style={{
                position: 'absolute',
                left: 0,
                top: 1280,
                width: 1080,
                height: 400,
                opacity: counterOut * clamp01((f - h.wallStart) / 6),
                background: 'linear-gradient(rgba(24,24,24,0) 0%, rgba(24,24,24,0.92) 35%, rgba(24,24,24,0.92) 80%, rgba(24,24,24,0) 100%)',
              }}
            />
          ) : null}
          <Counter
            value={count}
            y={1465}
            size={170}
            color={inv ? C.ink : C.cream}
            suffixColor={inv ? C.ink : C.cream}
            suffix="个碑帖原字"
            opacity={counterIn * counterOut}
            scale={1 + 0.08 * pulse(f, h.flashEnd - 1, 10)}
          />
          {f >= h.wallStart ? (
            <Karaoke
              frame={f}
              text="三本字帖 · 逐字原拓"
              y={1572}
              size={40}
              weight={600}
              color="rgba(239,231,214,0.8)"
              letterSpacing={0.1}
              show={{from: h.wallStart + 6, to: h.landStart}}
            />
          ) : null}
        </>
      ) : null}
      {ti >= 0 && f < h.title[0] ? (
        <div
          style={{
            position: 'absolute',
            top: 210,
            left: 0,
            width: 1080,
            textAlign: 'center',
            fontFamily: serif,
            fontWeight: 900,
            fontSize: 150,
            letterSpacing: '0.18em',
            color: labelColor,
            transform: `scale(${1 + 0.3 * pulse(f, h.triple[ti], 5)})`,
          }}
        >
          {labels[ti]}
        </div>
      ) : null}
      <Karaoke
        frame={f}
        text="现在，全部装进你的口袋"
        y={1585}
        size={54}
        weight={900}
        color={C.cream}
        accent={C.vermilion}
        accentChars="口袋"
        pill="rgba(24,24,24,0.78)"
        show={{from: v3.from, to: h.title[0]}}
        reveal={v3}
      />
      {f >= h.title[0] ? (
        <Karaoke
          frame={f}
          text="原碑集字"
          y={250}
          size={44}
          weight={600}
          color="rgba(239,231,214,0.75)"
          letterSpacing={0.5}
          show={{from: h.title[0], to: h.end}}
        />
      ) : null}
    </>
  );
};
