# Visual Widget Designer (UI.2)

## Создание HUD

1. Откройте **View → Workspaces → UI Designer**. Нажмите **New UI Document**, либо дважды щёлкните `.ui.json` в Asset Browser. Контекстное меню ассета и Inspector компонента UI Canvas также содержат **Open in Widget Designer**.
2. В Widget Hierarchy выберите Root и добавьте Panel. Добавьте внутрь Text, Image или ProgressBar. Для Image перетащите текстуру из существующего Asset Browser на поле Texture.
3. Выберите виджет в дереве или нажмите на его видимую часть в Canvas Designer. Перетаскивание перемещает его; восемь жёлтых handles изменяют размеры. Прозрачные и скрытые Panel доступны через дерево.
4. Настройте Anchor Preset и Pivot Preset в Widget Inspector. Эти операции сохраняют текущий прямоугольник. Ручные Anchor Min/Max, Pivot, Position и Size Delta находятся ниже в общем редакторе свойств UI.1.
5. Для переноса между Panel перетащите строку дерева на нового родителя. Положение сохраняется. Root, циклы и родители не типа Panel отклоняются целиком. Earlier/Later меняют порядок соседей. Rename ID обновляет ссылки детей; Duplicate копирует поддерево с новыми уникальными ID; Delete удаляет поддерево.
6. Колесо изменяет масштаб вокруг курсора, средняя кнопка панорамирует, Fit вписывает Canvas. В Canvas Options параметры Show Grid, Snap to Grid и Grid Step задают сетку в логических единицах. Alt временно отключает snapping. Escape отменяет текущий move/resize.
7. Preview Resolution содержит обычные и портретный размеры, Custom Resolution / Canvas Options → Custom Preview — произвольный размер. Это только предпросмотр: Reference Resolution документа меняется отдельно в Inspector.
8. Нажмите **Save UI** или Ctrl+S. Добавьте UI Canvas в сцену и укажите сохранённый документ. Для примера используйте `ui/PlayerHUD.ui.json` и `scenes/interactive_room_ui.json`.

## История и сохранение

Ctrl+Z, Ctrl+Shift+Z/Ctrl+Y и Ctrl+D/Delete в трёх панелях работают с UI Document. Scene Selection и Scene Undo не участвуют. Поля ввода сохраняют обычное поведение ввода текста. Один move/resize, reparent, preset или изменение структуры создаёт одну запись UI history. Escape восстанавливает исходный документ без добавления записи.

Список Document позволяет переключаться между открытыми и индексированными документами. Черновики хранятся по абсолютному пути источника, переживают переключение документа, закрытие панели и смену workspace в текущем сеансе редактора. Закрытие приложения не сохраняет их автоматически. Save Scene не заменяет Save UI. Reload UI from disk требует явного подтверждения и очищает историю только после успешной загрузки. При внешнем изменении документа Save UI отклоняется, сохраняя черновик. Во время Play разрешён предпросмотр, но authored-редактирование заблокировано.

## Устройство

Аудит UI.1 выявил готовые `ui::Document`, независимые от Scene черновики/Undo/Save, layout, атлас Segoe UI и загрузку Image через ResourceManager. Не хватало Canvas selection, манипуляторов, операций дерева и рабочего пространства.

Дизайнер вызывает тот же `ui::LayoutDocument`, что Runtime UI, с теми же атласом и GPU-текстурами из RenderSystem. Панель отображает полученные quads, UV, цвета и clip rects через существующий ImGui GPU backend. Это только представление editor preview; Runtime UI продолжает рисоваться своим существующим renderer без ImGui. Новый DX12 pipeline или render target не создаётся. Шрифт и текстуры имеют прежнего владельца и освобождаются RenderSystem.

LayoutDocument дополнительно сообщает диапазоны собственных draw commands каждого виджета. Picking обходит эти диапазоны в обратном порядке и проверяет пересечение реальной геометрии с clipping. Прозрачные заливки, пустой текст, скрытые поддеревья и полностью отсечённая геометрия не перехватывают клик. Picking геометрический: прозрачные пиксели внутри Image/glyph quad не анализируются.

`WidgetDesignerTools` использует `LayoutRect` для вычисления прямоугольников и его обратную формулу для Position/SizeDelta. При stretched anchors из желаемого размера вычитается anchor span. Pivot сохраняется; размер ограничен нулём. Move/resize вычисляется от исходных widget/rect и позиции мыши, с учётом zoom и uniform canvas scale, без накопления покадровых смещений. Во время жеста хранится один исходный widget и одна pending запись существующей истории UI.1; ECS World не клонируется.

Hierarchy и presets изменяют копию того же Document, проверяют её через `ui::Validate` и передают в `EditUIDocument`. Отдельной сериализации и отдельного layout нет. Inspector свойств общий с UI Canvas Inspector. Уникальные ID и authored/runtime separation сохранены для UI.3; Lua UI API, Button events и input widgets не добавлялись.

## Проверки

`WidgetDesignerRegression` проверяет геометрию при разных разрешениях, все anchor/pivot presets, восемь resize handles, stretched SizeDelta, clipping/picking, атомарный отказ reparent/rename, операции поддерева, реальные события мыши Canvas, move/resize/Text, Escape, историю, смену документа/workspace, Save/conflict и GPU validation. `RuntimeUIRegression` проверяет независимую runtime-отрисовку, изображения, кириллицу, clipping, alpha, несколько Canvas, Save/Load, Play/F11/Clean Play и Interactive Room. `InteractiveRoomRegression` проверяет игровой сценарий.

Ограничения UI.1 сохраняются: Screen Space Overlay, uniform scale, Segoe UI Latin/Cyrillic с явными переносами строк. Дизайнер не поддерживает text shaping, world-space UI, smart guides и редактирование authored-документа во время Play. Черновики не являются восстановлением после перезапуска приложения.

## Файлы UI.2

- `engine/editor/EditorWidgetDesigner.cpp`, `WidgetDesignerTools.h/.cpp`: панели, управление, операции геометрии/иерархии.
- `engine/editor/EditorRuntimeUI.cpp`, `EditorLayer.h/.cpp`: общий Inspector, открытие документа, отмена и reload.
- `engine/editor/EditorWorkspaces.h/.cpp`, `EditorPreferences.cpp`, `EditorUi.cpp`: workspace и видимость панелей.
- `engine/editor/EditorAssets.cpp`: открытие/создание UI Documents.
- `engine/ui/RuntimeUI.h/.cpp`: метаданные уже отрисованной геометрии для picking.
- `engine/ecs/systems/RenderSystem.h/.cpp`, `engine/core/Application.h`: доступ к общему runtime preview и его ресурсам.
- `engine/CMakeLists.txt`, `CMakeLists.txt`, `tests/WidgetDesignerRegression.cpp`: сборка и целевые проверки.
