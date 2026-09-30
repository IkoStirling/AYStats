[CmdletBinding()]
param(
    [ValidateRange(1,100)][int]$Samples=5,
    [switch]$Baseline,
    [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
$statsEngineRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$statsModuleRoot=Join-Path $statsEngineRoot 'AYRuntime/AYStats'
$statsRunId=[Guid]::NewGuid().ToString('N')
if(!$OutputDirectory){$OutputDirectory=Join-Path $statsEngineRoot "out/settlement-results/$statsRunId"}
$statsResults=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $statsResults | Out-Null
$statsSource='AYRuntime/AYStats'
$statsBuild='out/build/stats-core'
if($Baseline) {
    # Copy to a unique ignored sibling-depth path; never replace the live module or its build.
    $statsSource="out/settlement-baseline-$statsRunId"
    $statsSourceRoot=Join-Path $statsEngineRoot $statsSource
    New-Item -ItemType Directory -Path $statsSourceRoot | Out-Null
    foreach($statsPart in @('include','src','demo','CMakeLists.txt')) {
        Copy-Item -LiteralPath (Join-Path $statsModuleRoot $statsPart) -Destination $statsSourceRoot -Recurse
    }
    $statsArchive=Join-Path $statsModuleRoot 'docs/settlement-performance/before-source'
    foreach($statsPart in @('Stats','Batch')) {
        Copy-Item -LiteralPath (Join-Path $statsArchive "$statsPart.cpp.txt") -Destination (Join-Path $statsSourceRoot "src/$statsPart.cpp")
        Copy-Item -LiteralPath (Join-Path $statsArchive "$statsPart.h.txt") -Destination (Join-Path $statsSourceRoot "include/AYStats/$statsPart.h")
    }
    $statsBuild=Join-Path $env:TEMP "aystats-baseline-build-$statsRunId"
    & (Join-Path $PSScriptRoot 'build-local.ps1') -BuildDirectory $statsBuild -SourceDirectory $statsSource -Configure -Targets AYStats_SettlementProfileBenchmark,AYStats_SettlementAllocationBenchmark -ExtraConfigureArguments @('-DBUILD_TESTING=OFF','-DAY_STATS_BUILD_ASSETS=OFF','-DAY_STATS_BUILD_GPU=OFF')
} else {
    & (Join-Path $PSScriptRoot 'build-local.ps1') -BuildDirectory $statsBuild -Targets AYStats_SettlementProfileBenchmark,AYStats_SettlementAllocationBenchmark
}
$statsBuildRoot=if([IO.Path]::IsPathRooted($statsBuild)){$statsBuild}else{Join-Path $statsEngineRoot $statsBuild}
$statsLatencyExe=Join-Path $statsBuildRoot 'AYStats_SettlementProfileBenchmark.exe'
$statsAllocationExe=Join-Path $statsBuildRoot 'AYStats_SettlementAllocationBenchmark.exe'
& $statsLatencyExe $Samples | Set-Content (Join-Path $statsResults 'latency.csv')
if($LASTEXITCODE -ne 0){throw 'settlement latency benchmark failed'}
& $statsAllocationExe 1 | Set-Content (Join-Path $statsResults 'allocation.csv')
if($LASTEXITCODE -ne 0){throw 'settlement allocation benchmark failed'}
$statsFingerprints=foreach($statsExe in @($statsLatencyExe,$statsAllocationExe)) {
    [PSCustomObject]@{path=$statsExe;sha256=(Get-FileHash -LiteralPath $statsExe).Hash.ToLowerInvariant()}
}
[ordered]@{observedAtUtc=[DateTime]::UtcNow.ToString('o');baseline=[bool]$Baseline;samples=$Samples;source=$statsSource;build=$statsBuild;executables=$statsFingerprints} | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $statsResults 'run.json')
Write-Host "Settlement results: $statsResults"
