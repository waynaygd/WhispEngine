#pragma once
#include <cstdint>
enum class AntiAliasingMode {Off,FxaaLow,FxaaHigh};
inline bool ValidAntiAliasingMode(AntiAliasingMode mode){return mode==AntiAliasingMode::Off||mode==AntiAliasingMode::FxaaLow||mode==AntiAliasingMode::FxaaHigh;}
inline const char* AntiAliasingName(AntiAliasingMode mode){
    switch(mode){case AntiAliasingMode::Off:return "Off";case AntiAliasingMode::FxaaLow:return "FXAA Low";case AntiAliasingMode::FxaaHigh:return "FXAA High";}
    return "Invalid";
}
struct AntiAliasingDiagnostics {AntiAliasingMode mode=AntiAliasingMode::Off;unsigned effectiveSamples=1;std::uint64_t intermediateBytes=0;};
