// 配色取自设备真实界面（RGB565 量化后的值），保证视频底色与屏幕内容、首尾帧完全一致。
import {continueRender, delayRender, staticFile} from 'remotion';

export const C = {
  stone: '#181818', // 「拓本」底
  cream: '#efe7d6', // 拓本字 / 纸色
  ink: '#181810', // 「墨迹」字
  red: '#bd3029', // 界面强调红
  trace: '#e78e7b', // 「描红」字
  vermilion: '#d9473b', // 深色底上的醒目红
  line: '#4a4542',
};

// 思源宋体 / 思源黑体（Noto Serif SC / Noto Sans SC，OFL）的子集，由 scripts/fetch_fonts.py 生成。
export const serif = 'YZ Serif';
export const sans = 'YZ Sans';
const FACES: Array<[string, number, string]> = [
  [serif, 600, 'serif-600'],
  [serif, 900, 'serif-900'],
  [sans, 500, 'sans-500'],
  [sans, 700, 'sans-700'],
];
if (typeof document !== 'undefined') {
  const handle = delayRender('loading fonts');
  Promise.all(
    FACES.map(([family, weight, file]) =>
      new FontFace(family, `url(${staticFile(`gen/fonts/${file}.woff2`)}) format('woff2')`, {weight: String(weight)})
        .load()
        .then((face) => document.fonts.add(face)),
    ),
  ).then(() => continueRender(handle));
}

export const W = 1080;
export const H = 1920;

// 设备在画面中的标准摆放（缩放系数 k 作用于 device.png 原始像素）。
export const DEVICE_K = 0.8;
export const DEVICE_Y = 985;
