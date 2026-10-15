#pragma once
#include <cstdint>
enum class AntiAliasingMode {Off,FxaaLow,FxaaHigh};
struct AntiAliasingDiagnostics {AntiAliasingMode mode=AntiAliasingMode::Off;unsigned effectiveSamples=1;std::uint64_t intermediateBytes=0;};
