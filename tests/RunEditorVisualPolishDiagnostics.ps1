param([int[]]$Counts=@(100,500,1000))
$ErrorActionPreference='Stop'
$polishRepo=Split-Path -Parent $PSScriptRoot
$polishBuild=Join-Path $polishRepo 'out/build/shadows-dx12'
$polishOut=Join-Path $polishRepo 'out/diagnostics/editor-visual-polish'
New-Item -ItemType Directory -Force $polishOut | Out-Null
$polishIni=Join-Path $polishBuild 'imgui.ini'
$polishPrefs=Join-Path $polishBuild 'editor-preferences.json'
$polishIniBytes=if(Test-Path -LiteralPath $polishIni){[IO.File]::ReadAllBytes($polishIni)}else{$null}
$polishPrefBytes=if(Test-Path -LiteralPath $polishPrefs){[IO.File]::ReadAllBytes($polishPrefs)}else{$null}
$polishSeed=@'
[Window][Viewport]
Pos=260,60
Size=760,520
Collapsed=0

[Window][Scene Hierarchy]
Pos=0,60
Size=260,520
Collapsed=0

[Window][Inspector]
Pos=1020,60
Size=260,520
Collapsed=0

[Window][Statistics]
Pos=0,600
Size=200,100
Collapsed=1

[Window][Material Editor]
Pos=220,600
Size=200,100
Collapsed=1

[Window][Physics Stress Test]
Pos=440,600
Size=200,100
Collapsed=1

[Window][Asset Browser]
Pos=660,600
Size=200,100
Collapsed=1

[Window][Scene Settings]
Pos=880,600
Size=200,100
Collapsed=1

[Window][Console / Diagnostics]
Pos=1100,600
Size=170,100
Collapsed=1
'@
Push-Location $polishBuild
try{
    foreach($polishCount in $Counts){
        foreach($polishMode in @('off','grid','gradient','cubemap')){
            Set-Content -LiteralPath $polishIni -Value $polishSeed -Encoding utf8
            # Stable UI/helper settings for each independent process; restore user bytes below.
            $polishPreferences=@{version=1;grid=$true;lightGizmos=$false;panels=@{visuals=$false};visuals=@{enabled=$true;background=1;cubemap='environments/studio.hdr'}}
            Set-Content -LiteralPath $polishPrefs -Value ($polishPreferences|ConvertTo-Json -Depth 5) -Encoding utf8
            $polishCsv=Join-Path $polishOut "$polishMode-$polishCount.csv"
            $polishArgs=@('--render-benchmark',[string]$polishCount,$polishCsv,'--fixed-editor-layout')
            switch($polishMode){
                'off' {$polishArgs+='--no-editor-visuals'}
                'grid' {$polishArgs+='--editor-grid-only'}
                'gradient' {$polishArgs+='--editor-gradient'}
                'cubemap' {$polishArgs+=@('--editor-cubemap','environments/studio.hdr')}
            }
            & './WhispEngine.exe' @polishArgs *> (Join-Path $polishOut "$polishMode-$polishCount.log")
            if($LASTEXITCODE -ne 0){throw "Benchmark $polishMode/$polishCount failed"}
            if(!(Test-Path -LiteralPath $polishCsv) -or (Import-Csv -LiteralPath $polishCsv).Count -ne 1200){throw "Incomplete CSV $polishCsv"}
            Write-Output "$polishMode-$polishCount PASS"
        }
    }
}finally{
    if($null -ne $polishIniBytes){[IO.File]::WriteAllBytes($polishIni,$polishIniBytes)}elseif(Test-Path -LiteralPath $polishIni){Remove-Item -LiteralPath $polishIni}
    if($null -ne $polishPrefBytes){[IO.File]::WriteAllBytes($polishPrefs,$polishPrefBytes)}elseif(Test-Path -LiteralPath $polishPrefs){Remove-Item -LiteralPath $polishPrefs}
    Pop-Location
}
