param([string]$BuildDirectory='out/build/shadows-dx12',[string]$Executable='WhispEngine.exe',[string]$Phase='after',[string[]]$Cases=@('editor','shadows','edit-sky','play-sky'))
$ErrorActionPreference='Stop'
$capture22Repo=Split-Path -Parent $PSScriptRoot
$capture22Build=[IO.Path]::GetFullPath((Join-Path $capture22Repo $BuildDirectory))
$capture22Out=Join-Path $capture22Repo 'out/diagnostics/editor22'
$capture22Saved=@{}
foreach($capture22Name in @('imgui.ini','editor-preferences.json')){
    $capture22Path=Join-Path $capture22Build $capture22Name
    $capture22Saved[$capture22Name]=if(Test-Path -LiteralPath $capture22Path){[IO.File]::ReadAllBytes($capture22Path)}else{$null}
}
Push-Location $capture22Build
try {
    foreach($capture22Case in $Cases){
        $capture22Preference=@{version=1;grid=$true;lightGizmos=$false;projectileTestInput=$false;backgroundOverride=$false;panels=@{visuals=$false;settings=$true;assets=$true;inspector=$true;diagnostics=$false};visuals=@{enabled=$true;background=2;cubemap='environments/studio.hdr'}}
        Set-Content -LiteralPath (Join-Path $capture22Build 'editor-preferences.json') -Value ($capture22Preference|ConvertTo-Json -Depth 5) -Encoding utf8
        if($capture22Case -eq 'inspector'){
            $capture22Preference.panels.settings=$false;$capture22Preference.panels.assets=$false;$capture22Preference.panels.console=$false
            Set-Content -LiteralPath (Join-Path $capture22Build 'editor-preferences.json') -Value ($capture22Preference|ConvertTo-Json -Depth 5) -Encoding utf8
            $capture22InspectorLayout=@'
[Window][Viewport]
Pos=200,60
Size=760,550
Collapsed=0

[Window][Scene Hierarchy]
Pos=0,60
Size=200,550
Collapsed=0

[Window][Inspector]
Pos=960,60
Size=320,660
Collapsed=0

[Window][Statistics]
Pos=0,620
Size=200,100
Collapsed=1

[Window][Material Editor]
Pos=220,620
Size=200,100
Collapsed=1

[Window][Physics Stress Test]
Pos=440,620
Size=220,100
Collapsed=1
'@
            Set-Content -LiteralPath (Join-Path $capture22Build 'imgui.ini') -Value $capture22InspectorLayout -Encoding utf8
        }
        $capture22Image=Join-Path $capture22Out "$Phase-$capture22Case.ppm"
        $capture22Args=switch($capture22Case){
            'gradient' {@('--editor2-demo','--editor-gradient','--select-first','--scene-background-panel')}
            'inspector' {@('--fixed-editor-layout','--pbr-textures-demo','--editor-gradient','--select-first')}
            'editor' {@('--editor2-demo','--editor-cubemap','environments/studio.hdr','--select-first','--scene-background-panel')}
            'shadows' {@('--editor2-demo','--shadow-demo','--select-first','--scene-background-panel')}
            'edit-sky' {@('--render-benchmark','100',(Join-Path $capture22Out "$Phase-capture-samples.csv"),'--editor-cubemap','environments/studio.hdr','--select-first')}
            'play-sky' {@('--stress','100','--editor-cubemap','environments/studio.hdr','--select-first')}
        }
        $capture22Args+=@('--frames','180','--capture-frame',$capture22Image)
        & (Join-Path $capture22Build $Executable) @capture22Args *> (Join-Path $capture22Out "$Phase-$capture22Case.log")
        if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $capture22Image)){throw "Capture $Phase/$capture22Case failed"}
        Write-Output "$Phase/$capture22Case saved"
    }
} finally {
    foreach($capture22Name in @('imgui.ini','editor-preferences.json')){
        $capture22Path=Join-Path $capture22Build $capture22Name
        if($null -ne $capture22Saved[$capture22Name]){[IO.File]::WriteAllBytes($capture22Path,$capture22Saved[$capture22Name])}elseif(Test-Path -LiteralPath $capture22Path){Remove-Item -LiteralPath $capture22Path}
    }
    Pop-Location
}
