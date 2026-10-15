param(
    [string]$BuildDirectory='out/build/x64-release',
    [string]$CTest='ctest',
    [switch]$UseCurrentLayout
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$build=[IO.Path]::GetFullPath((Join-Path $repo $BuildDirectory))
if(-not $build.StartsWith($repo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
    throw 'BuildDirectory must stay inside the repository'
}
if(-not (Test-Path -LiteralPath (Join-Path $build 'CTestTestfile.cmake'))){throw 'Configure and build the test targets first'}
$fixture=Join-Path $PSScriptRoot 'fixtures/lua-diagnostics'
$saved=@{}
foreach($name in @('imgui.ini','editor-preferences.json')){
    $path=Join-Path $build $name
    $saved[$name]=if(Test-Path -LiteralPath $path){[IO.File]::ReadAllBytes($path)}else{$null}
}
try{
    # Synthetic pointer/pixel tests require a known layout, not a developer's last desktop.
    if(-not $UseCurrentLayout){
        foreach($name in $saved.Keys){Copy-Item -LiteralPath (Join-Path $fixture $name) -Destination (Join-Path $build $name)}
    }
    & $CTest --test-dir $build --output-on-failure --timeout 180
    $testExit=$LASTEXITCODE
}finally{
    foreach($name in $saved.Keys){
        $path=Join-Path $build $name
        if($null -ne $saved[$name]){[IO.File]::WriteAllBytes($path,$saved[$name])}
        elseif(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}
    }
}
exit $testExit
