param([string]$BuildDirectory='out/build/shadows-dx12',[string]$Executable='WhispEngine.exe',[string]$Phase='after',[string[]]$Cases=@('editor','shadows','edit-sky','play-sky'))
$ErrorActionPreference='Stop'
$capture25Repo=Split-Path -Parent $PSScriptRoot
$capture25Build=[IO.Path]::GetFullPath((Join-Path $capture25Repo $BuildDirectory))
$capture25Out=Join-Path $capture25Repo 'out/diagnostics/editor25'
$capture25Saved=@{}
foreach($capture25Name in @('imgui.ini','editor-preferences.json')){
    $capture25Path=Join-Path $capture25Build $capture25Name
    $capture25Saved[$capture25Name]=if(Test-Path -LiteralPath $capture25Path){[IO.File]::ReadAllBytes($capture25Path)}else{$null}
}
Push-Location $capture25Build
try {
    foreach($capture25Case in $Cases){
        $capture25Preference=@{version=1;grid=$true;lightGizmos=$false;projectileTestInput=$false;backgroundOverride=$false;panels=@{visuals=$false;settings=$true;assets=$true;inspector=$true;diagnostics=$false};visuals=@{enabled=$true;background=2;cubemap='environments/studio.hdr'}}
        Set-Content -LiteralPath (Join-Path $capture25Build 'editor-preferences.json') -Value ($capture25Preference|ConvertTo-Json -Depth 5) -Encoding utf8
        if($capture25Case -eq 'inspector'){
            $capture25Preference.panels.settings=$false;$capture25Preference.panels.assets=$false;$capture25Preference.panels.console=$false
            Set-Content -LiteralPath (Join-Path $capture25Build 'editor-preferences.json') -Value ($capture25Preference|ConvertTo-Json -Depth 5) -Encoding utf8
            $capture25InspectorLayout=@'
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
            Set-Content -LiteralPath (Join-Path $capture25Build 'imgui.ini') -Value $capture25InspectorLayout -Encoding utf8
        }
        $capture25Image=Join-Path $capture25Out "$Phase-$capture25Case.ppm"
        $capture25Args=switch($capture25Case){
            'primitives' {@('--primitive-demo','--editor-gradient','--scene-background-panel')}
            'day' {@('--primitive-demo','--daynight-demo','20','--aa-quality','2','--scene-background-panel')}
            'sunset' {@('--primitive-demo','--daynight-demo','0','--aa-quality','2','--scene-background-panel')}
            'night' {@('--primitive-demo','--daynight-demo','-25','--aa-quality','2','--scene-background-panel')}
            'known-sun' {@('--primitive-demo','--known-sun-demo','--aa-quality','2','--scene-background-panel')}
            'game' {@('--primitive-demo','--daynight-demo','20','--aa-quality','2','--game-view')}
            'gradient' {@('--editor2-demo','--editor-gradient','--select-first','--scene-background-panel')}
            'inspector' {@('--fixed-editor-layout','--pbr-textures-demo','--editor-gradient','--select-first')}
            'editor' {@('--editor2-demo','--editor-cubemap','environments/studio.hdr','--select-first','--scene-background-panel')}
            'shadows' {@('--editor2-demo','--shadow-demo','--select-first','--scene-background-panel')}
            'edit-sky' {@('--render-benchmark','100',(Join-Path $capture25Out "$Phase-capture-samples.csv"),'--editor-cubemap','environments/studio.hdr','--select-first')}
            'play-sky' {@('--stress','100','--editor-cubemap','environments/studio.hdr','--select-first')}
        }
        $capture25Args+=@('--frames','180','--capture-frame',$capture25Image)
        & (Join-Path $capture25Build $Executable) @capture25Args *> (Join-Path $capture25Out "$Phase-$capture25Case.log")
        if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $capture25Image)){throw "Capture $Phase/$capture25Case failed"}
        Write-Output "$Phase/$capture25Case saved"
    }
} finally {
    foreach($capture25Name in @('imgui.ini','editor-preferences.json')){
        $capture25Path=Join-Path $capture25Build $capture25Name
        if($null -ne $capture25Saved[$capture25Name]){[IO.File]::WriteAllBytes($capture25Path,$capture25Saved[$capture25Name])}elseif(Test-Path -LiteralPath $capture25Path){Remove-Item -LiteralPath $capture25Path}
    }
    Pop-Location
}
