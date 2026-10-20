#pragma once

#include <algorithm>
#include <cmath>

namespace sway
{

// Where to start a second read of a signal so that it lines up with a first one: the glide's
// handovers between read heads and HOLD's grains both need it. `at(i)` is the signal at position i;
// a match compares `length` samples from each candidate start with as many from the reference.

// How alike the signal from `b` is to the signal from `a`, normalised by b's energy (a's is the same
// for every candidate). Every `stride`-th sample is compared.
template <typename At>
double matchScore(const At& at, int a, int b, int length, int stride)
{
    double ab = 0.0, bb = 1e-12;
    for (int k = 0; k < length; k += stride)
    {
        const float x = at(a + k), y = at(b + k);
        ab += (double) x * y;
        bb += (double) y * y;
    }
    return ab / std::sqrt(bb);
}

// Normalised correlation of the two, 0 (unrelated or opposite) - 1 (the same).
template <typename At>
float similarity(const At& at, int a, int b, int length, int stride)
{
    double aa = 1e-12, ab = 0.0, bb = 1e-12;
    for (int k = 0; k < length; k += stride)
    {
        const float x = at(a + k), y = at(b + k);
        aa += (double) x * x;
        ab += (double) x * y;
        bb += (double) y * y;
    }
    return (float) std::clamp(ab / std::sqrt(aa * bb), 0.0, 1.0);
}

struct Splice
{
    int start; // the best start found
    int cost;  // multiply-adds the search took
};

// Searches `centre` +- `radius` (kept within lo - hi) for the start that best matches the
// reference: coarse on a grid of `step`, then sample by sample around the best. `preferCentre`: of
// two good matches, take the one nearer to the centre.
template <typename At>
Splice findSplice(const At& at, int reference, int centre, int lo, int hi, int radius, int length, int step,
                  bool preferCentre)
{
    Splice found { std::clamp(centre, lo, hi), 0 };
    double bestScore = -1e30;
    auto consider = [&](int d, int stride) {
        if (d < lo || d > hi)
            return;
        double score = matchScore(at, reference, d, length, stride);
        found.cost += (length + stride - 1) / stride;
        if (preferCentre)
            score -= 0.5 * std::abs(score) * std::abs(d - centre) / radius;
        if (score > bestScore)
        {
            bestScore = score;
            found.start = d;
        }
    };
    for (int off = -radius; off <= radius; off += step)
        consider(centre + off, step);
    const int coarse = found.start;
    const int refineStride = std::max(1, step / 4);
    for (int d = coarse - step + 1; d <= coarse + step - 1; ++d)
        if (d != coarse)
            consider(d, refineStride);
    return found;
}

} // namespace sway
