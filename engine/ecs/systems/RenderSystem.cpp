#include "RenderSystem.h"

#include "../World.h"
#include "../components/MeshRendererComponent.h"
#include "../components/TransformComponent.h"
#include "../../render/IRenderAdapter.h"

#include <cmath>

namespace
{
void BuildMvp(float* out16, const ecs::TransformComponent& transform)
{
    const float c = std::cos(transform.angle);
    const float s = std::sin(transform.angle);

    out16[0] = transform.scale * c; out16[4] = -transform.scale * s; out16[8] = 0.0f; out16[12] = transform.x;
    out16[1] = transform.scale * s; out16[5] =  transform.scale * c; out16[9] = 0.0f; out16[13] = transform.y;
    out16[2] = 0.0f;                out16[6] =  0.0f;                out16[10] = 1.0f; out16[14] = 0.0f;
    out16[3] = 0.0f;                out16[7] =  0.0f;                out16[11] = 0.0f; out16[15] = 1.0f;
}
}

namespace ecs
{
void RenderSystem::Update(World& world, float dt)
{
    (void)world;
    (void)dt;
}

void RenderSystem::Render(World& world, IRenderAdapter& renderer) const
{
    world.ForEach<TransformComponent, MeshRendererComponent>(
        [&](Entity, TransformComponent& transform, MeshRendererComponent& meshRenderer)
        {
            if (!meshRenderer.visible)
                return;

            if (meshRenderer.primitive != PrimitiveType::Triangle)
                return;

            float mvp[16];
            BuildMvp(mvp, transform);
            renderer.SetTestTransform(mvp);
            renderer.DrawTestTriangle();
        });
}
}
