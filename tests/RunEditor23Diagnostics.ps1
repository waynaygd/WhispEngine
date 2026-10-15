param(
    [string]$BuildDirectory='out/build/shadows-dx12',
    [string]$Executable='WhispEngine.exe',
    [string]$Phase='after',
    [string[]]$ExtraArguments=@(),
    [string[]]$Cases=@('100','500','1000','lights100','lights250','lights500','lights500-off','browser-list','browser-grid')
)
$ErrorActionPreference='Stop'
$editor23Repo=Split-Path -Parent $PSScriptRoot
$editor23Build=[IO.Path]::GetFullPath((Join-Path $editor23Repo $BuildDirectory))
$editor23Out=Join-Path $editor23Repo "out/diagnostics/editor23/$Phase"
New-Item -ItemType Directory -Force $editor23Out | Out-Null
$editor23Ini=Join-Path $editor23Build 'imgui.ini'
$editor23Prefs=Join-Path $editor23Build 'editor-preferences.json'
$editor23IniBytes=if(Test-Path -LiteralPath $editor23Ini){[IO.File]::ReadAllBytes($editor23Ini)}else{$null}
$editor23PrefBytes=if(Test-Path -LiteralPath $editor23Prefs){[IO.File]::ReadAllBytes($editor23Prefs)}else{$null}
$editor23OwnedAssets=$null
Push-Location $editor23Build
try {
    if(@($Cases | Where-Object {$_ -like 'browser-*'}).Count){
        $editor23Candidate=Join-Path $editor23Build 'assets/models/editor23-browser-fixture'
        if(Test-Path -LiteralPath $editor23Candidate){throw 'Benchmark fixture directory already exists; refusing to touch it'}
        New-Item -ItemType Directory -Path $editor23Candidate | Out-Null
        $editor23OwnedAssets=$editor23Candidate
        0..999 | ForEach-Object {Copy-Item -LiteralPath (Join-Path $editor23Build 'assets/models/validation_triangle.obj') -Destination (Join-Path $editor23OwnedAssets ('asset-{0:D4}.obj' -f $_))}
    }

    foreach($editor23Case in $Cases){
        $editor23Collapsed=if($editor23Case -eq 'inspector-collapsed'){1}else{0}
        $editor23BrowserCollapsed=if($editor23Case -like 'browser-*'){0}else{1}
        $editor23Seed=@"
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
Collapsed=$editor23Collapsed

[Window][Asset Browser]
Pos=260,580
Size=760,140
Collapsed=$editor23BrowserCollapsed

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
        if($editor23Case -like 'browser-*'){$editor23Seed=$editor23Seed.Replace("Pos=260,580`nSize=760,140","Pos=260,480`nSize=760,240").Replace("Pos=260,580`r`nSize=760,140","Pos=260,480`r`nSize=760,240")}
        Set-Content -LiteralPath $editor23Ini -Value $editor23Seed -Encoding utf8
        $editor23Preference=@{version=1;grid=$true;lightGizmos=($editor23Case -like 'lights*' -and $editor23Case -ne 'lights500-off');projectileTestInput=$false;backgroundOverride=$false;assetGrid=($editor23Case -eq 'browser-grid');panels=@{visuals=$false;settings=$true;console=$false;assets=$true;inspector=$true};visuals=@{enabled=$true;background=1;cubemap='environments/studio.hdr'}}
        Set-Content -LiteralPath $editor23Prefs -Value ($editor23Preference|ConvertTo-Json -Depth 5) -Encoding utf8
        $editor23Csv=Join-Path $editor23Out "$editor23Case.csv"
        $editor23Args=@('--fixed-editor-layout','--diagnostic-viewport','744','411','--editor-gradient')
        if($editor23Case -eq 'default'){$editor23Args+=@('--editor-benchmark',$editor23Csv,'--select-first')}
        elseif($editor23Case -like 'lights*'){$light23Count=[regex]::Match($editor23Case,'[0-9]+').Value;$editor23Args+=@('--editor-benchmark',$editor23Csv,'--light-stress',$light23Count)}
        elseif($editor23Case -eq 'primitives'){$editor23Args+=@('--editor-benchmark',$editor23Csv,'--primitive-demo')}
        elseif($editor23Case -eq 'materials'){$editor23Args+=@('--editor-benchmark',$editor23Csv,'--material-demo','--select-first')}
        else {
            $editor23Count=if($editor23Case -in @('100','500','1000')){$editor23Case}else{'500'}
            $editor23Args+=@('--render-benchmark',$editor23Count,$editor23Csv)
        }
        switch($editor23Case){
            'shadow-low' {$editor23Args+=@('--shadow-quality','0')}
            'shadow-medium' {$editor23Args+=@('--shadow-quality','1')}
            'shadow-high' {$editor23Args+=@('--shadow-quality','2')}
            'shadow-ultra' {$editor23Args+=@('--shadow-quality','3')}
            'sky-off' {$editor23Args+='--sky-off'}
            'sky-on' {$editor23Args+=@('--editor-cubemap','environments/studio.hdr')}
            'browser-grid' {$editor23Args+='--asset-grid'}
        }
        $editor23Args+='--select-first' # Select after scene setup, so Inspector measurements include actual components.
        $editor23Args+=$ExtraArguments
        & (Join-Path $editor23Build $Executable) @editor23Args *> (Join-Path $editor23Out "$editor23Case.log")
        if($LASTEXITCODE -ne 0){throw "$Phase/$editor23Case failed: $LASTEXITCODE"}
        $editor23Samples=Import-Csv -LiteralPath $editor23Csv
        if($editor23Samples.Count -ne 1200){throw "$Phase/${editor23Case}: incomplete samples"}
        if(@($editor23Samples | Where-Object {$_.viewportWidth -ne '744' -or $_.viewportHeight -ne '411'}).Count){throw "Mismatched viewport in $editor23Case"}
        Write-Output "$Phase/${editor23Case}: 1200 samples / 744x411 PASS"
    }
} finally {
    if($null -ne $editor23OwnedAssets){
        $editor23Resolved=[IO.Path]::GetFullPath($editor23OwnedAssets)
        $editor23Expected=[IO.Path]::GetFullPath((Join-Path $editor23Build 'assets/models/editor23-browser-fixture'))
        if($editor23Resolved -ne $editor23Expected){throw 'Fixture cleanup containment check failed'}
        if(Test-Path -LiteralPath $editor23Resolved){Remove-Item -LiteralPath $editor23Resolved -Recurse -Force}
    }

    if($null -ne $editor23IniBytes){[IO.File]::WriteAllBytes($editor23Ini,$editor23IniBytes)}elseif(Test-Path -LiteralPath $editor23Ini){Remove-Item -LiteralPath $editor23Ini}
    if($null -ne $editor23PrefBytes){[IO.File]::WriteAllBytes($editor23Prefs,$editor23PrefBytes)}elseif(Test-Path -LiteralPath $editor23Prefs){Remove-Item -LiteralPath $editor23Prefs}
    Pop-Location
}
