param([switch]$Baseline)
$ErrorActionPreference = 'Stop'
$editorRepo = Split-Path -Parent $PSScriptRoot
$editorBuild = Join-Path $editorRepo 'out/build/shadows-dx12'
$editorOutput = Join-Path $editorRepo 'out/diagnostics/editor2'
$editorExe = if ($Baseline) { Join-Path $editorOutput 'baseline-build/EditorBaseline.exe' } else { Join-Path $editorBuild 'WhispEngine.exe' }
$editorPrefix = if ($Baseline) { 'before-ui' } else { 'after' }
if (!(Test-Path -LiteralPath $editorExe)) { throw "Missing executable: $editorExe" }
$editorIni = Join-Path $editorBuild 'imgui.ini'
$editorPreferenceFile = Join-Path $editorBuild 'editor-preferences.json'
$editorOriginalIni = if (Test-Path -LiteralPath $editorIni) { [IO.File]::ReadAllBytes($editorIni) } else { $null }
$editorOriginalPreferences = if (Test-Path -LiteralPath $editorPreferenceFile) { [IO.File]::ReadAllBytes($editorPreferenceFile) } else { $null }
$editorSavedIni = Join-Path $editorOutput 'before-imgui.ini'
if (!(Test-Path -LiteralPath $editorSavedIni)) { throw 'Preserve baseline imgui.ini before measurements.' }
$editorBenchmarkIni = Join-Path $editorOutput 'editor-benchmark-imgui.ini'
# Expand the same Inspector in both versions; the stored user layout had it collapsed.
$editorIniText = Get-Content -LiteralPath $editorSavedIni -Raw
$editorIniText = [regex]::Replace($editorIniText, '(?s)(\[Window\]\[Inspector\].*?Collapsed=)1', '${1}0')
Set-Content -LiteralPath $editorBenchmarkIni -Value $editorIniText -Encoding utf8
Push-Location $editorBuild
try {
    $editorCases = @(
        @{ Name='entities-100'; Switch='--render-benchmark'; Count=100 },
        @{ Name='entities-500'; Switch='--render-benchmark'; Count=500 },
        @{ Name='entities-1000'; Switch='--render-benchmark'; Count=1000 },
        @{ Name='lights'; Switch='--light-benchmark'; Count=500 },
        @{ Name='materials'; Switch='--material-benchmark'; Count=50 }
    )
    foreach ($editorCase in $editorCases) {
        Copy-Item -LiteralPath $editorBenchmarkIni -Destination $editorIni
        $editorCsv = Join-Path $editorOutput "$editorPrefix-$($editorCase.Name).csv"
        $editorLog = Join-Path $editorOutput "$editorPrefix-$($editorCase.Name).log"
        $editorArguments = @($editorCase.Switch,[string]$editorCase.Count,$editorCsv)
        if (!$Baseline) { $editorArguments += '--fixed-editor-layout' }
        & $editorExe @editorArguments *> $editorLog
        if ($LASTEXITCODE -ne 0) { throw "Diagnostic $($editorCase.Name) failed: $LASTEXITCODE" }
        if (!(Test-Path -LiteralPath $editorCsv) -or (Import-Csv -LiteralPath $editorCsv).Count -lt 1000) { throw "Incomplete CSV: $editorCsv" }
        Write-Output "$editorPrefix-$($editorCase.Name): PASS"
    }
    $editorCpuExe = if ($Baseline) { Join-Path $editorOutput 'baseline-build/BaselineCpu.exe' } else { Join-Path $editorBuild 'Editor2Regression.exe' }
    # ASCII relative argv also works with the baseline CRT before Application.Initialize configures locale.
    $editorCpuCsv = "../../diagnostics/editor2/$editorPrefix-transitions.csv"
    $editorCpuArguments = if ($Baseline) { @($editorCpuCsv) } else { @('--benchmark',$editorCpuCsv) }
    & $editorCpuExe @editorCpuArguments *> (Join-Path $editorOutput "$editorPrefix-transitions.log")
    if ($LASTEXITCODE -ne 0) { throw 'CPU transition diagnostics failed' }
    if (!(Test-Path -LiteralPath $editorCpuCsv) -or (Import-Csv -LiteralPath $editorCpuCsv).Count -ne 75) { throw 'Incomplete transition CSV' }
    Write-Output "$editorPrefix-transitions: PASS"
} finally {
    if ($null -ne $editorOriginalIni) { [IO.File]::WriteAllBytes($editorIni,$editorOriginalIni) }
    if ($null -ne $editorOriginalPreferences) { [IO.File]::WriteAllBytes($editorPreferenceFile,$editorOriginalPreferences) }
    elseif (Test-Path -LiteralPath $editorPreferenceFile) { Remove-Item -LiteralPath $editorPreferenceFile }
    Pop-Location
}
