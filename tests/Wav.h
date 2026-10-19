#pragma once

// Minimal WAV I/O for tests and tools: reads 16/24-bit PCM and 32-bit float, writes 32-bit float.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

struct Wav
{
    int sampleRate = 0;
    int channels = 0;
    std::vector<std::vector<float>> data; // [channel][sample]
};

inline bool readWav(const std::string& path, Wav& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), {});
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        return false;
    auto u16 = [&](size_t o) { return (uint32_t) (uint8_t) bytes[o] | (uint32_t) (uint8_t) bytes[o + 1] << 8; };
    auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };

    int format = 0, bits = 0;
    size_t pos = 12;
    while (pos + 8 <= bytes.size())
    {
        const uint32_t size = u32(pos + 4);
        if (std::memcmp(bytes.data() + pos, "fmt ", 4) == 0)
        {
            format = (int) u16(pos + 8);
            out.channels = (int) u16(pos + 10);
            out.sampleRate = (int) u32(pos + 12);
            bits = (int) u16(pos + 22);
            if (format == 0xFFFE)
                format = (int) u16(pos + 32);
        }
        else if (std::memcmp(bytes.data() + pos, "data", 4) == 0)
        {
            const int bytesPer = bits / 8;
            const size_t frames = size / (size_t) (bytesPer * out.channels);
            out.data.assign((size_t) out.channels, std::vector<float>(frames));
            const char* p = bytes.data() + pos + 8;
            for (size_t i = 0; i < frames; ++i)
                for (int c = 0; c < out.channels; ++c, p += bytesPer)
                {
                    float v = 0.0f;
                    if (format == 3 && bits == 32)
                        std::memcpy(&v, p, 4);
                    else if (bits == 16)
                        v = (float) (int16_t) (uint16_t) ((uint8_t) p[0] | (uint8_t) p[1] << 8) / 32768.0f;
                    else if (bits == 24)
                    {
                        int32_t s = (int32_t) ((uint32_t) (uint8_t) p[0] << 8 | (uint32_t) (uint8_t) p[1] << 16 | (uint32_t) (uint8_t) p[2] << 24);
                        v = (float) (s >> 8) / 8388608.0f;
                    }
                    out.data[(size_t) c][i] = v;
                }
            return format == 1 || format == 3;
        }
        pos += 8 + size + (size & 1);
    }
    return false;
}

inline bool writeWav(const std::string& path, const std::vector<std::vector<float>>& data, int sampleRate)
{
    std::ofstream f(path, std::ios::binary);
    if (!f || data.empty())
        return false;
    const uint32_t ch = (uint32_t) data.size(), frames = (uint32_t) data[0].size();
    const uint32_t dataBytes = frames * ch * 4;
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4);
    w32(36 + dataBytes);
    f.write("WAVEfmt ", 8);
    w32(16);
    w16(3);
    w16((uint16_t) ch);
    w32((uint32_t) sampleRate);
    w32((uint32_t) sampleRate * ch * 4);
    w16((uint16_t) (ch * 4));
    w16(32);
    f.write("data", 4);
    w32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (uint32_t c = 0; c < ch; ++c)
            f.write(reinterpret_cast<const char*>(&data[c][i]), 4);
    return true;
}
