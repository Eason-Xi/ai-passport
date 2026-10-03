import {Config} from '@remotion/cli/config';

Config.setVideoImageFormat('jpeg');
Config.setJpegQuality(95);
Config.setOverwriteOutput(true);
// 碑墙图集 2904×3872，放大到十几倍时只绘制可见部分，需要足够的超时时间解码。
Config.setDelayRenderTimeoutInMilliseconds(120000);
