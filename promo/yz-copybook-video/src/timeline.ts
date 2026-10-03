// timeline.json 是音画共用的唯一时间源；这里只做类型化与派生计算。
import tl from '../timeline.json';
import glyphs from '../public/gen/glyphs/glyphs.json';
import device from '../public/gen/device/device.json';
import voSpans from '../public/gen/vo/durations.json';

export const TL = tl;
export const GLYPHS = glyphs;
export const DEVICE = device;
export const FPS = tl.fps;

export const scene = (id: string) => {
  const s = tl.scenes.find((x) => x.id === id);
  if (!s) throw new Error(`unknown scene ${id}`);
  return s;
};

export type Span = {from: number; to: number};

// 某句旁白真正发声的起止帧（绝对帧）。
export const speech = (id: string): Span => {
  const v = tl.vo.find((x) => x.id === id);
  const d = (voSpans as Record<string, {start: number; end: number}>)[id];
  if (!v || !d) throw new Error(`unknown vo ${id}`);
  return {from: v.at + Math.round(d.start * FPS), to: v.at + Math.round(d.end * FPS)};
};
