/*
 * Copyright (C) 2010-2013 Team XBMC
 * http://xbmc.org
 *
 * This Program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This Program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with XBMC; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

#version 100

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

uniform sampler2D m_samp0;
uniform lowp vec4 m_unicol;
varying vec4 m_cord0;
uniform float m_sdrPeak;
uniform float m_sdrSaturation;

// --- 新增：LUT 优化相关控制 ---
uniform sampler2D m_lutSampler; // 对应 C++ 中 1024x32 尺寸的纹理
uniform float m_useLut;         // 1.0 时开启 LUT 查表，0.0 时回归公式计算
// -----------------------

highp float rand(highp vec2 co)
{
  // 用于 PQ 输出的抖动算法，减弱色阶断层
  return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

// 快速路径：32x32x32 三线性插值采样
vec3 samplePQLUT(vec3 color)
{
    // 将 0.0-1.0 映射到 32 阶索引空间 [0, 31.0]
    vec3 c = clamp(color, 0.0, 1.0) * 31.0;
    
    float zSlice = floor(c.b);
    float zFract = fract(c.b);
    
    // 计算 2D 纹理坐标 (总宽 1024 = 32张切片 * 32像素)
    // 加上 0.5 偏移以对准像素中心
    float x0 = (zSlice * 32.0 + c.r + 0.5) / 1024.0;
    float x1 = (min(zSlice + 1.0, 31.0) * 32.0 + c.r + 0.5) / 1024.0;
    float y = (c.g + 0.5) / 32.0;
    
    // R,G 通道插值通过硬件 GL_LINEAR 自动完成
    // B 通道插值通过对相邻 Slice 进行 mix 完成
    vec3 rgb0 = texture2D(m_lutSampler, vec2(x0, y)).rgb;
    vec3 rgb1 = texture2D(m_lutSampler, vec2(x1, y)).rgb;
    
    return mix(rgb0, rgb1, zFract);
}

// 慢速路径：原始 PQ 转换公式 (用于非默认参数时的兜底)
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

  // REC.709 to linear
  x = pow(x, vec3(1.0 / 0.45));

  // REC.709 to BT.2020
  x = matx * x;
  x = max(x, vec3(0.0));

  // 实时饱和度调节
  vec3 luma = vec3(dot(x, vec3(0.2627, 0.6780, 0.0593)));
  x = mix(luma, x, m_sdrSaturation);
  x = max(x, vec3(0.0));

  // 缩放 SDR 峰值亮度并进行 PQ 编码
  float peakNits = 100.0 * m_sdrPeak;
  x = pow(x * (peakNits / 10000.0), vec3(ST2084_m1));
  x = (ST2084_c1 + ST2084_c2 * x) / (1.0 + ST2084_c3 * x);
  x = pow(x, vec3(ST2084_m2));

  return x;
}

vec3 transferPQ(vec3 x)
{
  vec3 result;
  
  if (m_useLut > 0.5) {
      result = samplePQLUT(x);
  } else {
      result = computePQ(x);
  }

  // 无论走哪条路径，最后统一进行 Dither 处理
  float dither = (rand(gl_FragCoord.xy) - 0.5) / 1024.0;
  return clamp(result + vec3(dither), 0.0, 1.0);
}

void main ()
{
  vec4 rgb;

  // 基础采样与颜色混合
  rgb = texture2D(m_samp0, m_cord0.xy).rgba * m_unicol;

#if defined(KODI_TRANSFER_PQ)
  // 执行 PQ 转换逻辑
  rgb.rgb = transferPQ(rgb.rgb);
#endif

#if defined(KODI_LIMITED_RANGE)
  // 16-235 范围转换
  rgb.rgb *= (235.0 - 16.0) / 255.0;
  rgb.rgb += 16.0 / 255.0;
#endif

  gl_FragColor = rgb;
}
