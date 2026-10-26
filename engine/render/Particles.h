#pragma once
#include "RenderResourceHandles.h"
#include <vector>
#include <array>
#include <cstddef>
struct ParticleVertex {std::array<float,4> position;std::array<float,2> uv;std::array<float,4> color;};
struct ParticleCommand {unsigned first=0,count=0;RenderTextureHandle texture;bool additive=false;};
struct ParticleDrawList {std::vector<ParticleVertex> vertices;std::vector<ParticleCommand> commands;};
struct ParticleGpuStatistics {double gpuMs=0;std::size_t drawCalls=0,bufferGrowths=0,uploadBytes=0;bool timingValid=false;};
