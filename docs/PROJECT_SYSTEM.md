# Project System MVP

## Создание и открытие

При обычном запуске `WhispEngine.exe` появляется отдельный стартовый **Project
Browser**, без сцены и панелей редактора позади. Редактор становится доступен
после открытия проекта или явного выбора **Legacy Workspace**. Project Browser
также доступен через **File → New Project / Open Project / Project Settings**.
`WhispEngine.exe --project <descriptor.whispproject>` сразу открывает проект;
при ошибке остаётся стартовый экран с сообщением. **Close Project** возвращает
на стартовый экран, не включая Legacy Workspace автоматически.

На вкладке **New Project** задайте Project Name, Location — существующую родительскую
папку — и шаблон Empty Project. **Create** создаёт новую папку и открывает проект.
Существующая папка никогда не перезаписывается. Пример:

```text
HospitalDemo/
  HospitalDemo.whispproject
  Assets/
    models/ textures/ materials/ scripts/ audio/ prefabs/ environments/
  Scenes/Main.json
  Settings/
```

Вкладка **Open Project** принимает путь к `.whispproject`; **Browse** открывает
системный диалог Windows с поддержкой Unicode. Вкладка **Recent** хранит имена и
пути последних 12 проектов. **Continue last project** открывает последний проект.
Недоступные записи помечаются; их можно удалить из списка. Ошибка открытия
показывается в Browser и не заменяет текущий мир пустой сценой.

Командная строка: `WhispEngine.exe --project "D:/Games/HospitalDemo/HospitalDemo.whispproject"`.
Для Unicode-путей используйте системный Browse, если терминал передаёт аргументы
не в UTF-8. Автоматическая регистрация файловых ассоциаций не выполняется.

## Сцены и настройки

**File → New Scene / Open Scene / Save / Save As** работают с текущим проектом.
Ключ сцены задаётся относительно папки проекта: `Scenes/Main.json`,
`Scenes/Levels/Ward.json`. Сохраняется существующий JSON scene schema v4.
Save As требует подтверждения замены существующего файла. Первое обычное Save
использует `Scenes/Untitled.json` и отказывается перезаписывать существующий файл.

В Project Browser активного проекта нажмите **Use current scene as Startup**.
Сцена должна быть сохранена. Выбор атомарно записывается в descriptor.
При открытии загружается Startup Scene; отсутствие или повреждение сцены — ошибка
открытия, текущий проект сохраняется.

Минимальные настройки описаны в `.whispproject`:

```json
{
  "formatVersion": 1,
  "name": "HospitalDemo",
  "startupScene": "Scenes/Main.json",
  "directories": { "assets": "Assets", "scenes": "Scenes" }
}
```

Имя и каталоги можно изменить в descriptor при закрытом проекте. Перенесите
соответствующие папки вручную и обновите startupScene, если меняете scenes.
Asset/scene directories должны быть раздельными и находиться внутри проекта.
Неизвестные поля descriptor сохраняются при изменении Startup Scene: формат
оставляет место для rendering/input/build/runtime settings.

## Ресурсы и Lua

Asset Browser индексирует **Assets текущего проекта**, импорт и файловые операции
работают там же. Через **Import Asset** выберите исходную модель/изображение,
проверьте зависимости и подтвердите импорт. Существующий импорт OBJ/glTF/GLB,
режимы отдельных мешей/сцены и проверки публикации пакета сохранены.

Используются прежние имена категорий в нижнем регистре: `models`, `textures`,
`materials`, `scripts`, `prefabs`, `environments`. Это осознанный формат, а не
зависимость от нечувствительности Windows к регистру.

Ссылки внутри сцен, prefab и материалов задаются **относительно Assets**:
`models/building/hospital.glb`, `textures/wall.png`, `scripts/DoorController.lua`.
Текстуры импортированного glTF могут ссылаться на файлы внутри пакета модели.
Drag-and-drop, назначение через Inspector и существующие preview работают с
текущим Asset Root. New Folder, Rename, Move и Delete сохраняют проверки зависимостей.
Move принимает полный asset key внутри той же категории и не перезаписывает файлы.
Изменения файлов находятся вне scene Undo. Сначала снимите ссылки на ассет и
очистите использующую его историю; редактор блокирует изменение ссылочных файлов.

Lua-файлы помещайте в `Assets/scripts`. **Copy FPS Controller** в Project Browser
копирует поставляемый `FirstPersonController.lua` без перезаписи существующего.
У PlayerStart задайте `scripts/FirstPersonController.lua` или свой контроллер.
Play создаёт отдельный Runtime World и runtime Player; существующие Input,
Character и Camera API сохранены. Lua hot reload использует файлы этого проекта.
Ошибки отсутствующих скриптов и ресурсов диагностируются существующими Lua/resource
сообщениями; отсутствующий проектный файл не подменяется legacy-версией.

Шейдеры остаются встроенными, например `dx12/textured.hlsl`. Примитивы Viewport
Create используют явные read-only ключи `@engine/models/primitives/cube.obj` и
`@engine/materials/default_primitive.material.json`. Для собственного материала
создайте ресурс в `Assets/materials`. Engine shader files не показываются среди
пользовательских файлов проекта. Верхнее меню не содержит Create Entity.

## Переключение, сохранение и перенос

Открытие другого проекта и **Close Project** предлагают Save / Discard / Cancel
при несохранённых scene/material edits. Cancel сохраняет проект, selection и
Play/Pause. Save из Play/Pause сохраняет редакторскую сцену, не runtime-объекты.
После успешной подготовки следующего проекта редактор останавливает Lua/Play,
освобождает курсор, очищает selection/Undo/drafts и ресурсы предыдущего проекта.
Закрытие окна с несохранённым активным проектом использует тот же диалог.

Для переноса закройте проект, скопируйте **всю его папку**, откройте descriptor
на новом месте. Абсолютные пути не требуются в игровых ссылках. В Recent останется
старая недоступная запись; открытие нового descriptor добавит новый путь.
Editor preferences и ImGui layout хранятся рядом с EXE, вне проекта. Поэтому
каталог установленного редактора должен быть доступен для записи preferences.

## Legacy Workspace и ограничения

Выберите **Legacy Workspace** на стартовом экране или запустите
`WhispEngine.exe --legacy`, чтобы работать в старом режиме. При использовании
прежних demo/benchmark CLI-параметров также укажите `--legacy`.
Стартовый Browser закрывается кнопкой **Exit** или закрытием окна приложения;
закрытие Browser не служит неявным выбором Legacy Workspace.
Старые `scenes/*.json`, staged/source `engine/assets` и
regression tests продолжают использовать прежнее разрешение путей. Автоматической
миграции, удаления и массового перемещения legacy-ресурсов нет.

MVP предоставляет Empty Project; 3D Starter Template и экспорт EXE не включены.
Каталог audio зарезервирован, новая аудиоподсистема не добавляется. Редактор
блокирует symlinks/junctions/reparse points в проектах, traversal и absolute
project-relative keys. Управление зависимостями импортированных model packages
остаётся консервативным: Rename/Move/Delete отдельных model-файлов защищены,
переносить пакет с изменением ссылок следует отдельным будущим workflow.

Несохранённая untitled-сцена в Play сохраняется под стандартным Untitled.json;
при коллизии остановите Play и используйте Save As. Изменения имени и каталогов
сейчас доступны через descriptor, не отдельные поля графического settings editor.
Настройки редактора общие, автоматического открытия последнего проекта нет:
используйте Continue last project. Group Scale X/Z и прежние F11-предупреждения
не входят в эту работу.
