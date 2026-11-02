# RenderingLab3

Изолированный DX12-проект для ЛР №3, вариант Г. Сцена:
`Scenes/MaterialSystem.json`. Полное руководство: `../../docs/RenderingLab3-Demo.md`.

Из корня репозитория, после Release-сборки:

```powershell
& .\out\build\x64-release\WhispEngine.exe --project .\test_projects\RenderingLab3\RenderingLab3.whispproject
```

Слева направо: A — общий серебристый материал; B — тот же asset с cyan Tint;
C — тот же asset с Roughness 0.85; D — отдельный медный материал с текстурой.
На новом запуске editor camera смотрит на ряд. Если положение камеры уже менялось,
выделите A–D в Hierarchy и нажмите F. Game View (G) скрывает gizmos/grid;
UI Preview (Ctrl+U) включает необязательную легенду. Play использует Demo Camera.

Материальные изменения видны в preview сразу, в сцене — после **Save Material**.
После редактирования вернитесь во вкладку сцены. Не путайте Refresh Asset Browser
с reload содержимого ресурса.

Для ошибок сначала откройте материал D, дождитесь загрузки preview и вернитесь
в сцену. Это регистрирует material/shader watches. Draft должен быть сохранён
или отменён. Команды меняют только копии ресурсов данного проекта:

```powershell
.\test_projects\RenderingLab3\DemoActions.ps1 MissingTexture
.\test_projects\RenderingLab3\DemoActions.ps1 RestoreAlternate
.\test_projects\RenderingLab3\DemoActions.ps1 BreakShader
.\test_projects\RenderingLab3\DemoActions.ps1 RestoreShader
```

После намеренной ошибки всегда восстановите ресурс. Не закрывайте редактор между
BreakShader и RestoreShader: last-good GPU allocation существует в текущем процессе.
`Recovery` содержит исходные копии. Engine shaders и пользовательские проекты
скриптом не затрагиваются.

Проверка проекта: `Verification/VerifyDemo.py` из x64 VS Developer shell, после
сборки существующего target MaterialEditorUXRegression. Верификатор создаёт копию
проекта под `out/build/x64-release/renderinglab3-verification/run-*`, проверяет
реальный DX12 output и не записывает исходную демонстрационную сцену.
