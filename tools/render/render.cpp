// Offline renderer: runs a WAV file (or an impulse) through Sway and the SPX90 engine.
//
//   glide90_render in=guitar.wav out=wet.wav program=17 rs=20 liv=4 delay=25 balance=1
//   glide90_render in=impulse rate=44100 seconds=2 out=ir.wav program=17
//   input=-6 output=0 (the plugin's INPUT and OUTPUT, in dB)
//   sway=1 depth=30 period=333 shape=0 random=0.15 smooth=0.2 direction=0
//   (shape: index into sway::kShapePresetNames; direction: 0 down, 1 both, 2 up)

#include "RomPrograms.h"
#include "SpxEngine.h"
#include "SwayProcessor.h"
#include "Wav.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

int main(int argc, char** argv)
{
    std::map<std::string, std::string> a;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto eq = arg.find('=');
        if (eq != std::string::npos)
            a[arg.substr(0, eq)] = arg.substr(eq + 1);
    }
    auto num = [&](const char* k, double def) { return a.count(k) ? std::stod(a[k]) : def; };
    if (!a.count("in") || !a.count("out"))
    {
        std::fprintf(stderr, "usage: glide90_render in=<file.wav|impulse> out=<file.wav> [program=17 rs= liv= delay= lpf= balance= input=dB output=dB sway=0|1 ...]\n");
        return 1;
    }

    std::vector<float> mono;
    double rate = num("rate", 44100.0);
    if (a["in"] == "impulse")
    {
        mono.assign((size_t) (num("seconds", 2.0) * rate), 0.0f);
        mono[0] = (float) num("amp", 0.5);
    }
    else
    {
        Wav w;
        if (!readWav(a["in"], w))
        {
            std::fprintf(stderr, "cannot read %s\n", a["in"].c_str());
            return 1;
        }
        rate = w.sampleRate;
        mono.assign(w.data[0].size(), 0.0f);
        for (const auto& ch : w.data)
            for (size_t i = 0; i < ch.size(); ++i)
                mono[i] += ch[i] / (float) w.data.size();
        mono.resize(mono.size() + (size_t) (num("tail", 2.0) * rate), 0.0f);
    }

    spx::SpxSettings s;
    s.program = (int) num("program", 17);
    s.er = spx::romProgram(s.program).er;
    s.er.roomSize = (float) num("rs", s.er.roomSize);
    s.er.liveness = (int) num("liv", s.er.liveness);
    s.er.delayMs = (float) num("delay", s.er.delayMs);
    s.er.lpfHz = (float) num("lpf", s.er.lpfHz);
    s.balance = (float) num("balance", 1.0);
    // The plugin's INPUT and OUTPUT, in dB, around the engine.
    const float inputGain = (float) std::pow(10.0, num("input", 0.0) / 20.0);
    const float outputGain = (float) std::pow(10.0, num("output", 0.0) / 20.0);
    for (auto& x : mono)
        x *= inputGain;

    sway::SwaySettings w;
    const auto shape = sway::makeShape((sway::ShapePreset) (int) num("shape", 0));
    w.enabled = num("sway", 0.0) != 0.0;
    w.shape = &shape;
    w.depthCents = (float) num("depth", w.depthCents);
    w.cycleSeconds = num("period", w.cycleSeconds * 1000.0) / 1000.0;
    w.random = (float) num("random", w.random);
    w.smooth = (float) num("smooth", w.smooth);
    w.direction = (sway::BendDirection) (int) num("direction", 0);

    const int block = 512;
    spx::SpxEngine engine;
    engine.prepare(rate, block);
    sway::SwayProcessor swayProc;
    swayProc.prepare(rate);

    std::vector<std::vector<float>> out(2, std::vector<float>(mono.size()));
    for (size_t i = 0; i < mono.size(); i += block)
    {
        const int n = (int) std::min<size_t>(block, mono.size() - i);
        if (w.enabled)
            swayProc.process(mono.data() + i, n, w);
        engine.process(mono.data() + i, out[0].data() + i, out[1].data() + i, n, s);
    }
    for (auto& ch : out)
        for (auto& x : ch)
            x *= outputGain;
    if (!writeWav(a["out"], out, (int) rate))
        return 1;
    std::printf("wrote %s (%zu samples at %.0f Hz, engine latency %d)\n", a["out"].c_str(), mono.size(), rate,
                engine.latencySamples());
    return 0;
}
