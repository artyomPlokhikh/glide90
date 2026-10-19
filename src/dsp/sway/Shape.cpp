#include "Shape.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace sway
{

Shape::Shape()
{
    const ShapePoint sine[] { { 0.0f, 0.0f }, { 0.5f, 1.0f } };
    setPoints(sine, 2);
}

void Shape::setPoints(const ShapePoint* points, int n)
{
    count = std::clamp(n, 1, kMaxPoints);
    for (int i = 0; i < count; ++i)
        pts[(size_t) i] = { std::clamp(points[i].x, 0.0f, 0.999f), std::clamp(points[i].y, 0.0f, 1.0f) };
    std::sort(pts.begin(), pts.begin() + count, [](const ShapePoint& a, const ShapePoint& b) { return a.x < b.x; });

    // Tangents of a periodic monotone cubic (Fritsch-Butland): flat at local extrema, a weighted
    // harmonic mean of the neighbouring slopes elsewhere.
    auto next = [this](int i) { return (i + 1) % count; };
    auto width = [this](int i) {
        const float w = (i + 1 < count ? pts[(size_t) i + 1].x : pts[0].x + 1.0f) - pts[(size_t) i].x;
        return std::max(w, 1e-4f);
    };
    for (int i = 0; i < count; ++i)
    {
        const int prev = (i + count - 1) % count;
        const float h0 = width(prev), h1 = width(i);
        const float d0 = (pts[(size_t) i].y - pts[(size_t) prev].y) / h0;
        const float d1 = (pts[(size_t) next(i)].y - pts[(size_t) i].y) / h1;
        tangents[(size_t) i] = d0 * d1 <= 0.0f ? 0.0f : 3.0f * (h0 + h1) / ((2.0f * h1 + h0) / d0 + (h1 + 2.0f * h0) / d1);
    }
}

float Shape::at(float phase) const
{
    if (count == 1)
        return pts[0].y;
    phase -= std::floor(phase);

    // The segment holding `phase`: from the last point at or before it to the next one, wrapping
    // from the last point of the cycle to the first point of the next.
    int i = -1;
    for (int k = 0; k < count && pts[(size_t) k].x <= phase; ++k)
        i = k;
    float x0, x1;
    if (i < 0)
    {
        i = count - 1;
        x0 = pts[(size_t) i].x - 1.0f;
        x1 = pts[0].x;
    }
    else
    {
        x0 = pts[(size_t) i].x;
        x1 = i + 1 < count ? pts[(size_t) i + 1].x : pts[0].x + 1.0f;
    }
    const int j = (i + 1) % count;
    const float h = std::max(x1 - x0, 1e-4f);
    const float t = std::clamp((phase - x0) / h, 0.0f, 1.0f);
    const float t2 = t * t, t3 = t2 * t;
    const float y = (2 * t3 - 3 * t2 + 1) * pts[(size_t) i].y + (t3 - 2 * t2 + t) * h * tangents[(size_t) i]
                    + (-2 * t3 + 3 * t2) * pts[(size_t) j].y + (t3 - t2) * h * tangents[(size_t) j];
    return std::clamp(y, 0.0f, 1.0f);
}

Shape makeShape(ShapePreset preset)
{
    auto make = [](std::initializer_list<ShapePoint> p) {
        Shape s;
        s.setPoints(std::data(p), (int) p.size());
        return s;
    };
    switch (preset)
    {
        case ShapePreset::Sine: return make({ { 0.0f, 0.0f }, { 0.5f, 1.0f } });
        case ShapePreset::Triangle: return make({ { 0.0f, 0.0f }, { 0.25f, 0.5f }, { 0.5f, 1.0f }, { 0.75f, 0.5f } });
        case ShapePreset::Dive: return make({ { 0.0f, 0.0f }, { 0.1f, 1.0f }, { 0.45f, 0.4f }, { 0.8f, 0.06f } });
        case ShapePreset::Swell: return make({ { 0.0f, 0.0f }, { 0.4f, 0.35f }, { 0.75f, 1.0f }, { 0.88f, 0.15f } });
        case ShapePreset::RampDown: return make({ { 0.0f, 0.0f }, { 0.85f, 1.0f }, { 0.95f, 0.02f } });
        case ShapePreset::RampUp: return make({ { 0.0f, 1.0f }, { 0.85f, 0.0f }, { 0.95f, 0.98f } });
        case ShapePreset::Square: return make({ { 0.0f, 0.0f }, { 0.07f, 1.0f }, { 0.5f, 1.0f }, { 0.57f, 0.0f } });
        case ShapePreset::Shimmy:
            return make({ { 0.0f, 0.0f }, { 0.18f, 0.75f }, { 0.34f, 0.3f }, { 0.55f, 1.0f }, { 0.78f, 0.12f } });
    }
    return make({ { 0.0f, 0.0f }, { 0.5f, 1.0f } });
}

} // namespace sway
