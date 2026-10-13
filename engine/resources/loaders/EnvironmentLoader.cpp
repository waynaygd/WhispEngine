#include "EnvironmentLoader.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <stb_image.h>
#include <tracy/Tracy.hpp>
namespace IblMath
{
constexpr double Pi = 3.14159265358979323846;
V Add(V a, V b)
{
    for (int k = 0; k < 3; ++k)
        a[k] += b[k];
    return a;
}
V Mul(V a, double s)
{
    for (auto &x : a)
        x *= s;
    return a;
}
double Dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V Cross(V a, V b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
V Unit(V a)
{
    double m = std::max({std::abs(a[0]), std::abs(a[1]), std::abs(a[2])});
    if (!std::isfinite(m) || m == 0)
        return {0, 0, 1};
    a = Mul(a, 1 / m);
    return Mul(a, 1 / std::sqrt(Dot(a, a)));
}
V CubeDirection(unsigned face, double u, double v)
{
    switch (face)
    {
    case 0:
        return Unit({1, -v, -u});
    case 1:
        return Unit({-1, -v, u});
    case 2:
        return Unit({u, 1, v});
    case 3:
        return Unit({u, -1, -v});
    case 4:
        return Unit({u, -v, 1});
    default:
        return Unit({-u, -v, -1});
    }
}
std::array<double, 2> PanoramaUv(V d)
{
    d = Unit(d);
    return {std::atan2(d[2], d[0]) / (2 * Pi) + .5, std::acos(std::clamp(d[1], -1., 1.)) / Pi};
}
V SamplePanorama(const EnvironmentResource &e, V d)
{
    if (!e.width || !e.height || e.radiance.size() != std::size_t(e.width) * e.height * 4)
        return {};
    auto uv = PanoramaUv(d);
    double x = uv[0] * e.width - .5, y = uv[1] * e.height - .5;
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    double fx = x - ix, fy = y - iy;
    V out{};
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i)
        {
            int xx = ((ix + i) % int(e.width) + int(e.width)) % int(e.width),
                yy = std::clamp(iy + j, 0, int(e.height) - 1);
            double w = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
            for (int k = 0; k < 3; ++k)
                out[k] += e.radiance[(std::size_t(yy) * e.width + xx) * 4 + k] * w;
        }
    return out;
}
double Radical(unsigned n)
{
    n = (n << 16) | (n >> 16);
    n = ((n & 0x55555555) << 1) | ((n & 0xaaaaaaaa) >> 1);
    n = ((n & 0x33333333) << 2) | ((n & 0xcccccccc) >> 2);
    n = ((n & 0x0f0f0f0f) << 4) | ((n & 0xf0f0f0f0) >> 4);
    n = ((n & 0x00ff00ff) << 8) | ((n & 0xff00ff00) >> 8);
    return n * 2.3283064365386963e-10;
}
V Basis(V n, V h)
{
    V t = Unit(Cross(std::abs(n[2]) < .999 ? V{0, 0, 1} : V{1, 0, 0}, n)), b = Cross(n, t);
    return Add(Add(Mul(t, h[0]), Mul(b, h[1])), Mul(n, h[2]));
}
V Half(unsigned i, unsigned count, double r, V n)
{
    double a = r * r, phi = 2 * Pi * double(i) / count, y = Radical(i),
           z = std::sqrt((1 - y) / (1 + (a * a - 1) * y)), s = std::sqrt(std::max(0., 1 - z * z));
    return Basis(n, {s * std::cos(phi), s * std::sin(phi), z});
}
double G1(double n, double a2)
{
    return 2 * n / std::max(n + std::sqrt(a2 + (1 - a2) * n * n), 1e-12);
}
std::array<double, 2> IntegrateBrdf(double nv, double rough, unsigned count)
{
    if (!count || count > 65536)
        throw std::runtime_error("Invalid BRDF integration sample count");
    nv = std::isfinite(nv) ? std::clamp(nv, 1e-5, 1.) : 1;
    rough = std::isfinite(rough) ? rough : .5;
    double r = std::clamp(rough, .04, 1.), a2 = r * r * r * r;
    V n{0, 0, 1}, v{std::sqrt(1 - nv * nv), 0, nv};
    std::array<double, 2> sum{};
    for (unsigned i = 0; i < count; ++i)
    {
        V h = Half(i, count, r, n), l = Add(Mul(h, 2 * Dot(v, h)), Mul(v, -1));
        double nl = std::max(l[2], 0.), nh = std::max(h[2], 0.), vh = std::max(Dot(v, h), 0.);
        if (nl <= 0 || nh <= 0)
            continue;
        double g = G1(nv, a2) * G1(nl, a2) * vh / (nh * nv), f = std::pow(1 - vh, 5);
        sum[0] += (1 - f) * g;
        sum[1] += f * g;
    }
    for (auto &x : sum)
        x /= count;
    return sum;
}
EnvironmentResource Preprocess(EnvironmentResource e, unsigned size, unsigned irr, unsigned count)
{
    ZoneScopedN("IblPreprocessCPU");
    if (!size || size > 256 || (size & (size - 1)) || !irr || irr > 64 || count < 16 ||
        count > 8192 || !e.width || !e.height ||
        e.radiance.size() != std::size_t(e.width) * e.height * 4)
        throw std::runtime_error("Invalid environment preprocessing dimensions");
    for (float v : e.radiance)
        if (!std::isfinite(v) || v < 0)
            throw std::runtime_error("Invalid environment radiance");
    auto start = std::chrono::steady_clock::now();
    e.environment = {size, 1, {}};
    e.irradiance = {irr, 1, {}};
    unsigned mips = 1;
    for (unsigned s = size; s > 1; s /= 2)
        ++mips;
    e.prefiltered = {size, mips, {}};
    for (unsigned face = 0; face < 6; ++face)
    {
        std::vector<float> env(size * size * 4);
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x)
            {
                auto v = SamplePanorama(
                    e, CubeDirection(face, (2. * x + 1) / size - 1, (2. * y + 1) / size - 1));
                for (int k = 0; k < 3; ++k)
                    env[(y * size + x) * 4 + k] = float(v[k]);
                env[(y * size + x) * 4 + 3] = 1;
            }
        e.environment.subresources.push_back(env);
        std::vector<float> diffuse(irr * irr * 4);
        for (unsigned y = 0; y < irr; ++y)
            for (unsigned x = 0; x < irr; ++x)
            {
                V n = CubeDirection(face, (2. * x + 1) / irr - 1, (2. * y + 1) / irr - 1), sum{};
                for (unsigned i = 0; i < count; ++i)
                {
                    double u = (i + .5) / count, phi = 2 * Pi * Radical(i);
                    V l = Basis(n, {std::sqrt(u) * std::cos(phi), std::sqrt(u) * std::sin(phi),
                                    std::sqrt(1 - u)});
                    sum = Add(sum, SamplePanorama(e, l));
                }
                sum = Mul(sum, 1. / count);
                for (int k = 0; k < 3; ++k)
                    diffuse[(y * irr + x) * 4 + k] = float(sum[k]);
                diffuse[(y * irr + x) * 4 + 3] = 1;
            }
        e.irradiance.subresources.push_back(std::move(diffuse));
        for (unsigned mip = 0, s = size; mip < mips; ++mip, s = std::max(1u, s / 2))
        {
            if (mip == 0)
            {
                e.prefiltered.subresources.push_back(env);
                continue;
            }
            double r = double(mip) / (mips - 1);
            std::vector<float> spec(s * s * 4);
            for (unsigned y = 0; y < s; ++y)
                for (unsigned x = 0; x < s; ++x)
                {
                    V n = CubeDirection(face, (2. * x + 1) / s - 1, (2. * y + 1) / s - 1), sum{};
                    double weight = 0;
                    for (unsigned i = 0; i < count; ++i)
                    {
                        V h = Half(i, count, r, n), l = Add(Mul(h, 2 * Dot(n, h)), Mul(n, -1));
                        double w = std::max(0., Dot(n, l));
                        sum = Add(sum, Mul(SamplePanorama(e, l), w));
                        weight += w;
                    }
                    sum = Mul(sum, 1 / std::max(weight, 1e-12));
                    for (int k = 0; k < 3; ++k)
                        spec[(y * s + x) * 4 + k] = float(sum[k]);
                    spec[(y * s + x) * 4 + 3] = 1;
                }
            e.prefiltered.subresources.push_back(std::move(spec));
        }
    }
    // Irradiance stores E/pi, estimated with cosine-weighted hemisphere samples.
    static std::once_flag once;
    static std::shared_ptr<const std::vector<float>> lut;
    std::call_once(once,
                   []
                   {
                       auto data = std::make_shared<std::vector<float>>(64 * 64 * 2);
                       for (unsigned y = 0; y < 64; ++y)
                           for (unsigned x = 0; x < 64; ++x)
                           {
                               auto ab = IntegrateBrdf((x + .5) / 64., (y + .5) / 64.);
                               (*data)[(y * 64 + x) * 2] = float(ab[0]);
                               (*data)[(y * 64 + x) * 2 + 1] = float(ab[1]);
                           }
                       lut = data;
                   });
    e.brdfLut = lut;
    e.placeholder = false;
    e.preprocessMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return e;
}
} // namespace IblMath
ResourceLoadResult<EnvironmentResource> EnvironmentLoader::Load(const std::string &key,
                                                                const std::filesystem::path &path)
{
    ResourceLoadResult<EnvironmentResource> out;
    out.errorMessage = "Invalid or unsupported Radiance HDR environment";
    try
    {
        if (path.extension() != ".hdr")
            return out;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return out;
        auto bytes = file.tellg();
        if (bytes <= 0 || bytes > 64 * 1024 * 1024)
            return out;
        std::vector<unsigned char> encoded(static_cast<std::size_t>(bytes));
        file.seekg(0);
        file.read(reinterpret_cast<char *>(encoded.data()), encoded.size());
        int w = 0, h = 0, c = 0;
        if (!stbi_is_hdr_from_memory(encoded.data(), int(encoded.size())) ||
            !stbi_info_from_memory(encoded.data(), int(encoded.size()), &w, &h, &c) || w <= 0 ||
            h <= 0 || std::uint64_t(w) * h > 16 * 1024 * 1024)
            return out;
        float *pixels = stbi_loadf_from_memory(encoded.data(), int(encoded.size()), &w, &h, &c, 4);
        if (!pixels)
            return out;
        EnvironmentResource source;
        source.sourcePath = key;
        source.width = w;
        source.height = h;
        source.radiance.assign(pixels, pixels + std::size_t(w) * h * 4);
        stbi_image_free(pixels);
        for (std::size_t i = 0; i < source.radiance.size(); ++i)
        {
            float v = source.radiance[i];
            if (!std::isfinite(v) || v < 0)
            {
                out.errorMessage = "HDR radiance must be finite and nonnegative";
                return out;
            }
        }
        out.data = IblMath::Preprocess(std::move(source));
        out.success = true;
        out.errorMessage.clear();
        return out;
    }
    catch (const std::exception &error)
    {
        out.errorMessage = error.what();
        return out;
    }
}
