#pragma once
#include "../EnvironmentResource.h"
#include "../ResourceLoader.h"
#include <filesystem>
namespace IblMath
{
using V = std::array<double, 3>;
V CubeDirection(unsigned face, double u, double v);
std::array<double, 2> PanoramaUv(V direction);
V SamplePanorama(const EnvironmentResource &, V direction);
std::array<double, 2> IntegrateBrdf(double noV, double roughness, unsigned samples = 512);
EnvironmentResource Preprocess(EnvironmentResource source, unsigned cubeSize = 64,
                               unsigned irradianceSize = 16, unsigned samples = 256);
} // namespace IblMath
class EnvironmentLoader
{
  public:
    static EnvironmentResource CreateDefault() { return {}; }
    static ResourceLoadResult<EnvironmentResource> Load(const std::string &,
                                                        const std::filesystem::path &);
};
