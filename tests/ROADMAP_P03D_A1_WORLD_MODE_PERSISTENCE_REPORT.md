# P0.3d-A1 — Scene World Mode Persistence

Дата: 2026-10-18. Ветка `project_system`.
Исходный HEAD: `1d571bb1e4f1121afa3e3a2c9453e550e958000a`.
На входе этапа `git status --short` был пуст: ранее перечисленные в checkpoint
изменения уже входят в текущий baseline. Git index/history этим этапом не изменялись.

## Формат и совместимость

Schema version **4 сохранена**. SaveWorld/CaptureWorld всегда записывают одно
из двух точных строковых значений:

```json
{"version": 4, "worldMode": "rootOnly", "settings": {}, "entities": []}
```

```json
{"version": 4, "worldMode": "hierarchy", "settings": {}, "entities": []}
```

Это примеры формы документа; реальные settings сериализуются прежним кодом.
Отсутствующий `worldMode` означает **RootOnly**, независимо от destination
World и наличия Relationship. Старые flat v4 и legacy array/v1/v2/v3 сохраняют
прежнюю семантику. При явном сохранении старой v4 сцены появляется marker
`rootOnly`; файлы автоматически не мигрируются и не переписываются.
SaveEntityConfigs остаётся legacy schema v3 с однозначной RootOnly семантикой.

Для HierarchyData marker обязателен даже при пустом World или только roots
без Relationship. Он не вычисляется из графа. Старый внутренний hierarchy JSON
без marker теперь диагностируется как RootOnly + недопустимый Relationship;
автоматического распознавания/upgrade нет, как требует задание A1.

Unknown strings, null, boolean, numbers, arrays и objects в worldMode
отклоняются с конкретной ошибкой. Строки чувствительны к регистру.
На legacy schema явный `hierarchy` отклоняется: этот режим требует v4;
legacy decoder не игнорирует неизвестные или повреждённые mode markers.
Версия документа также проверяется как целое 1–4.

## Strict load и атомарность

RestoreWorld/LoadWorld определяют режим до создания candidate/публикации.
Destination должен иметь **точно тот же DataMode** в обоих направлениях.
Например: `worldMode mismatch: scene requires hierarchy, destination World is rootOnly`.
Relationship в явно или неявно RootOnly документе отклоняется ещё на mode
preflight. LoadEntityConfigs также отказывает hierarchy без попытки flatten.

Далее используется прежний staged restore: все stable IDs создаются до
компонентов, затем проверяется graph и только после успеха выполняется
ReplaceData и присваивание SceneSettings. DataMode World остаётся const,
guards ReplaceData не менялись. Переключения/конверсии режимов нет.

При отказе mode preflight или загрузки сохранены destination identity,
handles, компоненты, stable IDs и SceneSettings. Через настоящий Application
проверены сохранность ProjectContext, scene key, selection, Undo count и dirty
state после отказов OpenScene, SetStartupScene и OpenProject.

## Интерфейс для будущего Application A2

Добавлены статические методы SceneSerializer:

```cpp
bool DetermineWorldMode(const nlohmann::json&, ecs::World::DataMode&, std::string* error = nullptr);
bool ReadWorldMode(const std::filesystem::path&, ecs::World::DataMode&, std::string* error = nullptr);
```

DetermineWorldMode проверяет marker/совместимость со schema и запрет Relationship
для RootOnly, не выполняя полный component/graph restore. ReadWorldMode читает
JSON файла и вызывает тот же разбор. Выходной DataMode присваивается только
при успехе; при ошибке остаётся прежним.

A2 сможет определить режим до создания candidate World. Загрузка в любом
случае повторно проверяет совместимость, поэтому предварительное чтение не
позволяет обойти guard. При работе с уже разобранным документом можно вызвать
DetermineWorldMode и RestoreWorld на одном JSON.

В Application API и ownership изменений нет. Editor остаётся RootOnly,
hierarchy document даже без Relationship получает явный отказ. Scene/Project
load paths используют новые проверки через существующий SceneSerializer.
Никакого пользовательского Hierarchy Editor на этом этапе не включено.

## Регрессии

Новый CPU CTest `SceneWorldModeRegression` выполняет **406 checks** через
реальные временные файлы, SaveWorld, LoadWorld, WriteAtomic, legacy configs и
настоящие Application/Project handlers:

- Old v4 missing marker, explicit RootOnly, explicit hierarchy.
- Empty HierarchyData, roots без Relationship, Parent/Child/Grandchild и sibling order.
- Обратный порядок entities в JSON, stable IDs, exact local TRS/components/settings
  roundtrip и affine matrix oracle, включая недекомпозируемый world shear.
- Invalid/unknown/wrong-type modes, RootOnly + Relationship, missing marker +
  Relationship, несовпадение режимов, malformed JSON и отсутствующий файл.
- Rollback всего destination и settings; preflight output rollback.
- Legacy array и schema v1/v2/v3, legacy flatten guard даже для пустой hierarchy.
- RootOnly collider с nonunit scale и nonzero offset: raycast сохраняет прежнюю
  дистанцию 10.5, dynamic Rigidbody продолжает симулироваться без hierarchy
  ограничений, authored scale/halfExtents/offset сохраняются.
