$ErrorActionPreference='Stop'
$tracy22Repo=Split-Path -Parent $PSScriptRoot
$tracy22Out=Join-Path $tracy22Repo 'out/diagnostics/editor22'
$tracy22Capture=Join-Path $tracy22Repo 'out/diagnostics/tracy-tools/tracy-capture.exe'
$tracy22Export=Join-Path $tracy22Repo 'out/diagnostics/tracy-tools/tracy-csvexport.exe'
if(!(Test-Path -LiteralPath $tracy22Capture)){throw 'Build/install matching Tracy 0.14.1 capture CLI first'}
foreach($tracy22Phase in @('before','after')){
    $tracy22Build=if($tracy22Phase -eq 'before'){'out/diagnostics/editor22/baseline-runtime'}else{'out/build/shadows-dx12'}
    $tracy22Executable=if($tracy22Phase -eq 'before'){'EditorBaseline.exe'}else{'WhispEngine.exe'}
    $tracy22Output=Join-Path $tracy22Out "$tracy22Phase-500.tracy"
    $tracy22Process=Start-Process -FilePath $tracy22Capture -ArgumentList @('-f','-s','8','-a','127.0.0.1','-o',('"'+$tracy22Output+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $tracy22Out "$tracy22Phase-tracy-capture.log") -RedirectStandardError (Join-Path $tracy22Out "$tracy22Phase-tracy-error.log")
    try{
        [void]$tracy22Process.Handle
        & "$PSScriptRoot/RunEditor22Diagnostics.ps1" -BuildDirectory $tracy22Build -Executable $tracy22Executable -Phase "tracy-$tracy22Phase" -Cases @('500') -ExtraArguments @('--wait-tracy')
        if(!$tracy22Process.WaitForExit(20000)){throw 'Tracy capture timeout'}
        $tracy22Process.WaitForExit();if($tracy22Process.ExitCode -ne 0){throw 'Tracy capture failed'}
        & $tracy22Export -f EditorLayer $tracy22Output > (Join-Path $tracy22Out "$tracy22Phase-tracy-editor.csv")
        if($LASTEXITCODE -ne 0){throw 'Tracy export failed'}
    }finally{if(!$tracy22Process.HasExited){$tracy22Process.Kill()}}
}
