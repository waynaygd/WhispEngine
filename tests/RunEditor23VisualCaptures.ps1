param([string]$BuildDirectory='out/build/shadows-dx12',[string]$Executable='WhispEngine.exe',[string]$Phase='after',[string[]]$Cases=@('editor','shadows','edit-sky','play-sky'))
$ErrorActionPreference='Stop'
$capture23Repo=Split-Path -Parent $PSScriptRoot
$capture23Build=[IO.Path]::GetFullPath((Join-Path $capture23Repo $BuildDirectory))
$capture23Out=Join-Path $capture23Repo 'out/diagnostics/editor23'
$capture23Saved=@{}
foreach($capture23Name in @('imgui.ini','editor-preferences.json')){
    $capture23Path=Join-Path $capture23Build $capture23Name
    $capture23Saved[$capture23Name]=if(Test-Path -LiteralPath $capture23Path){[IO.File]::ReadAllBytes($capture23Path)}else{$null}
}
Push-Location $capture23Build
try {
    foreach($capture23Case in $Cases){
        $capture23Preference=@{version=1;grid=$true;lightGizmos=$false;projectileTestInput=$false;backgroundOverride=$false;panels=@{visuals=$false;settings=$true;assets=$true;inspector=$true;diagnostics=$false};visuals=@{enabled=$true;background=2;cubemap='environments/studio.hdr'}}
        Set-Content -LiteralPath (Join-Path $capture23Build 'editor-preferences.json') -Value ($capture23Preference|ConvertTo-Json -Depth 5) -Encoding utf8
        if($capture23Case -eq 'inspector'){
            $capture23Preference.panels.settings=$false;$capture23Preference.panels.assets=$false;$capture23Preference.panels.console=$false
            Set-Content -LiteralPath (Join-Path $capture23Build 'editor-preferences.json') -Value ($capture23Preference|ConvertTo-Json -Depth 5) -Encoding utf8
            $capture23InspectorLayout=@'
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
            Set-Content -LiteralPath (Join-Path $capture23Build 'imgui.ini') -Value $capture23InspectorLayout -Encoding utf8
        }
        $capture23Image=Join-Path $capture23Out "$Phase-$capture23Case.ppm"
        $capture23Args=switch($capture23Case){
            'primitives' {@('--primitive-demo','--editor-gradient','--scene-background-panel')}
            'gradient' {@('--editor2-demo','--editor-gradient','--select-first','--scene-background-panel')}
            'inspector' {@('--fixed-editor-layout','--pbr-textures-demo','--editor-gradient','--select-first')}
            'editor' {@('--editor2-demo','--editor-cubemap','environments/studio.hdr','--select-first','--scene-background-panel')}
            'shadows' {@('--editor2-demo','--shadow-demo','--select-first','--scene-background-panel')}
            'edit-sky' {@('--render-benchmark','100',(Join-Path $capture23Out "$Phase-capture-samples.csv"),'--editor-cubemap','environments/studio.hdr','--select-first')}
            'play-sky' {@('--stress','100','--editor-cubemap','environments/studio.hdr','--select-first')}
        }
        $capture23Args+=@('--frames','180','--capture-frame',$capture23Image)
        & (Join-Path $capture23Build $Executable) @capture23Args *> (Join-Path $capture23Out "$Phase-$capture23Case.log")
        if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $capture23Image)){throw "Capture $Phase/$capture23Case failed"}
        Write-Output "$Phase/$capture23Case saved"
    }
} finally {
    foreach($capture23Name in @('imgui.ini','editor-preferences.json')){
        $capture23Path=Join-Path $capture23Build $capture23Name
        if($null -ne $capture23Saved[$capture23Name]){[IO.File]::WriteAllBytes($capture23Path,$capture23Saved[$capture23Name])}elseif(Test-Path -LiteralPath $capture23Path){Remove-Item -LiteralPath $capture23Path}
    }
    Pop-Location
}
