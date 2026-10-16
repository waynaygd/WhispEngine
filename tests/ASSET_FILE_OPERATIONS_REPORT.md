# Asset Browser: Rename / Delete

Ветка `scripting`. Агент не выполнял commit, push, merge, rebase или reset. Появившиеся в ходе работы сторонние коммиты сохранены.

## Поведение

- F2 и ПКМ → Rename открывают один компактный диалог. Основа имени выделяется автоматически; полный суффикс показан отдельно и сохраняется. Enter подтверждает, Escape отменяет.
- Delete и ПКМ → Delete открывают `Delete Asset?`, с полным путём, кнопками Delete/Cancel и предупреждением для staged-копии. Cancel выбран по умолчанию. Само нажатие Delete файл не удаляет.
- Файлы предыдущих сессий доступны без регистрации в `CreatedHere`.
- Существующие файлы не перезаписываются. Пустые имена, traversal, разделители, запрещённые Windows-символы, зарезервированные имена и завершающие точка/пробел отклоняются. Поддержаны Unicode и кириллица.
- После успеха обновляется каталог, освобождается GPU preview, очищаются metadata и старые resource cache entries. Rename сохраняет выделение на новом ключе, Delete очищает его. Material Editor переключается на новый путь или освобождает удалённый.
- Файл повторно проверяется при подтверждении: путь, размер, время изменения, доступность, ссылки. Ошибка не очищает selection и не считается успехом.
- Горячие клавиши принимаются только при фокусе Asset Browser, вне text input, popup/modal, drag-and-drop и Play. Delete в Hierarchy сохраняет существующее удаление Entity.

## Политика по типам

| Тип | Политика |
|---|---|
| Lua в `scripts/` | Rename/Delete неиспользуемых файлов, `.lua` сохраняется |
| Prefab в `prefabs/` | JSON Prefab, полный `.prefab.json`; ссылки и непроверяемые динамические Lua-зависимости блокируют изменение |
| Material в `materials/` | Неиспользуемые JSON/material assets, включая прошлые сессии; `.material.json` сохраняется целиком |
| Standalone textures в `textures/` | Разрешены при успешном аудите; opaque FBX/GLB и непроверяемые encoded glTF зависимости блокируют операцию |
| HDR в `environments/` | Разрешены неиспользуемые пользовательские HDR без связанного `.sun.json` |
| Models / imported scenes | Консервативно заблокированы: пакетные зависимости требуют owning tool |
| Shaders, binaries, generated caches, directories, вне-root файлы | Заблокированы |
| Известные встроенные/default/diagnostic assets, read-only/system/reparse files | Заблокированы |

Case-only rename не выполняется поверх существующего Windows-имени; используйте промежуточное имя.

## Ссылки и Asset Root

Слепой замены строк и автоматического переписывания сцен нет. Используемый ассет блокируется с диагностикой: текущие Entity/компоненты, Scene Settings, Undo/Redo, pending transaction, editor sky preferences, JSON-сцены, Prefab, Material, glTF/WScene и текстовые Lua/OBJ/MTL зависимости. Аудит читает активный и исходный Project Asset Root, но изменяет только активный файл. Неполный аудит — ошибка, а не разрешение.

Для динамических Lua prefab paths и opaque model dependencies применяется консервативная блокировка. Это может блокировать и фактически неиспользуемый ассет: доказательства безопасности недостаточно. Неразбираемый JSON или непроверяемые links также блокируют операцию с причиной.

Пути проходят через AssetPaths, AssetImport::SafeTarget, canonical containment, SafeAsset для Lua и SafeMaterialPath для материалов. Windows использует wide APIs; Rename не устанавливает REPLACE_EXISTING. Проверяются права на удаление/перемещение.

Source и staged не переименовываются/удаляются одновременно. Если изменена staged-копия, source остаётся. Старый source fallback скрывается из каталога текущей сессии, чтобы старое имя не выглядело пережившим операцию. Пересборка может восстановить staged-файл; после перезапуска source-only запись также может снова быть видна, но файловая операция не использует source fallback.

Небольшие исправления UTF-8 на границах AssetPaths/ResourceManager/MaterialLoader нужны для загрузки переименованных Unicode-ассетов; сериализуемые схемы не менялись. LuaRuntime, ECS, SceneSerializer, ScriptEditor, RenderSystem, Physics и Job System не изменены.

## Проверки

- Release build: PASS (`out/asset-files-build-review.log`).
- Новые CPU tests: **70 checks PASS**; DX12/ImGui integration: **96 checks PASS**.
- Проверены F2, Rename через контекстное меню, auto-selection имени, Enter/Escape, Delete/Cancel/подтверждение, Delete при фокусе Assets/Hierarchy, text input, Play, Unicode material loading, ссылки, конфликты, изменение файла и новая зависимость после открытия диалога.
- Symlink creation test: SKIP — Windows не предоставила privilege создания symlink. Production-проверки symlink/reparse/canonical containment включены; assertion не отключён.
- Соседний `EditorUsabilityDx12Regression` один раз упал на существующем mesh picking assertion; отдельный повтор с тем же diagnostic layout прошёл. Старые assertions не изменены.
- Полный CTest: **34/34 PASS**, 244.63 секунды (`out/asset-files-ctest.log`), включая Lua, Editor, PBR/Lighting/IBL, материалы и новые файловые проверки.
- После последней защиты encoded glTF выполнены пересборка текущего дерева и целевой повтор: **3/3 PASS**, 38.85 секунды (`out/asset-files-review.log`). Lua — **193 checks PASS**. `git diff --check` — PASS.
- Нативные OS mouse clicks и запуск Visual Studio в этом мини-патче повторно не выполнялись. GUI-проверки используют synthetic ImGui input с настоящим DX12 renderer. Open Script/Hot Reload сохраняют прежние реализации.

## Изменённые файлы

- `CMakeLists.txt`
- `engine/core/AssetPaths.cpp`
- `engine/editor/AssetCatalog.cpp/.h`
- `engine/editor/EditorAssets.cpp`
- `engine/editor/EditorAssetPreview.cpp`
- `engine/editor/EditorLayer.cpp/.h`
- `engine/resources/ResourceManager.h`
- `engine/resources/loaders/MaterialLoader.cpp`
- `tests/AssetFileOperationsRegression.cpp`
- Этот отчёт.

## Короткая ручная проверка

1. В Edit создайте тестовый Lua, выделите его в Asset Browser. F2 → новое имя → Enter: суффикс `.lua` сохранён, новый файл выделен. Проверьте то же через ПКМ → Rename.
2. Delete → Cancel/Escape: файл и selection остаются. Delete → Delete: неиспользуемый файл исчезает, preview/selection очищаются.
3. Назначьте другой тестовый Lua Entity и сохраните сцену/Prefab. Rename/Delete должны показывать зависимости и блокировать изменение.
4. С выбранными Entity и ассетом переключайте фокус между Hierarchy и Asset Browser: Delete действует только на соответствующую область. Повторите во время text input и Play — файловых операций быть не должно.
5. Проверьте кириллическое имя, конфликт имён, повторное открытие файла после перезапуска редактора, double-click → Visual Studio и обычный Reload Script после редактирования.
