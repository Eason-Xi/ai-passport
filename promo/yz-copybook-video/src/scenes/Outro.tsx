// 25–30 s 收尾：品牌标题 + 标语；最后 1 秒推进屏幕里的「永」，末帧与第 0 帧构图完全一致，循环无缝。
import React from 'react';
import {AbsoluteFill, Img, interpolate, spring, staticFile} from 'remotion';
import {Fill, Glyph, clamp01, lerp} from '../components/primitives';
import {DEVICE_CENTER, Device, deviceCenterFor, uiToDevice} from '../components/Device';
import {Karaoke, Seal} from '../components/Text';
import {C} from '../theme';
import {DEVICE, FPS, GLYPHS, TL, scene, speech} from '../timeline';
import {HERO} from './Hook';

const OUT = scene('outro');
const END = TL.durationInFrames - 1;
const K0 = 0.6;
const Y0 = 1110;
// 屏幕截图里「永」字形画布：中心 (480, 536)，边长 704（界面截图 960×1280 坐标）。
const GLYPH_DEV = uiToDevice(480, 536);
const GLYPH_W_DEV = (704 / 960) * DEVICE.screen.w;
const K_END = (HERO.size * HERO.slamFrom) / GLYPH_W_DEV;

export const Outro: React.FC<{frame: number}> = ({frame: f}) => {
  if (f < OUT.from) return null;
  const v = speech('vo09');
  const enter = spring({frame: f - OUT.from, fps: FPS, config: {damping: 15, stiffness: 160}});
  const loopAt = TL.loopZoom;
  const screen = f < loopAt - 4 ? '01_home_new' : '12_practice_yong_nogrid';

  // 循环推镜：缩放在对数空间插值，「永」的画面位置同步从屏幕上移到第 0 帧的位置。
  const u = clamp01((f - loopAt) / (END - loopAt));
  const e = u ** 2.6;
  const k = Math.exp(lerp(Math.log(K0), Math.log(K_END), e)) * lerp(0.86, 1, enter);
  const base = {x: 540, y: lerp(Y0 + 260, Y0, enter)};
  const g0 = {x: base.x + K0 * (GLYPH_DEV.x - DEVICE_CENTER.x), y: base.y + K0 * (GLYPH_DEV.y - DEVICE_CENTER.y)};
  const g = {x: lerp(g0.x, HERO.x, e), y: lerp(g0.y, HERO.y, e)};
  const c = f >= loopAt ? deviceCenterFor(GLYPH_DEV, g.x, g.y, k) : base;

  const textOut = interpolate(f, [loopAt - 4, loopAt + 4], [1, 0], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});
  // 末 10 帧：叠上与第 0 帧完全相同的「永」+ 光晕（拓本底 #181818）。
  const seam = interpolate(f, [END - 10, END], [0, 1], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});
  const yong = GLYPHS.heroes.find((x) => x.key === 'yong')!;
  // 叠加的高清「永」跟随屏幕里那个字的位置与大小，末帧正好落在第 0 帧的位置（glyphScale = 1）。
  const glyphScale = (k * GLYPH_W_DEV) / (HERO.size * HERO.slamFrom);
  const wordmark = interpolate(f, [OUT.from + 6, OUT.from + 14], [0, 0.9], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});

  return (
    <AbsoluteFill style={{backgroundColor: C.stone}}>
      <Device x={c.x} y={c.y} k={k} screen={screen} glow={0.2 * textOut} />
      <AbsoluteFill style={{opacity: textOut}}>
        <Img
          src={staticFile('gen/brand/logo-wordmark-dark.png')}
          style={{position: 'absolute', left: 540 - 110, top: 150, width: 220, opacity: wordmark}}
        />
        <Karaoke
          frame={f}
          text="AI Passport"
          y={290}
          size={72}
          weight={600}
          color={C.cream}
          letterSpacing={0.08}
          show={{from: OUT.from, to: TL.durationInFrames}}
          reveal={{from: v.from, to: v.from + 40}}
        />
        <Karaoke
          frame={f}
          text="颜真卿字帖"
          y={440}
          size={150}
          color={C.cream}
          letterSpacing={0.12}
          show={{from: v.from + 64, to: TL.durationInFrames}}
        />
        <Seal frame={f} at={OUT.beats[2]} x={880} y={640} size={92} />
        <Karaoke
          frame={f}
          text="随身临帖 · 一字一遍"
          y={1565}
          size={56}
          weight={600}
          color={C.cream}
          accent={C.vermilion}
          accentChars="·"
          letterSpacing={0.14}
          show={{from: OUT.beats[2], to: TL.durationInFrames}}
        />
      </AbsoluteFill>
      {seam > 0 ? (
        <>
          <Fill color={C.stone} opacity={seam} />
          <div
            style={{
              position: 'absolute',
              left: g.x - 700 * glyphScale,
              top: g.y - 700 * glyphScale,
              width: 1400 * glyphScale,
              height: 1400 * glyphScale,
              opacity: seam,
              background: 'radial-gradient(circle, rgba(239,231,214,0.28) 0%, rgba(239,231,214,0) 60%)',
            }}
          />
          <Glyph file={yong.file} size={HERO.size * HERO.slamFrom * glyphScale} x={g.x} y={g.y} opacity={seam} />
        </>
      ) : null}
    </AbsoluteFill>
  );
};
