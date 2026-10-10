param(
    [string]$BuildDirectory = "$PSScriptRoot/../out/build/x64-release",
    [string]$ArtifactDirectory = "$PSScriptRoot/../out/diagnostics",
    [int]$LifecycleRuns = 6
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path $BuildDirectory).Path
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$artifacts = (Resolve-Path $ArtifactDirectory).Path
$results = @()
Push-Location $build
try {
    function Invoke-EngineDiagnostic([string]$Name, [string[]]$EngineArguments) {
        & ./WhispEngine.exe @EngineArguments *> (Join-Path $artifacts "$Name.log")
        $code = $LASTEXITCODE
        Write-Host "$Name exit=$code"
        return [pscustomobject]@{ Name=$Name; ExitCode=$code }
    }
    for ($i=1; $i -le $LifecycleRuns; ++$i) {
        $name = "lifecycle-$i"
        $results += Invoke-EngineDiagnostic $name @('--stability-scenario', (Join-Path $artifacts "$name.csv"))
    }
    foreach ($mode in @('Serial','Parallel')) {
        foreach ($frames in @(5,30,120)) {
            $name = "close-$mode-$frames"
            $arguments = @('--stress','500','--frames',"$frames",'--diagnostics',(Join-Path $artifacts "$name.csv"))
            if ($mode -eq 'Serial') { $arguments += '--serial' }
            $results += Invoke-EngineDiagnostic $name $arguments
        }
        $name = "visual-$mode-5000"
        $arguments = @('--stress','500','--frames','5000','--diagnostics',(Join-Path $artifacts "$name.csv"))
        if ($mode -eq 'Serial') { $arguments += '--serial' }
        $results += Invoke-EngineDiagnostic $name $arguments
    }
} finally { Pop-Location }
$results | Export-Csv (Join-Path $artifacts 'engine-exits.csv') -NoTypeInformation
if ($results | Where-Object ExitCode -ne 0) { throw 'At least one engine run failed; inspect engine-exits.csv and logs.' }
