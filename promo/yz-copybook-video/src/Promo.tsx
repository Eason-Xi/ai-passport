// 总时间线：开场 → 功能段 → 收尾，统一叠加颗粒与暗角；配乐整轨 + 旁白逐句。
import React from 'react';
import {AbsoluteFill, Audio, Sequence, staticFile, useCurrentFrame} from 'remotion';
import {z} from 'zod';
import {Grain, SafeArea, TintDefs, Vignette} from './components/primitives';
import {Body} from './scenes/Body';
import {Hook} from './scenes/Hook';
import {Outro} from './scenes/Outro';
import {TL} from './timeline';

export const promoSchema = z.object({
  showSafeArea: z.boolean(),
  musicVolume: z.number().min(0).max(1),
});

export const Promo: React.FC<z.infer<typeof promoSchema>> = ({showSafeArea, musicVolume}) => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill style={{backgroundColor: '#181818'}}>
      <TintDefs />
      <Hook frame={frame} />
      <Body frame={frame} />
      <Outro frame={frame} />
      <Vignette strength={0.45} />
      <Grain opacity={0.13} />
      {showSafeArea ? <SafeArea /> : null}
      <Audio src={staticFile('gen/audio/music.wav')} volume={musicVolume} />
      {TL.vo.map((v) => (
        <Sequence key={v.id} from={v.at} layout="none">
          <Audio src={staticFile(`gen/vo/${v.id}.wav`)} volume={1} />
        </Sequence>
      ))}
    </AbsoluteFill>
  );
};
