/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
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

// 原始参数，用于非默认值时的实时计算路径
uniform float m_sdrPeak;
uniform float m_sdrSaturation;

// --- 新增：LUT 优化相关 ---
uniform sampler2D m_lutSampler; // 对应 C++ 中绑定的 1024x32 纹理
uniform float m_useLut;         // 开关控制：1.0 使用查表，0.0 使用计算
// -----------------------

highp float rand(highp vec2 co)
{
  // 简单的抖动哈希函数，用于减弱色阶断层
  return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

// 路径 A: 快速查表逻辑 (针对 32x32x32 映射到 1024x32 的 2D 纹理)
vec3 samplePQLUT(vec3 color)
{
    // 将 0.0-1.0 映射到 0.0-31.0 的索引空间
    vec3 c = clamp(color, 0.0, 1.0) * 31.0;
    
    float zSlice = floor(c.b);
    float zFract = fract(c.b);
    
    // 计算当前 slice 和下一个 slice 的 X 偏移 (总宽 1024 = 32个slice * 32px)
    // 使用 +0.5 偏移来确保采样像素中心
    float x0 = (zSlice * 32.0 + c.r + 0.5) / 1024.0;
    float x1 = (min(zSlice + 1.0, 31.0) * 32.0 + c.r + 0.5) / 1024.0;
    float y = (c.g + 0.5) / 32.0;
    
    // R,G 通道的线性插值由 GPU 硬件完成 (前提是 C++ 设置了 GL_LINEAR)
    // B 通道的手动进行线性混合 (mix)
    vec3 rgb0 = texture2D(m_lutSampler, vec2(x0, y)).rgb;
    vec3 rgb1 = texture2D(m_lutSampler, vec2(x1, y)).rgb;
    
    return mix(rgb0, rgb1, zFract);
}

// 路径 B: 原始数学计算逻辑 (兜底路径)
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

    // REC.709 逆伽马补偿转为线性
    x = pow(x, vec3(1.0 / 0.45));

    // 色域转换: REC.709 to BT.2020
    x = matx * x;
    x = max(x, vec3(0.0));

    // 实时饱和度混合
    vec3 luma = vec3(dot(x, vec3(0.2627, 0.6780, 0.0593)));
    x = mix(luma, x, m_sdrSaturation);
    x = max(x, vec3(0.0));

    // 实时亮度缩放并进行 PQ 编码 (ST2084)
    float peakNits = 100.0 * m_sdrPeak;
    x = pow(x * (peakNits / 10000.0), vec3(ST2084_m1));
    x = (ST2084_c1 + ST2084_c2 * x) / (1.0 + ST2084_c3 * x);
    x = pow(x, vec3(ST2084_m2));

    return x;
}

// 统一转接函数
vec3 transferPQ(vec3 x)
{
    vec3 res;
    if (m_useLut > 0.5) {
        res = samplePQLUT(x);
    } else {
        res = computePQ(x);
    }

    // 抖动处理：将计算或查表后的结果进行 10bit 级别的微小扰动，消除色带
    float dither = (rand(gl_FragCoord.xy) - 0.5) / 1024.0;
    return clamp(res + vec3(dither), 0.0, 1.0);
}

void main()
{
    // 基础颜色采样
    vec4 tex0 = texture2D(m_samp0, m_cord0.xy);
    vec4 tex1 = texture2D(m_samp1, m_cord1.xy);

    // 标准 Kodi 混合逻辑：m_samp0 是颜色，m_samp1 通常是 Alpha 掩码
    gl_FragColor = m_unicol * tex0;
    gl_FragColor.a *= tex1.r;

#if defined(KODI_TRANSFER_PQ)
    // 执行转换逻辑
    gl_FragColor.rgb = transferPQ(gl_FragColor.rgb);
#endif

#if defined(KODI_LIMITED_RANGE)
    // 16-235 范围映射
    gl_FragColor.rgb *= (235.0 - 16.0) / 255.0;
    gl_FragColor.rgb += 16.0 / 255.0;
#endif
}
