param(
    [string]$BuildDirectory='out/build/shadows-dx12',
    [string]$Executable='WhispEngine.exe',
    [string]$Phase='after',
    [string[]]$ExtraArguments=@(),
    [string[]]$Cases=@('100','500','1000','lights100','lights250','lights500','lights500-off','browser-list','browser-grid')
)
$ErrorActionPreference='Stop'
$editor24Repo=Split-Path -Parent $PSScriptRoot
$editor24Build=[IO.Path]::GetFullPath((Join-Path $editor24Repo $BuildDirectory))
$editor24Out=Join-Path $editor24Repo "out/diagnostics/editor24/$Phase"
New-Item -ItemType Directory -Force $editor24Out | Out-Null
$editor24Ini=Join-Path $editor24Build 'imgui.ini'
$editor24Prefs=Join-Path $editor24Build 'editor-preferences.json'
$editor24IniBytes=if(Test-Path -LiteralPath $editor24Ini){[IO.File]::ReadAllBytes($editor24Ini)}else{$null}
$editor24PrefBytes=if(Test-Path -LiteralPath $editor24Prefs){[IO.File]::ReadAllBytes($editor24Prefs)}else{$null}
$editor24OwnedAssets=$null
Push-Location $editor24Build
try {
    if(@($Cases | Where-Object {$_ -like 'browser-*'}).Count){
        $editor24Candidate=Join-Path $editor24Build 'assets/models/editor24-browser-fixture'
        if(Test-Path -LiteralPath $editor24Candidate){throw 'Benchmark fixture directory already exists; refusing to touch it'}
        New-Item -ItemType Directory -Path $editor24Candidate | Out-Null
        $editor24OwnedAssets=$editor24Candidate
        0..999 | ForEach-Object {Copy-Item -LiteralPath (Join-Path $editor24Build 'assets/models/validation_triangle.obj') -Destination (Join-Path $editor24OwnedAssets ('asset-{0:D4}.obj' -f $_))}
    }

    foreach($editor24Case in $Cases){
        $editor24Collapsed=if($editor24Case -eq 'inspector-collapsed'){1}else{0}
        $editor24BrowserCollapsed=if($editor24Case -like 'browser-*'){0}else{1}
        $editor24Seed=@"
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
Collapsed=$editor24Collapsed

[Window][Asset Browser]
Pos=260,580
Size=760,140
Collapsed=$editor24BrowserCollapsed

[Window][Statistics]
Pos=0,600
Size=200,100
Collapsed=1

[Window][Material Editor]
Pos=1020,600
Size=260,100
Collapsed=1

[Window][Physics Stress Test]
Pos=0,580
Size=240,140
Collapsed=1

[Window][Scene Settings]
Pos=0,640
Size=240,80
Collapsed=1

[Window][Console / Diagnostics]
Pos=1020,580
Size=260,140
Collapsed=1
"@
        if($editor24Case -like 'browser-*'){$editor24Seed=$editor24Seed.Replace("Pos=260,580`nSize=760,140","Pos=260,480`nSize=760,240").Replace("Pos=260,580`r`nSize=760,140","Pos=260,480`r`nSize=760,240")}
        Set-Content -LiteralPath $editor24Ini -Value $editor24Seed -Encoding utf8
        $editor24Preference=@{version=1;grid=$true;lightGizmos=($editor24Case -like 'lights*' -and $editor24Case -ne 'lights500-off');projectileTestInput=$false;backgroundOverride=$false;assetGrid=($editor24Case -eq 'browser-grid');panels=@{visuals=$false;settings=$true;console=$false;assets=$true;inspector=$true};visuals=@{enabled=$true;background=1;cubemap='environments/studio.hdr'}}
        Set-Content -LiteralPath $editor24Prefs -Value ($editor24Preference|ConvertTo-Json -Depth 5) -Encoding utf8
        $editor24Csv=Join-Path $editor24Out "$editor24Case.csv"
        $editor24Args=@('--fixed-editor-layout','--diagnostic-viewport','744','411','--editor-gradient')
        if($editor24Case -eq 'default'){$editor24Args+=@('--editor-benchmark',$editor24Csv,'--select-first')}
        elseif($editor24Case -like 'lights*'){$light23Count=[regex]::Match($editor24Case,'[0-9]+').Value;$editor24Args+=@('--editor-benchmark',$editor24Csv,'--light-stress',$light23Count)}
        elseif($editor24Case -eq 'primitives'){$editor24Args+=@('--editor-benchmark',$editor24Csv,'--primitive-demo')}
        elseif($editor24Case -eq 'materials'){$editor24Args+=@('--editor-benchmark',$editor24Csv,'--material-demo','--select-first')}
        else {
            $editor24Count=if($editor24Case -in @('100','500','1000')){$editor24Case}else{'500'}
            $editor24Args+=@('--render-benchmark',$editor24Count,$editor24Csv)
        }
        switch($editor24Case){
            'shadow-low' {$editor24Args+=@('--shadow-quality','0')}
            'shadow-medium' {$editor24Args+=@('--shadow-quality','1')}
            'shadow-high' {$editor24Args+=@('--shadow-quality','2')}
            'shadow-ultra' {$editor24Args+=@('--shadow-quality','3')}
            'sky-off' {$editor24Args+='--sky-off'}
            'sky-on' {$editor24Args+=@('--editor-cubemap','environments/studio.hdr')}
            'browser-grid' {$editor24Args+='--asset-grid'}
        }
        $editor24Args+='--select-first' # Select after scene setup, so Inspector measurements include actual components.
        $editor24Args+=$ExtraArguments
        & (Join-Path $editor24Build $Executable) @editor24Args *> (Join-Path $editor24Out "$editor24Case.log")
        if($LASTEXITCODE -ne 0){throw "$Phase/$editor24Case failed: $LASTEXITCODE"}
        $editor24Samples=Import-Csv -LiteralPath $editor24Csv
        if($editor24Samples.Count -ne 1200){throw "$Phase/${editor24Case}: incomplete samples"}
        if(@($editor24Samples | Where-Object {$_.viewportWidth -ne '744' -or $_.viewportHeight -ne '411'}).Count){throw "Mismatched viewport in $editor24Case"}
        Write-Output "$Phase/${editor24Case}: 1200 samples / 744x411 PASS"
    }
} finally {
    if($null -ne $editor24OwnedAssets){
        $editor24Resolved=[IO.Path]::GetFullPath($editor24OwnedAssets)
        $editor24Expected=[IO.Path]::GetFullPath((Join-Path $editor24Build 'assets/models/editor24-browser-fixture'))
        if($editor24Resolved -ne $editor24Expected){throw 'Fixture cleanup containment check failed'}
        if(Test-Path -LiteralPath $editor24Resolved){Remove-Item -LiteralPath $editor24Resolved -Recurse -Force}
    }

    if($null -ne $editor24IniBytes){[IO.File]::WriteAllBytes($editor24Ini,$editor24IniBytes)}elseif(Test-Path -LiteralPath $editor24Ini){Remove-Item -LiteralPath $editor24Ini}
    if($null -ne $editor24PrefBytes){[IO.File]::WriteAllBytes($editor24Prefs,$editor24PrefBytes)}elseif(Test-Path -LiteralPath $editor24Prefs){Remove-Item -LiteralPath $editor24Prefs}
    Pop-Location
}
