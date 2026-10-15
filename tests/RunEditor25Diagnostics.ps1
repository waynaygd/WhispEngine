param(
    [string]$BuildDirectory='out/build/shadows-dx12',
    [string]$Executable='WhispEngine.exe',
    [string]$Phase='after',
    [string[]]$ExtraArguments=@(),
    [string[]]$Cases=@('100','500','1000','materials','lights100','lights250','lights500','browser-list','browser-grid')
)
$ErrorActionPreference='Stop'
$editor25Repo=Split-Path -Parent $PSScriptRoot
$editor25Build=[IO.Path]::GetFullPath((Join-Path $editor25Repo $BuildDirectory))
$editor25Out=Join-Path $editor25Repo "out/diagnostics/editor25/$Phase"
New-Item -ItemType Directory -Force $editor25Out | Out-Null
$editor25Ini=Join-Path $editor25Build 'imgui.ini'
$editor25Prefs=Join-Path $editor25Build 'editor-preferences.json'
$editor25IniBytes=if(Test-Path -LiteralPath $editor25Ini){[IO.File]::ReadAllBytes($editor25Ini)}else{$null}
$editor25PrefBytes=if(Test-Path -LiteralPath $editor25Prefs){[IO.File]::ReadAllBytes($editor25Prefs)}else{$null}
$editor25OwnedAssets=$null
Push-Location $editor25Build
try {
    if(@($Cases | Where-Object {$_ -like 'browser-*'}).Count){
        $editor25Candidate=Join-Path $editor25Build 'assets/models/editor25-browser-fixture'
        if(Test-Path -LiteralPath $editor25Candidate){throw 'Benchmark fixture directory already exists; refusing to touch it'}
        New-Item -ItemType Directory -Path $editor25Candidate | Out-Null
        $editor25OwnedAssets=$editor25Candidate
        0..999 | ForEach-Object {Copy-Item -LiteralPath (Join-Path $editor25Build 'assets/models/validation_triangle.obj') -Destination (Join-Path $editor25OwnedAssets ('asset-{0:D4}.obj' -f $_))}
    }

    foreach($editor25Case in $Cases){
        $editor25Collapsed=if($editor25Case -eq 'inspector-collapsed'){1}else{0}
        $editor25BrowserCollapsed=if($editor25Case -like 'browser-*'){0}else{1}
        $editor25Seed=@"
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
Collapsed=$editor25Collapsed

[Window][Asset Browser]
Pos=260,580
Size=760,140
Collapsed=$editor25BrowserCollapsed

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
        if($editor25Case -like 'browser-*'){$editor25Seed=$editor25Seed.Replace("Pos=260,580`nSize=760,140","Pos=260,480`nSize=760,240").Replace("Pos=260,580`r`nSize=760,140","Pos=260,480`r`nSize=760,240")}
        Set-Content -LiteralPath $editor25Ini -Value $editor25Seed -Encoding utf8
        $editor25Preference=@{version=1;grid=$true;lightGizmos=($editor25Case -like 'lights*' -and $editor25Case -ne 'lights500-off');projectileTestInput=$false;backgroundOverride=$false;assetGrid=($editor25Case -eq 'browser-grid');panels=@{visuals=$false;settings=$true;console=$false;assets=$true;inspector=$true};visuals=@{enabled=$true;background=1;cubemap='environments/studio.hdr'}}
        Set-Content -LiteralPath $editor25Prefs -Value ($editor25Preference|ConvertTo-Json -Depth 5) -Encoding utf8
        $editor25Csv=Join-Path $editor25Out "$editor25Case.csv"
        $editor25Args=@('--fixed-editor-layout','--diagnostic-viewport','744','411','--editor-gradient')
        if($editor25Case -eq 'default'){$editor25Args+=@('--editor-benchmark',$editor25Csv,'--select-first')}
        elseif($editor25Case -like 'lights*'){$light25Count=[regex]::Match($editor25Case,'[0-9]+').Value;$editor25Args+=@('--editor-benchmark',$editor25Csv,'--light-stress',$light25Count)}
        elseif($editor25Case -eq 'primitives'){$editor25Args+=@('--editor-benchmark',$editor25Csv,'--primitive-demo')}
        elseif($editor25Case -eq 'materials-heavy'){$editor25Args+=@('--material-benchmark','100',$editor25Csv)}
        elseif($editor25Case -eq 'materials'){$editor25Args+=@('--editor-benchmark',$editor25Csv,'--material-demo','--select-first')}
        else {
            $editor25Count=if($editor25Case -in @('100','500','1000')){$editor25Case}else{'500'}
            $editor25Args+=@('--render-benchmark',$editor25Count,$editor25Csv)
        }
        switch($editor25Case){
            'shadow-low' {$editor25Args+=@('--shadow-quality','0')}
            'shadow-medium' {$editor25Args+=@('--shadow-quality','1')}
            'shadow-high' {$editor25Args+=@('--shadow-quality','2')}
            'shadow-ultra' {$editor25Args+=@('--shadow-quality','3')}
            'sky-off' {$editor25Args+='--sky-off'}
            'sky-on' {$editor25Args+=@('--editor-cubemap','environments/studio.hdr')}
            'browser-grid' {$editor25Args+='--asset-grid'}
        }
        $editor25Args+='--select-first' # Select after scene setup, so Inspector measurements include actual components.
        $editor25Args+=$ExtraArguments
        & (Join-Path $editor25Build $Executable) @editor25Args *> (Join-Path $editor25Out "$editor25Case.log")
        if($LASTEXITCODE -ne 0){throw "$Phase/$editor25Case failed: $LASTEXITCODE"}
        $editor25Samples=Import-Csv -LiteralPath $editor25Csv
        $editor25Expected=if($editor25Case -eq 'materials-heavy'){1800}else{1200}
        if($editor25Samples.Count -ne $editor25Expected){throw "$Phase/${editor25Case}: incomplete samples"}
        if(@($editor25Samples | Where-Object {$_.viewportWidth -ne '744' -or $_.viewportHeight -ne '411'}).Count){throw "Mismatched viewport in $editor25Case"}
        Write-Output "$Phase/${editor25Case}: $editor25Expected samples / 744x411 PASS"
    }
} finally {
    if($null -ne $editor25OwnedAssets){
        $editor25Resolved=[IO.Path]::GetFullPath($editor25OwnedAssets)
        $editor25Expected=[IO.Path]::GetFullPath((Join-Path $editor25Build 'assets/models/editor25-browser-fixture'))
        if($editor25Resolved -ne $editor25Expected){throw 'Fixture cleanup containment check failed'}
        if(Test-Path -LiteralPath $editor25Resolved){Remove-Item -LiteralPath $editor25Resolved -Recurse -Force}
    }

    if($null -ne $editor25IniBytes){[IO.File]::WriteAllBytes($editor25Ini,$editor25IniBytes)}elseif(Test-Path -LiteralPath $editor25Ini){Remove-Item -LiteralPath $editor25Ini}
    if($null -ne $editor25PrefBytes){[IO.File]::WriteAllBytes($editor25Prefs,$editor25PrefBytes)}elseif(Test-Path -LiteralPath $editor25Prefs){Remove-Item -LiteralPath $editor25Prefs}
    Pop-Location
}
