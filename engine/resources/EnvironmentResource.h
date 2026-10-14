#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
// CPU-only payload; D3D resources are exclusively owned by the backend.
struct EnvironmentCube
{
    unsigned resolution = 0, mipCount = 1;
    // D3D subresource order: face*mipCount+mip, RGBA linear float pixels.
    std::vector<std::vector<float>> subresources;
};
struct EnvironmentResource
{
    std::string sourcePath;
    unsigned width = 0, height = 0;
    std::vector<float> radiance;
    EnvironmentCube environment, irradiance, prefiltered;
    EnvironmentCube sky; // Source-radiance cube; independent of IBL convolution resolution.
    std::shared_ptr<const std::vector<float>> brdfLut;
    unsigned lutResolution = 64;
    double preprocessMs = 0;
    bool placeholder = true;
};
