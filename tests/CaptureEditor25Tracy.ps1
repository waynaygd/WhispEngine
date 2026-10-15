param([string]$BuildDirectory='out/build/shadows-dx12')
$ErrorActionPreference='Stop'
$trace25Repo=Split-Path -Parent $PSScriptRoot
$trace25Build=[IO.Path]::GetFullPath((Join-Path $trace25Repo $BuildDirectory))
$trace25Out=Join-Path $trace25Repo 'out/diagnostics/editor25'
$trace25Saved=@{}
foreach($name in @('imgui.ini','editor-preferences.json')){
    $path=Join-Path $trace25Build $name
    $trace25Saved[$name]=if(Test-Path -LiteralPath $path){[IO.File]::ReadAllBytes($path)}else{$null}
}
$trace25Capture=$null
Push-Location $trace25Build
try {
    $capturePath=Join-Path $trace25Out 'aa500.tracy'
    $captureExe=Join-Path $trace25Repo 'out/diagnostics/tracy-tools/tracy-capture.exe'
    $trace25Capture=Start-Process -FilePath $captureExe -ArgumentList @('-o',('"'+$capturePath+'"'),'-a','127.0.0.1','-s','30','-f') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $trace25Out 'tracy-capture.log') -RedirectStandardError (Join-Path $trace25Out 'tracy-capture-error.log')
    & (Join-Path $trace25Build 'WhispEngine.exe') --render-benchmark 500 (Join-Path $trace25Out 'tracy-500.csv') --diagnostic-viewport 744 411 --editor-gradient --aa-quality 2 --wait-tracy *> (Join-Path $trace25Out 'tracy-engine.log')
    if($LASTEXITCODE -ne 0){throw 'Tracy engine run failed'}
    if(!$trace25Capture.WaitForExit(50000)){throw 'Tracy capture did not finish within bounded wait'}
    if($trace25Capture.ExitCode -ne 0){throw 'Tracy capture failed'}
    & (Join-Path $trace25Repo 'out/diagnostics/tracy-tools/tracy-csvexport.exe') $capturePath > (Join-Path $trace25Out 'tracy-zones.csv')
    if($LASTEXITCODE -ne 0){throw 'Tracy export failed'}
} finally {
    if($null -ne $trace25Capture -and !$trace25Capture.HasExited){$trace25Capture.Kill()}
    foreach($name in @('imgui.ini','editor-preferences.json')){
        $path=Join-Path $trace25Build $name
        if($null -ne $trace25Saved[$name]){[IO.File]::WriteAllBytes($path,$trace25Saved[$name])}elseif(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}
    }
    Pop-Location
}
