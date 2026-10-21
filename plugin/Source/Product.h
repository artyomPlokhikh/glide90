#pragma once

// Which plugin this build is. One source tree makes two:
//   GLIDE90       GLIDE, REVERSE GATE and HOLD, with the ROUTING between them;
//   GLIDE90 LITE  GLIDE and REVERSE GATE, swapped by dragging a module's title bar, MIX on the gate.
// The build sets GLIDE90_PRODUCT (0, 1); without it, GLIDE90.
#ifndef GLIDE90_PRODUCT
    #define GLIDE90_PRODUCT 0
#endif

namespace glide90
{

enum class Product
{
    Full,
    Lite
};

inline constexpr Product kProduct = (Product) GLIDE90_PRODUCT;
inline constexpr bool kLite = kProduct == Product::Lite;
// HOLD and the ROUTING selector; LITE orders its two modules by dragging instead.
inline constexpr bool kHasHold = !kLite;

} // namespace glide90
