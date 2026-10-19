#pragma once

namespace spx
{

// The SPX90 DSP rate. The spec sheet says 31.25 kHz, but the tap spacing of two independent
// captures (SPX90 and SPX90II) puts every reflection on a grid of 8 MHz / 255 = 31372.55 Hz.
inline constexpr double kDspRate = 8.0e6 / 255.0;

// Delay memory in DSP samples (~2.09 s). Reverse Gate at ROOM SIZE 20 with 400 ms pre-delay needs
// ~1.8 s, so the unit had at least this much; longer patterns are cut off by the memory.
inline constexpr int kDelayMemory = 1 << 16;

// 16-bit linear converters.
inline constexpr float kAdcFullScale = 1.0f;
inline constexpr float kQuantStep = 1.0f / 32768.0f;

} // namespace spx
