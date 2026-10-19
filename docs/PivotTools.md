# Pivot Tools

В Placement включите **Edit Pivot** для одной Entity. Move Gizmo перемещает локальный offset; геометрия и дочерние объекты остаются на месте. Alt не создаёт копию в этом режиме. Отпускание ЛКМ подтверждает один шаг Undo/Redo; Escape отменяет жест. Local/World в существующей панели задаёт ориентацию осей. Для Rotate/Scale выключите Edit Pivot. При нескольких выделенных объектах режим показывает подсказку и допускает отключение.

**Reset Pivot** возвращает нулевой локальный offset без перемещения геометрии, сброса Rotation или Scale. **Pivot to Bounds Center** использует центр bounds уже загруженного Mesh; для пустого объекта доступно ручное редактирование Pivot. Поле Pivot Offset также доступно в Inspector/Bulk Inspector и всегда выражено в локальных координатах геометрии.

## Математика и совместимость

Используется M = T(position) T(pivot) R S T(-pivot). Локальное положение точки вращения в пространстве Parent равно position + pivot. При редактировании offset Position компенсируется: p_new = p_old + pivot_old - RS*pivot_old - pivot_new + RS*pivot_new. Матрица геометрии сохраняется.

RenderPose хранит каноническую трансляцию матрицы геометрии, без повторного применения Pivot. World Transform после декомпозиции сохраняет локальный offset Entity и перебазирует Position. Reparent Keep World сохраняет геометрию и offset; Keep Local сохраняет локальный Transform. Старое ограничение на shear остаётся.

Pivot сохраняется необязательным полем pivot в Transform существующих Scene/Prefab. Нулевое поле не записывается; версии форматов и .whispproject не меняются. Prefab Editor сохраняет offset, Apply/Revert/Update Instances применяют его с сохранением матрицы размещённого корня. Vertex Snap использует полную матрицу; во время Edit Pivot он временно приостановлен. Placement, Focus, Box Selection и Drop to Floor используют общий путь матриц.

## Ограничения

Ненулевой Pivot не поддерживается непосредственно на динамическом Rigidbody с simulatePhysics=true. Используйте статическое тело, отключённую симуляцию либо визуального Child под динамическим телом. Ограничение диагностируется при редактировании/загрузке. Изменён только адаптер поз статических коллайдеров; алгоритмы решателя ЛР №3 не менялись. Существующие соглашения Collider offset/halfExtents сохраняются.

В Play Edit Pivot отключён. Lua set_local_rotation/set_rotation и Scale используют новую точку через общий Transform API. Исходные Mesh и GPU vertex buffers не изменяются.

## Файлы P2.2.2

- engine/ecs/components/TransformComponent.h; engine/ecs/RenderPose.h; engine/ecs/TransformMath.h; engine/ecs/Hierarchy.cpp; engine/ecs/RenderInterpolation.h; engine/ecs/ComponentRegistry.cpp; engine/ecs/systems/PhysicsSystem.cpp.
- engine/editor/EditorPivot.cpp; engine/editor/EditorLayer.h/.cpp; engine/editor/EditorPlacement.cpp; engine/editor/EditorGroupTransform.cpp; engine/editor/GroupTransform.h; engine/editor/EditorTransactions.cpp; engine/editor/EditorInspector.cpp; engine/editor/EditorBulkInspector.cpp; engine/editor/EditorPrefabs.cpp; engine/editor/EditorVertexSnap.cpp.
- engine/scripting/Prefab.cpp; engine/scene/SceneSerializer.cpp; engine/core/ConfigLoader.h/.cpp; engine/core/Application.cpp; engine/CMakeLists.txt.
- tests/HierarchyRegression.cpp; tests/MultiSelectionRegression.cpp; tests/PlacementRegression.cpp; tests/PrefabPlacementRegression.cpp; docs/PivotTools.md.
