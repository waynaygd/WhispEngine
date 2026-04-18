#include "RenderSystem.h"

#include "../World.h"
#include "../components/MeshRendererComponent.h"
#include "../components/TransformComponent.h"
#include "../../render/IRenderAdapter.h"

#include <cmath>

namespace
{
void SetIdentity(float* out16)
{
    for (int i = 0; i < 16; ++i)
        out16[i] = 0.0f;

    out16[0] = 1.0f;
    out16[5] = 1.0f;
    out16[10] = 1.0f;
    out16[15] = 1.0f;
}

void MultiplyMatrix(const float* lhs, const float* rhs, float* out16)
{
    float result[16]{};

    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            for (int k = 0; k < 4; ++k)
                result[col * 4 + row] += lhs[k * 4 + row] * rhs[col * 4 + k];
        }
    }

    for (int i = 0; i < 16; ++i)
        out16[i] = result[i];
}

void BuildScaleMatrix(const ecs::Vec3& scale, float* out16)
{
    SetIdentity(out16);
    out16[0] = scale.x;
    out16[5] = scale.y;
    out16[10] = scale.z;
}

void BuildRotationX(float angle, float* out16)
{
    SetIdentity(out16);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    out16[5] = c;
    out16[6] = s;
    out16[9] = -s;
    out16[10] = c;
}

void BuildRotationY(float angle, float* out16)
{
    SetIdentity(out16);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    out16[0] = c;
    out16[2] = -s;
    out16[8] = s;
    out16[10] = c;
}

void BuildRotationZ(float angle, float* out16)
{
    SetIdentity(out16);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    out16[0] = c;
    out16[1] = s;
    out16[4] = -s;
    out16[5] = c;
}

void BuildTranslationMatrix(const ecs::Vec3& position, float* out16)
{
    SetIdentity(out16);
    out16[12] = position.x;
    out16[13] = position.y;
    out16[14] = position.z;
}

void BuildMvp(float* out16, const ecs::TransformComponent& transform)
{
    float scaleMatrix[16];
    float rotateX[16];
    float rotateY[16];
    float rotateZ[16];
    float translation[16];
    float temp0[16];
    float temp1[16];
    float temp2[16];

    BuildScaleMatrix(transform.scale, scaleMatrix);
    BuildRotationX(transform.rotation.x, rotateX);
    BuildRotationY(transform.rotation.y, rotateY);
    BuildRotationZ(transform.rotation.z, rotateZ);
    BuildTranslationMatrix(transform.position, translation);

    MultiplyMatrix(rotateX, scaleMatrix, temp0);
    MultiplyMatrix(rotateY, temp0, temp1);
    MultiplyMatrix(rotateZ, temp1, temp2);
    MultiplyMatrix(translation, temp2, out16);
}
}

namespace ecs
{
void RenderSystem::Update(World& world, float dt)
{
    (void)dt;
    if (m_Renderer == nullptr)
        return;

    world.ForEach<TransformComponent, MeshRendererComponent>(
        [&](Entity, TransformComponent& transform, MeshRendererComponent& meshRenderer)
        {
            if (!meshRenderer.visible)
                return;

            float mvp[16];
            BuildMvp(mvp, transform);
            m_Renderer->SetTestTransform(mvp);
            m_Renderer->SetTestColor(
                meshRenderer.color.r,
                meshRenderer.color.g,
                meshRenderer.color.b,
                meshRenderer.color.a);

            switch (meshRenderer.primitive)
            {
            case PrimitiveType::Line:
                m_Renderer->DrawTestLine();
                break;
            case PrimitiveType::Triangle:
                m_Renderer->DrawTestTriangle();
                break;
            case PrimitiveType::Quad:
                m_Renderer->DrawTestQuad();
                break;
            case PrimitiveType::Cube:
                m_Renderer->DrawTestCube();
                break;
            default:
                break;
            }
        });
}
}
