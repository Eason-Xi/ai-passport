// 10–25 s 功能段：三本字帖 → 笔法卡 → 格线底色 → 计时记遍 → 千字文全文 → 进度。
// 每 0.5–1 s 一次打断（切屏 / 推镜 / 底色翻转），间隔随旁白，不做等距节拍器。
import React from 'react';
import {AbsoluteFill, Img, interpolate, spring, staticFile} from 'remotion';
import {clamp01, easeInOutCubic, lerp, pulse, shake} from '../components/primitives';
import {DEVICE_CENTER, Device, ScreenSlide, deviceCenterFor, ui, uiToDevice} from '../components/Device';
import {Counter, Karaoke, Tag} from '../components/Text';
import {C, DEVICE_K, sans, serif} from '../theme';
import {FPS, GLYPHS, scene, speech} from '../timeline';

const clampOpts = {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'} as const;
const cut = (f: number, beats: number[]) => beats.filter((b) => f >= b).length - 1;

// ---------------------------------------------------------------- 三本字帖
const LIB = scene('library');
const BOOKS = [
  {ui: '24_library_duobao', x: 300, y: 1050, rot: -8, w: 420, badge: '752 · 188 字'},
  {ui: '25_library_qinli', x: 780, y: 1050, rot: 8, w: 420, badge: '779 · 224 字'},
  {ui: '38_library_qianzi', x: 540, y: 1010, rot: 0, w: 470, badge: '1000 字'},
];

export const Library: React.FC<{frame: number}> = ({frame: f}) => {
  const v = speech('vo04');
  const drift = 1 + 0.04 * clamp01((f - LIB.from) / (LIB.to - LIB.from));
  return (
    <AbsoluteFill style={{backgroundColor: C.stone}}>
      <AbsoluteFill style={{transform: `scale(${drift})`}}>
        {BOOKS.map((b, i) => {
          const at = LIB.beats[i];
          if (f < at) return null;
          const spr = spring({frame: f - at, fps: FPS, config: {damping: 14, stiffness: 220, mass: 0.7}});
          const h = (b.w * 4) / 3;
          const y = lerp(b.y + 900, b.y, spr);
          const rot = lerp(b.rot * 3, b.rot, spr);
          return (
            <div
              key={b.ui}
              style={{
                position: 'absolute',
                left: b.x - b.w / 2,
                top: y - h / 2,
                width: b.w,
                height: h,
                transform: `rotate(${rot}deg)`,
              }}
            >
              <Img
                src={ui(b.ui)}
                style={{
                  width: '100%',
                  height: '100%',
                  borderRadius: (b.w * 30) / 240,
                  boxShadow: '0 40px 90px rgba(0,0,0,0.75), 0 0 0 2px rgba(239,231,214,0.18)',
                }}
              />
              <div style={{position: 'absolute', right: -14, top: -30}}>
                <Tag text={b.badge} size={40} scale={1 + 0.25 * pulse(f, at + 4, 6)} opacity={clamp01((f - at - 3) / 3)} />
              </div>
            </div>
          );
        })}
      </AbsoluteFill>
      <Karaoke frame={f} text="三本经典" y={265} size={130} color={C.cream} letterSpacing={0.2} show={{from: LIB.from, to: LIB.to}} />
      <Karaoke
        frame={f}
        text="多宝塔碑 · 颜勤礼碑 · 千字文"
        y={400}
        size={54}
        weight={600}
        color={C.cream}
        accent={C.vermilion}
        accentChars="·"
        show={{from: v.from, to: LIB.to}}
        reveal={v}
      />
    </AbsoluteFill>
  );
};

// ---------------------------------------------------------------- 笔法卡
const CARD = scene('card');
export const Card: React.FC<{frame: number}> = ({frame: f}) => {
  const v = speech('vo05');
  const flipAt = CARD.beats[1];
  // 翻卡：屏幕横向压扁到 0 再展开，中点换图
  const flip = interpolate(f, [flipAt - 3, flipAt, flipAt + 3], [1, 0, 1], clampOpts);
  const screen = f < flipAt ? '12_practice_yong_nogrid' : '13_practice_yong_card';
  // 推镜：把笔法卡区域推到画面中央
  const z = easeInOutCubic((f - (flipAt + 10)) / 26);
  const k = DEVICE_K * lerp(1, 1.45, z);
  const focus = uiToDevice(480, 520);
  const target = {
    x: lerp(540 + (focus.x - DEVICE_CENTER.x) * DEVICE_K, 540, z),
    y: lerp(1010 + (focus.y - DEVICE_CENTER.y) * DEVICE_K, 1040, z),
  };
  const c = deviceCenterFor(focus, target.x, target.y, k);
  const tags = ['笔法', '结构', '出处'];
  const tagAt = [0, 22, 41].map((d) => v.from + d);
  return (
    <AbsoluteFill style={{backgroundColor: C.cream}}>
      <Device x={c.x} y={c.y} k={k * (1 + 0.04 * pulse(f, CARD.from, 8))} glow={0}>
        <div style={{position: 'absolute', inset: 0, transform: `scaleX(${flip})`}}>
          <Img src={ui(screen)} style={{width: '100%', height: '100%'}} />
        </div>
      </Device>
      <div
        style={{
          position: 'absolute',
          top: 0,
          left: 0,
          width: 1080,
          height: 520,
          background: `linear-gradient(${C.cream} 70%, rgba(239,231,214,0))`,
        }}
      />
      <div style={{position: 'absolute', top: 205, left: 0, width: 1080, display: 'flex', justifyContent: 'center', gap: 34}}>
        {tags.map((t, i) => (
          <Tag
            key={t}
            text={t}
            size={92}
            opacity={clamp01((f - tagAt[i]) / 2)}
            scale={f < tagAt[i] ? 0 : 1 + 0.35 * pulse(f, tagAt[i], 6)}
          />
        ))}
      </div>
      <Karaoke frame={f} text="一字一讲" y={395} size={64} color={C.ink} letterSpacing={0.3} show={{from: v.from + 60, to: CARD.to}} />
    </AbsoluteFill>
  );
};

// ---------------------------------------------------------------- 格线底色
const GRIDS = scene('grids');
const GRID_SCREENS = [
  {ui: '21_settings', dark: false},
  {ui: '14_practice_last', dark: true},
  {ui: '23_practice_after_full_loop', dark: false},
  {ui: '09_practice_paper_tian_fav', dark: false},
  {ui: '10_practice_trace_jiu_timer', dark: false},
];
export const Grids: React.FC<{frame: number}> = ({frame: f}) => {
  const v = speech('vo06');
  const i = Math.max(0, cut(f, GRIDS.beats));
  const s = GRID_SCREENS[i];
  const beat = GRIDS.beats.reduce((acc, b) => acc + pulse(f, b, 8), 0);
  const rot = (i % 2 ? -1 : 1) * 2.5;
  const fg = s.dark ? C.cream : C.ink;
  const mid = v.from + Math.round((v.to - v.from) * 0.45);
  return (
    <AbsoluteFill style={{backgroundColor: s.dark ? C.stone : C.cream}}>
      {i === 4 ? (
        <AbsoluteFill>
          {[1, 2].map((n) => (
            <React.Fragment key={n}>
              <div style={{position: 'absolute', left: (1080 * n) / 3, top: 0, width: 3, height: 1920, background: C.trace, opacity: 0.4}} />
              <div style={{position: 'absolute', left: 0, top: (1920 * n) / 3, width: 1080, height: 3, background: C.trace, opacity: 0.4}} />
            </React.Fragment>
          ))}
        </AbsoluteFill>
      ) : null}
      <Device x={540} y={1060} k={DEVICE_K * (1 + 0.06 * beat)} rotate={rot} screen={s.ui} glow={s.dark ? 0.14 : 0} />
      <Karaoke frame={f} text="格线 · 底色" y={265} size={124} color={fg} accent={C.red} accentChars="·" show={{from: GRIDS.from, to: GRIDS.to}} reveal={{from: v.from, to: mid}} />
      <Karaoke frame={f} text="一键切换" y={395} size={64} color={fg} letterSpacing={0.3} show={{from: mid, to: GRIDS.to}} reveal={{from: mid, to: v.to}} />
    </AbsoluteFill>
  );
};

// ---------------------------------------------------------------- 计时临写
const TIMER = scene('timer');
export const Timer: React.FC<{frame: number}> = ({frame: f}) => {
  const v = speech('vo07');
  const done = TIMER.beats[1];
  const secs = Math.round(interpolate(f, [TIMER.from + 2, done - 2], [36, 60], clampOpts));
  const screen = f < done ? '10_practice_trace_jiu_timer' : '11_practice_timer_done';
  const sh = shake(f, [[done, 12, 10]]);
  return (
    <AbsoluteFill style={{backgroundColor: C.stone}}>
      <AbsoluteFill style={{transform: `translate(${sh.x}px, ${sh.y}px)`}}>
        <Device x={540} y={1080} k={0.74 * (1 + 0.05 * pulse(f, done, 8))} screen={screen} />
        {f >= done ? (
          <div style={{position: 'absolute', left: 700, top: 610, transform: 'rotate(-8deg)'}}>
            <Tag text="共 87 遍" size={64} scale={1 + 0.5 * pulse(f, done, 6)} />
          </div>
        ) : null}
      </AbsoluteFill>
      <div
        style={{
          position: 'absolute',
          top: 375,
          width: 1080,
          textAlign: 'center',
          fontFamily: serif,
          fontWeight: 900,
          fontSize: 150,
          color: C.vermilion,
          fontVariantNumeric: 'tabular-nums',
          lineHeight: 1,
          transform: `scale(${1 + 0.15 * pulse(f, done, 8)})`,
        }}
      >
        {`${Math.floor(secs / 60)}:${String(secs % 60).padStart(2, '0')}`}
      </div>
      <Karaoke frame={f} text="计时临写 · 自动计数" y={270} size={88} color={C.cream} accent={C.vermilion} accentChars="·" show={{from: TIMER.from, to: TIMER.to}} reveal={v} />
    </AbsoluteFill>
  );
};

// ---------------------------------------------------------------- 千字文全文
const QZ = scene('qianzi');
const wall = GLYPHS.wall;
export const Qianzi: React.FC<{frame: number}> = ({frame: f}) => {
  const v = speech('vo08');
  const [, slideAt, practiceAt] = QZ.beats;
  const slide = easeInOutCubic((f - slideAt) / 8);
  let content: React.ReactNode;
  if (f < practiceAt) content = <ScreenSlide from="40_catalog_qianzi_full" to="41_catalog_qianzi_full_scrolled" t={slide} />;
  else content = <Img src={ui('42_practice_qianzi')} style={{width: '100%', height: '100%'}} />;
  // 背景：千字文所在的碑墙右侧几列缓缓上移
  const s = 0.62;
  const scroll = (f - QZ.from) * 2.2;
  const n = Math.round(interpolate(f, [slideAt, practiceAt], [0, 1000], {...clampOpts, easing: (t) => 1 - (1 - t) ** 3}));
  return (
    <AbsoluteFill style={{backgroundColor: C.stone}}>
      <div style={{position: 'absolute', inset: 0, opacity: 0.16}}>
        <Img
          src={staticFile(wall.file)}
          style={{position: 'absolute', left: 1080 - wall.cols * wall.tile * s + 40, top: -200 - scroll, width: wall.cols * wall.tile * s, height: wall.rows * wall.tile * s}}
        />
      </div>
      <Device x={540} y={1060} k={0.76 * (1 + 0.05 * pulse(f, practiceAt, 8) + 0.03 * pulse(f, QZ.from, 8))}>
        {content}
      </Device>
      <Karaoke frame={f} text="千字文全文" y={270} size={110} color={C.cream} letterSpacing={0.12} show={{from: QZ.from, to: QZ.to}} reveal={{from: v.from, to: v.from + 30}} />
      {f >= slideAt ? <Counter value={n} y={430} size={110} color={C.vermilion} suffix="字" /> : null}
    </AbsoluteFill>
  );
};

// ---------------------------------------------------------------- 进度
const PROG = scene('progress');
export const Progress: React.FC<{frame: number}> = ({frame: f}) => {
  const at = PROG.beats[1];
  const screen = f < at ? '01_home_new' : '02_home_progress_lowbat';
  const n = Math.round(interpolate(f, [at, at + 20], [0, 86], {...clampOpts, easing: (t) => 1 - (1 - t) ** 2}));
  const num = (x: number) => (
    <span style={{color: C.red, fontFamily: serif, fontWeight: 900, fontSize: 112, fontVariantNumeric: 'tabular-nums', margin: '0 10px'}}>{x}</span>
  );
  return (
    <AbsoluteFill style={{backgroundColor: C.cream}}>
      <Device x={540} y={1090} k={0.74 * (1 + 0.06 * pulse(f, at, 8))} screen={screen} glow={0} />
      <Karaoke frame={f} text="进度自动保存" y={270} size={92} color={C.ink} letterSpacing={0.12} show={{from: PROG.from, to: PROG.to}} />
      {f >= at ? (
        <div
          style={{
            position: 'absolute',
            top: 365,
            width: 1080,
            display: 'flex',
            justifyContent: 'center',
            alignItems: 'baseline',
            fontFamily: sans,
            fontWeight: 700,
            fontSize: 52,
            color: C.ink,
          }}
        >
          已临{num(n)}遍 ·{num(n)}分钟
        </div>
      ) : null}
    </AbsoluteFill>
  );
};

export const BODY: Array<{id: string; C: React.FC<{frame: number}>}> = [
  {id: 'library', C: Library},
  {id: 'card', C: Card},
  {id: 'grids', C: Grids},
  {id: 'timer', C: Timer},
  {id: 'qianzi', C: Qianzi},
  {id: 'progress', C: Progress},
];

export const Body: React.FC<{frame: number}> = ({frame}) => (
  <>
    {BODY.map(({id, C: Scene}) => {
      const s = scene(id);
      return frame >= s.from && frame < s.to ? <Scene key={id} frame={frame} /> : null;
    })}
  </>
);

