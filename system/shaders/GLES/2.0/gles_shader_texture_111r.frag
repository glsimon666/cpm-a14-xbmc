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
uniform lowp vec4 m_unicol;
varying vec4 m_cord0;
uniform float m_sdrPeak;
uniform float m_sdrSaturation;

// --- 新增：LUT 优化相关控制 ---
uniform sampler2D m_lutSampler; // 1024x32 纹理句柄
uniform float m_useLut;         // 开关：1.0 使用 LUT, 0.0 使用实时计算
// -----------------------

highp float rand(highp vec2 co)
{
  // 用于抖动（Dithering）的稳定哈希函数
  return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

// 路径 A: 快速查表逻辑 (适配 32x32x32 映射到 1024x32 的纹理)
vec3 samplePQLUT(vec3 color)
{
    // 将 0.0-1.0 映射到 0.0-31.0 的索引空间
    vec3 c = clamp(color, 0.0, 1.0) * 31.0;
    
    float zSlice = floor(c.b);
    float zFract = fract(c.b);
    
    // 计算当前 slice 和下一个 slice 的 X 偏移 (总宽 1024 = 32个slice * 32px)
    // +0.5 偏移确保采样像素中心
    float x0 = (zSlice * 32.0 + c.r + 0.5) / 1024.0;
    float x1 = (min(zSlice + 1.0, 31.0) * 32.0 + c.r + 0.5) / 1024.0;
    float y = (c.g + 0.5) / 32.0;
    
    // R,G 通道的线性插值由 GPU 硬件完成
    // B 通道手动进行线性插值
    vec3 rgb0 = texture2D(m_lutSampler, vec2(x0, y)).rgb;
    vec3 rgb1 = texture2D(m_lutSampler, vec2(x1, y)).rgb;
    
    return mix(rgb0, rgb1, zFract);
}

// 路径 B: 原始数学计算逻辑 (兜底)
vec3 computePQ(vec3 x)
{
    const float ST2084_m1 = 2610.0 / (16384.0);
    const float ST2084_m2 = (2523.0 / 4096.0) * 128.0;
    const float ST2084_c1 = 3424.0 / 4096.0;
    const float ST2084_c2 = (2413.0 / 4096.0) * 32.0;
    const float ST2084_c3 = (2392.0 / 4096.0) * 32.0;

    const mat3 matx = mat3(
        0.627402, 0.069095, 0.016394,
        0.329292, 0.919544, 0.088028,
        0.043306, 0.011360, 0.895578);

    x = max(x, vec3(0.0));
    x = pow(x, vec3(1.0 / 0.45)); // 逆伽马补偿
    x = matx * x;                 // 色域转换
    x = max(x, vec3(0.0));

    // 实时饱和度混合
    vec3 luma = vec3(dot(x, vec3(0.2627, 0.6780, 0.0593)));
    x = mix(luma, x, m_sdrSaturation);
    x = max(x, vec3(0.0));

    // 亮度缩放及 PQ 编码
    float peakNits = 100.0 * m_sdrPeak;
    x = pow(x * (peakNits / 10000.0), vec3(ST2084_m1));
    x = (ST2084_c1 + ST2084_c2 * x) / (1.0 + ST2084_c3 * x);
    x = pow(x, vec3(ST2084_m2));

    return x;
}

// 统一接口
vec3 transferPQ(vec3 x)
{
    vec3 outColor;
    if (m_useLut > 0.5) {
        outColor = samplePQLUT(x);
    } else {
        outColor = computePQ(x);
    }
    
    // 抖动处理
    float dither = (rand(gl_FragCoord.xy) - 0.5) / 1024.0;
    return clamp(outColor + vec3(dither), 0.0, 1.0);
}

void main()
{
    // 获取基础色和 Alpha
    gl_FragColor = m_unicol;
    gl_FragColor.a *= texture2D(m_samp0, m_cord0.xy).r;

#if defined(KODI_TRANSFER_PQ)
    // 只有在 HDR PQ 开启时转换 RGB
    gl_FragColor.rgb = transferPQ(gl_FragColor.rgb);
#endif

#if defined(KODI_LIMITED_RANGE)
    // 有限范围转换
    gl_FragColor.rgb *= (235.0 - 16.0) / 255.0;
    gl_FragColor.rgb += 16.0 / 255.0;
#endif
}