- Старый и explicit RootOnly Project Startup, отказ hierarchy/malformed
  startup и сохранность descriptor/ProjectContext/Editor selection/history.
- Application Save сохраняет RootOnly marker.

Существующие regression tests/assertions не изменялись и не ослаблялись.
Lua/rendering/physics contracts и ComponentRegistry не менялись.

## Результаты верификации

- Release build: **PASS** (`out/p03da1-release-build.log`).
  Существующие CMake/dependency/compiler warnings не подавлялись.
- Новый SceneWorldModeRegression: **1/1 PASS**, 406 checks, 0.85 s
  (`out/p03da1-world-mode-ctest.log`, `out/p03da1-world-mode-detail.log`).
- Profile CTest: **14/14 PASS**, 38.36 s
  (`out/p03da1-profile-ctest.log`, `out/p03da1-profile-detail.log`).
  ECS Hierarchy 261, Runtime CPU 296 / DX12 16, Transform Contract 718 checks;
  также SceneWorldMode, Physics, LuaScripting, Editor2 CPU/DX12 и
  Project Core/System/Settings CPU/DX12.
- Full CTest: **60/61 PASS, 1 FAILED**, 265.11 s, exit code 8
  (`out/p03da1-full-ctest.log`, `out/p03da1-full-detail.log`).
  Упал `LuaGameplayControllerDx12Regression` на assertion
  **`fullscreen mouse picking selects Entity`**. Это фактический результат
  полного прогона; полного повторного запуска и rerun-until-pass не было.
- Одна изолированная диагностическая проверка того же CTest также **FAILED**,
  15.73 s, на том же assertion
  (`out/p03da1-native-picking-diagnostic-ctest.log`,
  `out/p03da1-native-picking-diagnostic-detail.log`).
- Для проверки связи с A1 собран отдельный executable в out/build/x64-release:
  реализации SceneSerializer взяты из исходного HEAD, с прежней сериализацией
  без worldMode; остальные objects/libraries и неизменённый native test те же.
  Production sources, CTest executable и assertions при этом не менялись.
  Этот диагностический executable тоже **FAILED** на том же fullscreen picking
  assertion (`out/p03da1-baseline-diagnostic-build.log`,
  `out/p03da1-baseline-native-picking.log`). Это не отдельный полный baseline
  CTest. Сбой воспроизводится без новых реализаций SceneSerializer; причина
  native picking не установлена, потеря фокуса Windows не объявляется доказанной.
  Связанный код `NativeViewportDisplay` создаёт сцену напрямую после NewScene,
  без загрузки сцены в участке F11/picking. Никаких исправлений этого отдельного
  workflow в рамках A1 не выполнялось.
- DX12 validation: профильный RuntimeHierarchyDx12Regression с assertion
  GetValidationErrorCount()==0 прошёл. D3D12 ERROR/CORRUPTION markers в profile,
  full и диагностических логах — **0**. Это не отменяет native picking failure.
- `git diff --check`: **PASS**; whitespace-проверка обоих новых файлов также PASS.
- Снимок на входе содержит **822** tracked/ранее untracked файла
  (`out/p03da1-start-hashes.json`). Из них изменены только три заявленных файла:
  CMakeLists.txt и SceneSerializer.cpp/.h. Все остальные прежние файлы сохранены.
  SHA256 всех **89 assets + EventBus (90/90)** совпадает со снимком до этапа.

## Изменённые файлы

- `engine/scene/SceneSerializer.h` — mode preflight API.
- `engine/scene/SceneSerializer.cpp` — explicit capture, strict parse и staged mismatch guards.
- `tests/SceneWorldModeRegression.cpp` — новые file/physics/Application regressions.
- `CMakeLists.txt` — регистрация одного нового CPU CTest.
- Этот отчёт.

Application ownership, World guards, physics collider semantics, пользовательские
Lua/сцены/модели/assets и EventBus не редактировались. Test fixtures принадлежат
уникальному временному каталогу под build directory и очищаются RAII.
Git commit/push/merge/rebase/reset/clean не выполнялись.

## Граница этапа

Исправляется только persisted World DataMode. Collider migration/совместимость
upgrade и Application World ownership остаются отдельными блокерами A2.
Новые Tree UI, Reparent/Unparent, Gizmo, subtree duplication и interpolation
не реализованы. Публичная hierarchy остаётся закрыта; ручной UI-сеанс для неё
не проводился. К A2 и другим пунктам Roadmap этот этап не переходит.

**Verdict: INCOMPLETE по финальной верификации.** Persisted mode и строгие
Save/Load pathways реализованы, новый regression (406 checks) и профиль
(14/14) зелёные. Однако полный CTest не зелёный (60/61), поэтому общий READY
не заявляется. Native fullscreen picking failure воспроизводится и с исходным
SceneSerializer из HEAD; assertions не ослаблялись, результат не скрыт.
Application ownership и Collider upgrade semantics остаются за пределами A1;
к A2 перехода нет.
