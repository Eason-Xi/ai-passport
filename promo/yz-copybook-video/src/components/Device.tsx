// AI Passport 设备：AI 生成的高清外壳（屏幕区已涂黑）+ 屏幕里合成真实界面截图。
import React from 'react';
import {Img, staticFile} from 'remotion';
import {DEVICE} from '../timeline';

export const ui = (name: string) => staticFile(`gen/ui/${name}.png`);

const S = DEVICE.screen;
// 屏幕中心、设备中心（device.png 像素坐标）。
export const SCREEN_CENTER = {x: S.x + S.w / 2, y: S.y + S.h / 2};
export const DEVICE_CENTER = {x: DEVICE.w / 2, y: DEVICE.h / 2};

// 已知屏幕上某点（设备像素）要落在画面 (px, py)，求设备中心的画面坐标。
export const deviceCenterFor = (devPt: {x: number; y: number}, px: number, py: number, k: number) => ({
  x: px - k * (devPt.x - DEVICE_CENTER.x),
  y: py - k * (devPt.y - DEVICE_CENTER.y),
});

// 界面截图坐标（960×1280）→ 设备像素坐标。
export const uiToDevice = (ux: number, uy: number) => ({
  x: S.x + (ux / 960) * S.w,
  y: S.y + (uy / 1280) * S.h,
});

export const Device: React.FC<{
  x: number;
  y: number;
  k: number;
  rotate?: number;
  opacity?: number;
  screen?: string; // gen/ui 下的截图名（不带扩展名）
  children?: React.ReactNode; // 自定义屏幕内容（优先于 screen）
  glow?: number;
}> = ({x, y, k, rotate = 0, opacity = 1, screen, children, glow = 0.18}) => (
  <div
    style={{
      position: 'absolute',
      left: x - DEVICE.w / 2,
      top: y - DEVICE.h / 2,
      width: DEVICE.w,
      height: DEVICE.h,
      transform: `scale(${k}) rotate(${rotate}deg)`,
      transformOrigin: '50% 50%',
      opacity,
    }}
  >
    {glow > 0 ? (
      <div
        style={{
          position: 'absolute',
          left: -DEVICE.w * 0.35,
          top: -DEVICE.h * 0.2,
          width: DEVICE.w * 1.7,
          height: DEVICE.h * 1.4,
          background: `radial-gradient(ellipse 50% 50% at 50% 50%, rgba(239,231,214,${glow}) 0%, rgba(239,231,214,0) 70%)`,
        }}
      />
    ) : null}
    <Img src={staticFile('gen/device/device.png')} style={{position: 'absolute', left: 0, top: 0, width: DEVICE.w, height: DEVICE.h}} />
    <div
      style={{
        position: 'absolute',
        left: S.x,
        top: S.y,
        width: S.w,
        height: S.h,
        borderRadius: S.r,
        overflow: 'hidden',
        background: '#000',
      }}
    >
      {children ?? (screen ? <Img src={ui(screen)} style={{width: '100%', height: '100%', display: 'block'}} /> : null)}
      {/* 屏幕玻璃的一道斜向反光 */}
      <div
        style={{
          position: 'absolute',
          inset: 0,
          background: 'linear-gradient(135deg, rgba(255,255,255,0.10) 0%, rgba(255,255,255,0.02) 38%, rgba(255,255,255,0) 60%)',
        }}
      />
    </div>
  </div>
);

// 屏幕内两张截图的纵向滑动切换（模拟滚动）：t 从 0 到 1。
export const ScreenSlide: React.FC<{from: string; to: string; t: number}> = ({from, to, t}) => (
  <div style={{position: 'absolute', inset: 0}}>
    <Img src={ui(from)} style={{position: 'absolute', left: 0, top: `${-t * 100}%`, width: '100%', height: '100%'}} />
    <Img src={ui(to)} style={{position: 'absolute', left: 0, top: `${(1 - t) * 100}%`, width: '100%', height: '100%'}} />
  </div>
);

// 屏幕内交叉溶解。
export const ScreenFade: React.FC<{from: React.ReactNode; to: string; t: number}> = ({from, to, t}) => (
  <div style={{position: 'absolute', inset: 0}}>
    {from}
    <Img src={ui(to)} style={{position: 'absolute', inset: 0, width: '100%', height: '100%', opacity: t}} />
  </div>
);
