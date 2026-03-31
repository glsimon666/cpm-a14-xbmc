/*
 * Copyright (C) 2024 Team Kodi
 * This file is part of Kodi - https://kodi.tv
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * See LICENSES/README.md for more information.
 */

#version 100

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

uniform sampler2D m_samp0;
uniform sampler2D m_samp1;
varying vec4 m_cord0;
varying vec4 m_cord1;
uniform lowp vec4 m_unicol;

// 原始参数，用于非默认值时的实时计算
uniform float m_sdrPeak;
uniform float m_sdrSaturation;

// --- 新增：LUT 优化控制 ---
uniform sampler2D m_lutSampler; // 1024x32 纹理
uniform float m_useLut;         // 1.0 时查表，0.0 时计算
// -----------------------

highp float rand(highp vec2 co)
{
  // 用于 PQ 输出的抖动算法
  return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

// 快速路径：32阶三线性插值采样 (适配 1024x32 纹理)
vec3 samplePQLUT(vec3 color)
{
    // 将 0.0-1.0 映射到 31.0 索引空间
    vec3 c = clamp(color, 0.0, 1.0) * 31.0;
    
    float zSlice = floor(c.b);
    float zFract = fract(c.b);
    
    // 计算 2D 坐标。总宽 1024 = 32 切片 * 32 像素
    float x0 = (zSlice * 32.0 + c.r + 0.5) / 1024.0;
    float x1 = (min(zSlice + 1.0, 31.0) * 32.0 + c.r + 0.5) / 1024.0;
    float y = (c.g + 0.5) / 32.0;
    
    // R,G 通道由硬件插值，B 通道手动插值
    vec3 rgb0 = texture2D(m_lutSampler, vec2(x0, y)).rgb;
    vec3 rgb1 = texture2D(m_lutSampler, vec2(x1, y)).rgb;
    
    return mix(rgb0, rgb1, zFract);
}

// 慢速路径：原始 PQ 转换公式 (兜底逻辑)
vec3 computePQ(vec3 x)
{
  const float ST2084_m1 = 2610.0 / (4096.0 * 4.0);
  const float ST2084_m2 = (2523.0 / 4096.0) * 128.0;
  const float ST2084_c1 = 3424.0 / 4096.0;
  const float ST2084_c2 = (2413.0 / 4096.0) * 32.0;
  const float ST2084_c3 = (2392.0 / 4096.0) * 32.0;

  const mat3 matx = mat3(
      0.627402, 0.069095, 0.016394,
      0.329292, 0.919544, 0.088028,
      0.043306, 0.011360, 0.895578);

  x = max(x, vec3(0.0));
  x = pow(x, vec3(1.0 / 0.45)); // REC.709 to linear
  x = matx * x;                 // to BT.2020
  x = max(x, vec3(0.0));

  // 实时饱和度调节
  vec3 luma = vec3(dot(x, vec3(0.2627, 0.6780, 0.0593)));
  x = mix(luma, x, m_sdrSaturation);
  x = max(x, vec3(0.0));

  // 峰值缩放及 PQ 编码
  float peakNits = 100.0 * m_sdrPeak;
  x = pow(x * (peakNits / 10000.0), vec3(ST2084_m1));
  x = (ST2084_c1 + ST2084_c2 * x) / (1.0 + ST2084_c3 * x);
  x = pow(x, vec3(ST2084_m2));

  return x;
}

vec3 transferPQ(vec3 x)
{
  vec3 res;
  if (m_useLut > 0.5) {
      res = samplePQLUT(x);
  } else {
      res = computePQ(x);
  }

  // 抖动处理消除 8-bit LUT 阶梯感
  float dither = (rand(gl_FragCoord.xy) - 0.5) / 1024.0;
  return clamp(res + vec3(dither), 0.0, 1.0);
}

void main()
{
  // 混合基础色、采样颜色和掩码
  gl_FragColor = m_unicol;
  gl_FragColor *= texture2D(m_samp0, m_cord0.xy);
  gl_FragColor.a *= texture2D(m_samp1, m_cord1.xy).r;

#if defined(KODI_TRANSFER_PQ)
  // 执行 PQ 逻辑切换
  gl_FragColor.rgb = transferPQ(gl_FragColor.rgb);
#endif

#if defined(KODI_LIMITED_RANGE)
  // 有限范围处理
  gl_FragColor.rgb *= (235.0 - 16.0) / 255.0;
  gl_FragColor.rgb += 16.0 / 255.0;
#endif
}
