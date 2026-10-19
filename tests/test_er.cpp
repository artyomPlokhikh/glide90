#include "Constants.h"
#include "ErEngine.h"
#include "RomPrograms.h"

#include <doctest/doctest.h>

#include <cmath>

using spx::ErEngine;
using spx::ErSettings;
using spx::ErType;

TEST_CASE("ER1 HALL at factory settings puts its 13 reflections where the real unit does")
{
    // Tap positions in DSP samples measured from the Millertone SPX90 capture of EARLY REF 1.
    const int measured[] = { 0, 136, 286, 456, 663, 986, 1190, 1518, 1717, 2006, 2329, 2792, 2998 };
    const ErSettings s = spx::romProgram(5).er;
    const auto taps = ErEngine::buildTaps(s, 0);
    REQUIRE(taps.size() == 13);
    const int pre = (int) std::lround(s.delayMs * spx::kDspRate / 1000.0);
    for (size_t i = 0; i < taps.size(); ++i)
    {
        CAPTURE(i);
        CHECK(std::abs(taps[i].delay - pre - measured[i]) <= 1);
    }
}

TEST_CASE("reflection times scale linearly with ROOM SIZE")
{
    ErSettings a = spx::romProgram(5).er, b = a;
    a.delayMs = b.delayMs = 0.1f;
    b.roomSize = 2.0f * a.roomSize;
    const auto ta = ErEngine::buildTaps(a, 0), tb = ErEngine::buildTaps(b, 0);
    REQUIRE(ta.size() == tb.size());
    const int pre = (int) std::lround(0.1 * spx::kDspRate / 1000.0);
    for (size_t i = 1; i < ta.size(); ++i)
        CHECK(std::abs((tb[i].delay - pre) - 2 * (ta[i].delay - pre)) <= 1);
}

TEST_CASE("REVERSE builds up towards its last reflections")
{
    ErSettings s = spx::romProgram(17).er;
    s.density = 1;
    const auto taps = ErEngine::buildTaps(s, 0);
    REQUIRE(taps.size() >= 12);
    float loudest = 0.0f;
    for (const auto& t : taps)
        loudest = std::max(loudest, std::abs(t.gain));
    CHECK(std::abs(taps.front().gain) < 0.1f * loudest);
    CHECK(std::abs(taps.back().gain) > 0.9f * loudest);
}

TEST_CASE("LIVENESS flattens the pattern when raised and steepens it when lowered")
{
    ErSettings s = spx::romProgram(17).er;
    s.density = 1;
    auto quietest = [&](int liveness) {
        s.liveness = liveness;
        const auto t = ErEngine::buildTaps(s, 0);
        float quiet = 1e9f, loud = 0.0f;
        for (const auto& x : t)
        {
            quiet = std::min(quiet, std::abs(x.gain));
            loud = std::max(loud, std::abs(x.gain));
        }
        return quiet / loud;
    };
    CHECK(quietest(10) > quietest(5));
    CHECK(quietest(0) < quietest(5));
}

TEST_CASE("Reverse Gate at ROOM SIZE 20 and 400 ms still fits the delay memory")
{
    ErSettings s = spx::romProgram(17).er;
    s.roomSize = 20.0f;
    s.delayMs = 400.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto taps = ErEngine::buildTaps(s, ch);
        REQUIRE(!taps.empty());
        CHECK(taps.back().delay < spx::kDelayMemory);
        // The whole swell survives: nothing was cut off.
        s.roomSize = 3.3f;
        const auto ref = ErEngine::buildTaps(s, ch);
        s.roomSize = 20.0f;
        CHECK(taps.size() >= ref.size() * 9 / 10);
    }
}

TEST_CASE("engine output is the tap sum and stays finite")
{
    ErEngine e;
    e.prepare();
    e.setSettings(spx::romProgram(17).er);
    std::vector<float> in(40000, 0.0f), l(in.size()), r(in.size());
    in[0] = 1.0f;
    e.process(in.data(), l.data(), r.data(), (int) in.size());
    const auto& taps = e.tapsL();
    for (const auto& t : taps)
        if (t.delay < (int) in.size())
            CHECK(l[(size_t) t.delay] == doctest::Approx(t.gain).epsilon(1e-4));
    for (float v : l)
        REQUIRE(std::isfinite(v));
}

TEST_CASE("turning ROOM SIZE while it plays crossfades without steps")
{
    // A knob turned (or automated) sends new settings every block, faster than the 20 ms crossfade.
    // Each must not cut off the tap set that is still fading out.
    const int block = 42; // about 64 host samples at 48 kHz
    const int n = 200 * block + 40000;
    std::vector<float> in((size_t) n);
    for (int i = 0; i < n; ++i)
        in[(size_t) i] = 0.25f * (float) std::sin(2.0 * 3.141592653589793 * 200.0 * i / spx::kDspRate);

    auto largestStep = [&](bool turning) {
        ErEngine e;
        e.prepare();
        ErSettings s = spx::romProgram(17).er;
        std::vector<float> l((size_t) n), r((size_t) n);
        for (int start = 0; start < n; start += block)
        {
            if (turning && start >= 40000)
                s.roomSize = std::min(20.0f, 3.3f + 0.1f * (float) ((start - 40000) / block));
            else if (!turning)
                s.roomSize = start < 40000 ? 3.3f : 20.0f;
            e.setSettings(s);
            e.process(in.data() + start, l.data() + start, r.data() + start, std::min(block, n - start));
        }
        float worst = 0.0f;
        for (int i = 40001; i < n; ++i)
            worst = std::max(worst, std::abs(l[(size_t) i] - l[(size_t) i - 1]));
        return worst;
    };
    const float still = largestStep(false), turning = largestStep(true);
    MESSAGE("largest sample-to-sample step: one change " << still << ", turning " << turning);
    CHECK(turning < 1.5f * still);
}
