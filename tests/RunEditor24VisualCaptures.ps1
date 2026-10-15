param([string]$BuildDirectory='out/build/shadows-dx12',[string]$Executable='WhispEngine.exe',[string]$Phase='after',[string[]]$Cases=@('editor','shadows','edit-sky','play-sky'))
$ErrorActionPreference='Stop'
$capture24Repo=Split-Path -Parent $PSScriptRoot
$capture24Build=[IO.Path]::GetFullPath((Join-Path $capture24Repo $BuildDirectory))
$capture24Out=Join-Path $capture24Repo 'out/diagnostics/editor24'
$capture24Saved=@{}
foreach($capture24Name in @('imgui.ini','editor-preferences.json')){
    $capture24Path=Join-Path $capture24Build $capture24Name
    $capture24Saved[$capture24Name]=if(Test-Path -LiteralPath $capture24Path){[IO.File]::ReadAllBytes($capture24Path)}else{$null}
}
Push-Location $capture24Build
try {
    foreach($capture24Case in $Cases){
        $capture24Preference=@{version=1;grid=$true;lightGizmos=$false;projectileTestInput=$false;backgroundOverride=$false;panels=@{visuals=$false;settings=$true;assets=$true;inspector=$true;diagnostics=$false};visuals=@{enabled=$true;background=2;cubemap='environments/studio.hdr'}}
        Set-Content -LiteralPath (Join-Path $capture24Build 'editor-preferences.json') -Value ($capture24Preference|ConvertTo-Json -Depth 5) -Encoding utf8
        if($capture24Case -eq 'inspector'){
            $capture24Preference.panels.settings=$false;$capture24Preference.panels.assets=$false;$capture24Preference.panels.console=$false
            Set-Content -LiteralPath (Join-Path $capture24Build 'editor-preferences.json') -Value ($capture24Preference|ConvertTo-Json -Depth 5) -Encoding utf8
            $capture24InspectorLayout=@'
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
            Set-Content -LiteralPath (Join-Path $capture24Build 'imgui.ini') -Value $capture24InspectorLayout -Encoding utf8
        }
        $capture24Image=Join-Path $capture24Out "$Phase-$capture24Case.ppm"
        $capture24Args=switch($capture24Case){
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
            'edit-sky' {@('--render-benchmark','100',(Join-Path $capture24Out "$Phase-capture-samples.csv"),'--editor-cubemap','environments/studio.hdr','--select-first')}
            'play-sky' {@('--stress','100','--editor-cubemap','environments/studio.hdr','--select-first')}
        }
        $capture24Args+=@('--frames','180','--capture-frame',$capture24Image)
        & (Join-Path $capture24Build $Executable) @capture24Args *> (Join-Path $capture24Out "$Phase-$capture24Case.log")
        if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $capture24Image)){throw "Capture $Phase/$capture24Case failed"}
        Write-Output "$Phase/$capture24Case saved"
    }
} finally {
    foreach($capture24Name in @('imgui.ini','editor-preferences.json')){
        $capture24Path=Join-Path $capture24Build $capture24Name
        if($null -ne $capture24Saved[$capture24Name]){[IO.File]::WriteAllBytes($capture24Path,$capture24Saved[$capture24Name])}elseif(Test-Path -LiteralPath $capture24Path){Remove-Item -LiteralPath $capture24Path}
    }
    Pop-Location
}
