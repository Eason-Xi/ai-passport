import React from 'react';
import {Composition, Still} from 'remotion';
import {CoverA, CoverB, RATIOS, type Ratio} from './covers/Covers';
import {Promo, promoSchema} from './Promo';
import {TL} from './timeline';

export const RemotionRoot: React.FC = () => (
  <>
    <Composition
      id="Promo"
      component={Promo}
      schema={promoSchema}
      defaultProps={{showSafeArea: false, musicVolume: 0.8}}
      durationInFrames={TL.durationInFrames}
      fps={TL.fps}
      width={TL.width}
      height={TL.height}
    />
    {(Object.keys(RATIOS) as Ratio[]).map((ratio) => (
      <React.Fragment key={ratio}>
        <Still id={`CoverA-${ratio}`} component={CoverA} defaultProps={{ratio}} width={RATIOS[ratio].w} height={RATIOS[ratio].h} />
        <Still id={`CoverB-${ratio}`} component={CoverB} defaultProps={{ratio}} width={RATIOS[ratio].w} height={RATIOS[ratio].h} />
      </React.Fragment>
    ))}
  </>
);
