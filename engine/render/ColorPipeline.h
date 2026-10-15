#pragma once
#include <cmath>
#include <algorithm>
// Display/UI tint and base-color texture bytes are sRGB. Material factors and
// radiance are linear. Alpha is never transferred. UNORM output is encoded once.
inline float SrgbToLinear(float x) {
    x=std::isfinite(x)?std::clamp(x,0.f,1.f):0.f;
    return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);
}
inline float LinearToSrgb(float x) {
    x=std::isfinite(x)?std::clamp(x,0.f,1.f):0.f;
    return x<=.0031308f?12.92f*x:1.055f*std::pow(x,1.f/2.4f)-.055f;
}
