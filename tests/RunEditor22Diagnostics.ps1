param(
    [string]$BuildDirectory='out/build/shadows-dx12',
    [string]$Executable='WhispEngine.exe',
    [string]$Phase='after',
    [string[]]$ExtraArguments=@(),
    [string[]]$Cases=@('default','100','500','1000','materials','shadow-low','shadow-medium','shadow-high','shadow-ultra','sky-off','sky-on','inspector-collapsed','browser-list','browser-grid')
)
$ErrorActionPreference='Stop'
$editor22Repo=Split-Path -Parent $PSScriptRoot
$editor22Build=[IO.Path]::GetFullPath((Join-Path $editor22Repo $BuildDirectory))
$editor22Out=Join-Path $editor22Repo "out/diagnostics/editor22/$Phase"
New-Item -ItemType Directory -Force $editor22Out | Out-Null
$editor22Ini=Join-Path $editor22Build 'imgui.ini'
$editor22Prefs=Join-Path $editor22Build 'editor-preferences.json'
$editor22IniBytes=if(Test-Path -LiteralPath $editor22Ini){[IO.File]::ReadAllBytes($editor22Ini)}else{$null}
$editor22PrefBytes=if(Test-Path -LiteralPath $editor22Prefs){[IO.File]::ReadAllBytes($editor22Prefs)}else{$null}
Push-Location $editor22Build
try {
    foreach($editor22Case in $Cases){
        $editor22Collapsed=if($editor22Case -eq 'inspector-collapsed'){1}else{0}
        $editor22BrowserCollapsed=if($editor22Case -like 'browser-*'){0}else{1}
        $editor22Seed=@"
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
Collapsed=$editor22Collapsed

[Window][Asset Browser]
Pos=260,580
Size=760,140
Collapsed=$editor22BrowserCollapsed

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
        if($editor22Case -like 'browser-*'){$editor22Seed=$editor22Seed.Replace("Pos=260,580`nSize=760,140","Pos=260,480`nSize=760,240").Replace("Pos=260,580`r`nSize=760,140","Pos=260,480`r`nSize=760,240")}
        Set-Content -LiteralPath $editor22Ini -Value $editor22Seed -Encoding utf8
        $editor22Preference=@{version=1;grid=$true;lightGizmos=$false;projectileTestInput=$false;backgroundOverride=$false;assetGrid=($editor22Case -eq 'browser-grid');panels=@{visuals=$false;settings=$true;console=$false;assets=$true;inspector=$true};visuals=@{enabled=$true;background=1;cubemap='environments/studio.hdr'}}
        Set-Content -LiteralPath $editor22Prefs -Value ($editor22Preference|ConvertTo-Json -Depth 5) -Encoding utf8
        $editor22Csv=Join-Path $editor22Out "$editor22Case.csv"
        $editor22Args=@('--fixed-editor-layout','--diagnostic-viewport','744','411','--editor-gradient')
        if($editor22Case -eq 'default'){$editor22Args+=@('--editor-benchmark',$editor22Csv,'--select-first')}
        elseif($editor22Case -eq 'materials'){$editor22Args+=@('--editor-benchmark',$editor22Csv,'--material-demo','--select-first')}
        else {
            $editor22Count=if($editor22Case -in @('100','500','1000')){$editor22Case}else{'500'}
            $editor22Args+=@('--render-benchmark',$editor22Count,$editor22Csv)
        }
        switch($editor22Case){
            'shadow-low' {$editor22Args+=@('--shadow-quality','0')}
            'shadow-medium' {$editor22Args+=@('--shadow-quality','1')}
            'shadow-high' {$editor22Args+=@('--shadow-quality','2')}
            'shadow-ultra' {$editor22Args+=@('--shadow-quality','3')}
            'sky-off' {$editor22Args+='--sky-off'}
            'sky-on' {$editor22Args+=@('--editor-cubemap','environments/studio.hdr')}
            'browser-grid' {$editor22Args+='--asset-grid'}
        }
        $editor22Args+='--select-first' # Select after scene setup, so Inspector measurements include actual components.
        $editor22Args+=$ExtraArguments
        & (Join-Path $editor22Build $Executable) @editor22Args *> (Join-Path $editor22Out "$editor22Case.log")
        if($LASTEXITCODE -ne 0){throw "$Phase/$editor22Case failed: $LASTEXITCODE"}
        $editor22Samples=Import-Csv -LiteralPath $editor22Csv
        if($editor22Samples.Count -ne 1200){throw "$Phase/${editor22Case}: incomplete samples"}
        if(@($editor22Samples | Where-Object {$_.viewportWidth -ne '744' -or $_.viewportHeight -ne '411'}).Count){throw "Mismatched viewport in $editor22Case"}
        Write-Output "$Phase/${editor22Case}: 1200 samples / 744x411 PASS"
    }
} finally {
    if($null -ne $editor22IniBytes){[IO.File]::WriteAllBytes($editor22Ini,$editor22IniBytes)}elseif(Test-Path -LiteralPath $editor22Ini){Remove-Item -LiteralPath $editor22Ini}
    if($null -ne $editor22PrefBytes){[IO.File]::WriteAllBytes($editor22Prefs,$editor22PrefBytes)}elseif(Test-Path -LiteralPath $editor22Prefs){Remove-Item -LiteralPath $editor22Prefs}
    Pop-Location
}
