#pragma once

#include <array>
#include <string_view>

namespace sway
{

struct ShapePoint
{
    float x; // position in the cycle, 0 - 1
    float y; // how far the bar is pushed, 0 (rest) - 1 (full depth)
};

// One cycle of bar movement: a smooth periodic curve through up to kMaxPoints points (monotone
// cubic, so it never overshoots between them). Plain data: cheap to copy to the audio thread.
class Shape
{
public:
    static constexpr int kMaxPoints = 32;

    Shape();

    // Sorts by x, clamps to the unit square, drops extra points and precomputes the tangents.
    void setPoints(const ShapePoint* points, int count);

    float at(float phase) const;

    int size() const { return count; }
    const ShapePoint& point(int i) const { return pts[(size_t) i]; }

private:
    std::array<ShapePoint, kMaxPoints> pts {};
    std::array<float, kMaxPoints> tangents {};
    int count = 0;
};

enum class ShapePreset
{
    Sine,
    Triangle,
    Dive,
    Swell,
    RampDown,
    RampUp,
    Square,
    Shimmy,
};

inline constexpr std::array<std::string_view, 8> kShapePresetNames { "Sine", "Triangle", "Dive", "Swell",
                                                                    "Ramp Down", "Ramp Up", "Square", "Shimmy" };

Shape makeShape(ShapePreset preset);

} // namespace sway
