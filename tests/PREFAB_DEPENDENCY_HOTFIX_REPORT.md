# Prefab dependency audit hotfix

Рабочее дерево `scripting`. Commit/push/reset/clean не выполнялись. Изменения ограничены Asset Browser dependency audit, подтверждением файловых операций и профильными тестами; LuaRuntime/Physics/rendering не изменялись.

## Первопричина

`AssetCatalog::Dependencies` искал `spawn_prefab` простым substring в любом `.lua`. Если имя проверяемого prefab не встречалось в тексте этого файла, добавлялась строка `dynamic Lua prefab dependency cannot be proven safe` в тот же vector, что и настоящие зависимости. `EditorLayer::AssetFileBlockers` и нижнеуровневый `RequireNoDependencies` считали любую такую строку безусловным запретом.

Поэтому `World.spawn_prefab(self.enemy_prefab, ...)` из wave_controller.lua блокировал **каждый** новый несвязанный prefab, хотя статический default enemy_prefab указывает на wave_enemy.prefab.json. Подстрочный поиск также учитывал комментарии и не различал статический literal spawn другого asset и неизвестный expression.

## Разделение результатов

`AssetCatalog::AuditDependencies` возвращает:

- `confirmed`: точные ссылки на проверяемый key из поддерживаемых источников.
- `dynamic`: обнаруженный prefab spawn/alias с неизвестным runtime argument; это предупреждение о риске, не утверждение, что конкретный asset используется.
- `incomplete`: ошибки чтения/JSON/Lua lexical audit, symlink/path security и прочие незавершённые проверки. Они остаются hard blockers.

`Dependencies` сохранён как совместимый список hard blockers (confirmed + incomplete). Файловые операции отдельно проверяют dynamic и требуют `acceptDynamicRisk=true`, по умолчанию false. Обычный `confirmed=true` команды EditorLayer сам по себе не принимает динамический риск.

Поддерживаемые источники подтверждённых ссылок:

- Lua string literals, включая defaults `fields.*.default`, обычные literal assignments и прямые `World.spawn_prefab("key", ...)`.
- Постоянная конкатенация строк, long bracket strings, стандартные escapes, в том числе decimal/hex/Unicode. Комментарии пропускаются; строка для Log с фрагментом имени не считается совпадением полного asset key.
- Scene/prefab/material/import JSON и прежние поддерживаемые document references, включая `LuaScript.properties.enemy_prefab` overrides.
- Текущая несохранённая сцена, Scene Settings, Undo/Redo и pending transaction — через существующий AssetReferenced.
- По-прежнему проверяются и active Asset Root, и authoring/source Asset Root. Относительные document references и texture subresource handling сохранены.

Lua не исполняется при аудите. Небольшой lexer анализирует данные/аргументы вызовов и учитывает `.spawn_prefab`, `World['spawn_prefab']` и использование API через alias. Literal spawn конкретного другого prefab не создаёт динамического риска. `self.enemy_prefab` остаётся динамическим expression: default и все сохранённые overrides проверяются как известные значения, но script может изменить поле или вычислить новый путь в runtime. Это основание для предупреждения и отдельного согласия, а не для глобального запрета.

Это не полный Lua AST/data-flow анализ: произвольная рефлексия/обфускация имён API не доказывается безопасной. Известные literal values защищаются консервативно даже в недостижимом коде. Автоматического переписывания произвольного Lua нет.

## Rename/Delete UI

В обоих модальных окнах показаны отдельно `Blocked` и `Potential dynamic Lua dependencies`. Для второго списка добавлен checkbox `I accept the risk of unknown dynamic Lua paths`, который изначально выключен и сбрасывается на каждую новую операцию. Enter и кнопка операции не обходят checkbox. В предупреждении прямо сказано, что неизвестные вычисляемые пути не будут автоматически обновлены Rename.

Confirmed/incomplete по-прежнему запрещают операцию даже с отмеченным checkbox. При появлении новой зависимости после открытия окна повторный аудит блокирует действие; после ошибки список warnings/blockers обновляется без полного сканирования проекта каждый ImGui frame.

Для Rename выбран прежний безопасный контракт: asset с известными ссылками переименовать нельзя. Поэтому известные ссылки остаются согласованными — файл и ссылки не меняются. Rename с принятым динамическим риском разрешён только при отсутствии известных ссылок/ошибок аудита; неизвестные Lua expressions остаются без изменений. Массовое переписывание source/staged документов не добавлено.

Проверки EditablePath, canonical containment/symlink escape, built-in/read-only/generated resources, folder/suffix, overwrite protection, fingerprint файла при подтверждении, active-root-only mutation и hidden source fallbacks сохранены. Риск-согласие не обходит ни одну из них. Wave controller и прочие пользовательские файлы не удаляются и не редактируются.

## Регрессии

Расширен AssetFileOperationsRegression:

- Используется реальный текст wave_controller.lua в изолированных active/source fixtures. Новый entity_entity-*.prefab.json получает warnings без hard blockers; default wave_enemy остаётся confirmed и не удаляется/не переименовывается даже при принятом риске.
- Rename/Delete требуют отдельного согласия; после согласия меняется только active copy, source-копия сохраняется. Текст wave_controller.lua проверяется на неизменность.
- Защищены scene/prefab/source overrides, несохранённый override и Redo history.
- Проверены static spawn другого prefab, literal concatenation, comments, long/escaped literals, динамический alias и table-index call.
- Incomplete JSON и Lua audit невозможно обойти risk flag; Lua error указывает имя файла.
- Сохранены прежние тесты built-ins, locked/read-only, Unicode, overwrite, traversal, symlink escape, source/staged, late dependency и changed-file confirmation.
- Native ImGui test: обычная Delete не обходит risk checkbox; его явное включение разрешает Delete несвязанного prefab; следующая операция снова требует согласия. DX12 validation: 0 ERROR/CORRUPTION.

Release-сборка всех targets — PASS (`out/prefab-dependency-release-last.log`, exit 0). `git diff --check` — PASS.

Финальный профиль: **10/10 PASS**, 141,83 s (`out/prefab-dependency-profile.log`):

- AssetFileOperationsRegression / AssetFileOperationsDx12Regression.
- EditorWorkflowRegression / EditorWorkflowDx12Regression.
- EditorUsabilityRegression / EditorUsabilityDx12Regression.
- LuaGameplayApiRegression / LuaGameplayApiDx12Regression.
- LuaGameplayControllerRegression.
- LuaScriptingRegression (включая Wave Survival/prefab).

DX12 validation в Asset Browser native test — 0 ошибок. Полный CTest не запускался: изменения ограничены editor dependency audit и UI файловых операций. Native тест проверяет динамический Delete checkbox; Rename risk forwarding/защита известных ссылок проверены CPU-командами, обычный F2/Enter Rename — прежним native сценарием. Ручная проверка модальных окон Rename/Delete на пользовательском layout/DPI остаётся полезной.

## Изменённые файлы

- `engine/editor/AssetCatalog.cpp/.h`: классификация audit и повторная проверка в файловых операциях.
- `engine/editor/LuaAssetDependencies.h`: lexer/constant literal dependency analysis.
- `engine/editor/EditorAssets.cpp`, `EditorLayer.h`: warning/checkbox и передача отдельного согласия.
- `tests/AssetFileOperationsRegression.cpp`: CPU/native regression coverage.
- Этот отчёт.
