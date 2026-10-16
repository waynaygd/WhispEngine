# F11 Viewport Layout Hotfix

## Причина и воспроизведение

`DrawViewport` сначала регистрировал полноэкранный `Image`. ImGui ItemSize устанавливает `CursorMaxPos` по нижней границе Image, а `CursorPos` переводит ниже неё на `ItemSpacing.y`, готовя следующую строку.

Добавленный Create overlay сохранял именно этот post-image CursorPos, перемещал курсор внутрь Image, регистрировал Button, затем вызывал `SetCursorScreenPos(savedCursor)`. После этого следовали draw-list helpers/gizmos без layout item. При `End()` окна `Viewport##Maximized` проверка ImGui `ErrorCheckUsingSetCursorPosToExtendParentBoundaries` обнаруживала `IsSetPos` и CursorPos ниже CursorMaxPos.

Диагностика воспроизведена до исправления в Release/DX12: к существующему UI-тесту подключён ImGui ErrorCallback, ошибка проверяется после Application::Run. Лог `out/f11-layout-repro.log` содержит исходное предупреждение на кадрах 205–207; тест завершается FAILED с точным текстом предупреждения. Ранее этот тест проверял работу F11/Create, но не проверял диагностические сообщения ImGui, поэтому Release мог пройти с error tooltip.

Проверены `EditorLayer::Render`, `DrawViewport`, `DrawViewportCreateMenu` и последующие helper functions. Render сохраняет dock tree через KeepAliveOnly; сам Button/Popup правильно регистрирует items. Проблемный вызов — восстановление post-image cursor после overlay.

## Исправление

Удалены сохранение и восстановление post-image cursor. Позиционирование Create внутри уже зарегистрированного Image сохранено: +8 px от левого верхнего угла. Button остаётся последним layout item; после него нужны только draw-list helpers. Image по-прежнему определяет CursorMaxPos и полный размер Viewport.

Не добавлены Dummy, ручные изменения CursorMaxPos/content bounds, дополнительные окна или отключение asserts/error recovery. Главное меню, popup/actions, camera aspect calculation, framebuffer sizing и docking code не менялись. Fog rendering, Lua и Physics не затронуты.

## Регрессия

Расширен существующий `FogPostHotfixRegression::Ui`:

- ImGui ErrorCallback собирает ошибки на каждом запуске кадров; любая ошибка приводит к FAILED. ConfigErrorRecoveryEnableAssert/DebugLog/Tooltip остаются прежними.
- Вход/выход F11 и несколько кадров после переключений, повторные циклы.
- Pointer-click Create/Environment/Fog в обычном и F11 Viewport, включая масштабы UI 100/150/200%.
- Resize GLFW window 1024×768 и 1152×648; проверка полноэкранных bounds относительно main viewport, framebuffer scale, фактического readback render target.
- Проверка отсутствия scrollbars и превышения content size над размером окна.
- Сохранение DockId и возврат обычных viewport bounds после восстановления DPI/размера окна.
- Существующие проверки Menu Bar без Create, Fog icon picking, обычного mesh picking, Undo/Redo и запрета Create в Play сохранены.

Тестовые клики по меню используют фактически зарегистрированную высоту строк popup. GetFontSize вне popup frame мог возвращать размер fallback window, что давало неверный адрес строки при 150% DPI.

Для DPI fixture до первого кадра резервируется font atlas 1024×1024. Обнаружено отдельное существующее ограничение DX12 backend: выделен один ImGui descriptor, поэтому рост atlas при загрузке нескольких размеров шрифта может завершиться `DX12 ImGui descriptor allocation failed`. Production backend в этом локальном исправлении не изменён. Тест проверяет layout при UI DPI 100/150/200%; перенос реального окна между мониторами с разным Windows DPI и рост font atlas этим тестом полностью не покрываются.

## Результаты

Release-сборка всех targets: PASS (`out/f11-layout-release.log`, exit 0); финальная пересборка regression fixture: PASS (`out/f11-layout-build-final.log`).

| Проверки | Результат |
|---|---|
| FogPostHotfixRegression / FogPostHotfixDx12Regression, финальная fixture | 2/2 PASS, 12,80 s |
| MultiSelection CPU/DX12 | 2/2 PASS |
| Editor2 CPU/DX12 | 2/2 PASS |
| EditorLevelWorkflow CPU/DX12 | 2/2 PASS |
| EditorWorkflow CPU/DX12 | 2/2 PASS |
| EditorUsability CPU/DX12 | 2/2 PASS |
| ScalePlaneGizmoRegression | 1/1 PASS |

Итого 13 профильных тестов прошли. Логи: `out/f11-layout-final-tests.log` и `out/f11-layout-profile.log`. В первом профильном запуске F11 fixture упала на отдельном ограничении роста font atlas; остальные тесты прошли. После резервирования atlas оба изменённых CPU/DX12 теста повторно прошли. Исходное предупреждение ImGui исправлено до этого и больше не регистрируется; asserts и диагностические сообщения остаются включёнными. DX12 validation в целевом тесте: 0 ошибок. `git diff --check`: PASS.

Полный CTest не запускался. Ручная проверка на разных физических DPI/мониторах остаётся необходимой; синтетические UI DPI и GLFW resize проверяются автоматически. Коммиты и push не выполнялись.

## Файлы

- `engine/editor/EditorLayer.cpp`: только устранение ошибочного восстановления cursor и поясняющий комментарий.
- `tests/FogPostHotfixRegression.cpp`: диагностика ImGui и F11/resize/DPI regression.
- `tests/F11_VIEWPORT_LAYOUT_HOTFIX_REPORT.md`: этот отчёт.
