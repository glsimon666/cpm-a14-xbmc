/*
 * Copyright (C) 2010-2013 Team XBMC
 * http://xbmc.org
 *
 * This Program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#version 100

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

uniform sampler2D m_samp0;
varying vec4 m_cord0;
uniform float m_sdrPeak;
uniform float m_sdrSaturation;

// --- 新增：LUT 优化控制 ---
uniform sampler2D m_lutSampler; // 1024x32 纹理
uniform float m_useLut;         // 1.0 时使用 LUT，0.0 时回归公式
// -----------------------

#if defined(KODI_HDR_PGS_ADJUST)
uniform float m_hdrPgsPeak;
uniform float m_hdrPgsSaturation;
#endif

highp float rand(highp vec2 co)
{
  return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

// 快速路径：32阶三线性插值采样
vec3 samplePQLUT(vec3 color)
{
    vec3 c = clamp(color, 0.0, 1.0) * 31.0;
    float zSlice = floor(c.b);
    float zFract = fract(c.b);
    float x0 = (zSlice * 32.0 + c.r + 0.5) / 1024.0;
    float x1 = (min(zSlice + 1.0, 31.0) * 32.0 + c.r + 0.5) / 1024.0;
    float y = (c.g + 0.5) / 32.0;
    vec3 rgb0 = texture2D(m_lutSampler, vec2(x0, y)).rgb;
    vec3 rgb1 = texture2D(m_lutSampler, vec2(x1, y)).rgb;
    return mix(rgb0, rgb1, zFract);
}

// 慢速路径：原始数学计算
vec3 computeTransferPQ(vec3 x)
{
  const float ST2084_m1 = 2610.0 / (4096.0 * 4.0);
  const float ST2084_m2 = (2523.0 / 4096.0) * 128.0;
  const float ST2084_c1 = 3424.0 / 4096.0;
  const float ST2084_c2 = (2413.0 / 4096.0) * 32.0;
  const float ST2084_c3 = (2392.0 / 4096.0) * 32.0;
  const mat3 matx = mat3(0.627402, 0.069095, 0.016394, 0.329292, 0.919544, 0.088028, 0.043306, 0.011360, 0.895578);

  x = max(x, vec3(0.0));
  x = pow(x, vec3(1.0 / 0.45));
  x = matx * x;
  x = max(x, vec3(0.0));
  vec3 luma = vec3(dot(x, vec3(0.2627, 0.6780, 0.0593)));
  x = mix(luma, x, m_sdrSaturation);
  x = max(x, vec3(0.0));
  float peakNits = 100.0 * m_sdrPeak;
  x = pow(x * (peakNits / 10000.0), vec3(ST2084_m1));
  x = (ST2084_c1 + ST2084_c2 * x) / (1.0 + ST2084_c3 * x);
  x = pow(x, vec3(ST2084_m2));
  return x;
}

vec3 transferPQ(vec3 x)
{
  vec3 res;
  if (m_useLut > 0.5) res = samplePQLUT(x);
  else res = computeTransferPQ(x);
  
  float dither = (rand(gl_FragCoord.xy) - 0.5) / 1024.0;
  return clamp(res + vec3(dither), 0.0, 1.0);
}

#if defined(KODI_HDR_PGS_ADJUST)
// 注意：HDR PGS 调节逻辑不建议走 LUT，因为其输入本身就是 PQ 编码，通常建议保持数学精确
vec3 decodePQ(vec3 x)
{
  const float ST2084_m1 = 2610.0 / (4096.0 * 4.0);
  const float ST2084_m2 = (2523.0 / 4096.0) * 128.0;
  const float ST2084_c1 = 3424.0 / 4096.0;
  const float ST2084_c2 = (2413.0 / 4096.0) * 32.0;
  const float ST2084_c3 = (2392.0 / 4096.0) * 32.0;

  x = clamp(x, vec3(0.0), vec3(1.0));
  vec3 p = pow(x, vec3(1.0 / ST2084_m2));
  vec3 num = max(p - vec3(ST2084_c1), vec3(0.0));
  vec3 den = max(vec3(ST2084_c2) - vec3(ST2084_c3) * p, vec3(1e-6));
  return pow(num / den, vec3(1.0 / ST2084_m1));
}

vec3 encodePQ(vec3 x)
{
  const float ST2084_m1 = 2610.0 / (4096.0 * 4.0);
  const float ST2084_m2 = (2523.0 / 4096.0) * 128.0;
  const float ST2084_c1 = 3424.0 / 4096.0;
  const float ST2084_c2 = (2413.0 / 4096.0) * 32.0;
  const float ST2084_c3 = (2392.0 / 4096.0) * 32.0;

  x = max(x, vec3(0.0));
  vec3 p = pow(x, vec3(ST2084_m1));
  vec3 y = (vec3(ST2084_c1) + vec3(ST2084_c2) * p) / (vec3(1.0) + vec3(ST2084_c3) * p);
  y = pow(y, vec3(ST2084_m2));
  return clamp(y, vec3(0.0), vec3(1.0));
}

vec3 adjustHdrPgsPQ(vec3 pq)
{
  vec3 linear = decodePQ(pq);
  vec3 luma = vec3(dot(linear, vec3(0.2627, 0.6780, 0.0593)));
  linear = mix(luma, linear, m_hdrPgsSaturation);
  linear = max(linear, vec3(0.0));
  linear *= m_hdrPgsPeak;
  return encodePQ(linear);
}
#endif

void main ()
{
  vec4 rgb = texture2D(m_samp0, m_cord0.xy);

#if defined(KODI_TRANSFER_PQ)
  // 当从 SDR GUI 转换到 HDR 输出时，优先走 LUT 快速路径
  rgb.rgb = transferPQ(rgb.rgb);
#elif defined(KODI_HDR_PGS_ADJUST)
  // 调节已经处于 HDR PQ 空间的字幕，走数学公式
  rgb.rgb = adjustHdrPgsPQ(rgb.rgb);
#endif

#if defined(KODI_LIMITED_RANGE)
  rgb.rgb *= (235.0 - 16.0) / 255.0;
  rgb.rgb += 16.0 / 255.0;
#endif

  gl_FragColor = rgb;
}
