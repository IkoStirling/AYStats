[CmdletBinding()]
param(
    [string]$BuildDirectory='out/build/stats-core',
    [string]$Configuration='Release',
    [string[]]$Targets=@('AYStats_UnitTests'),
    [ValidateRange(1,64)][int]$Parallel=4,
    [switch]$Configure,
    [string]$SourceDirectory='AYRuntime/AYStats',
    [string[]]$ExtraConfigureArguments=@()
)
$ErrorActionPreference='Stop'
$Targets=@($Targets | ForEach-Object { $_ -split ',' })
$engineRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$statsVsRoot='E:/MySoftware/vs2026insider'
$statsCompilerRoot=Get-ChildItem -LiteralPath "$statsVsRoot/VC/Tools/MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
$statsSdkRoot=((Get-ItemProperty 'HKLM:/SOFTWARE/Microsoft/Windows Kits/Installed Roots').KitsRoot10).Replace('\','/').TrimEnd('/')
$statsSdkVersion=Get-ChildItem -LiteralPath (Join-Path $statsSdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty Name
$env:INCLUDE="$statsCompilerRoot/include;$statsSdkRoot/Include/$statsSdkVersion/ucrt;$statsSdkRoot/Include/$statsSdkVersion/shared;$statsSdkRoot/Include/$statsSdkVersion/um;$statsSdkRoot/Include/$statsSdkVersion/winrt"
$env:LIB="$statsCompilerRoot/lib/x64;$statsSdkRoot/Lib/$statsSdkVersion/ucrt/x64;$statsSdkRoot/Lib/$statsSdkVersion/um/x64"
$env:PATH="$statsCompilerRoot/bin/Hostx64/x64;$statsSdkRoot/bin/$statsSdkVersion/x64;$statsVsRoot/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja;$env:PATH"
$env:VSLANG='1033'
Push-Location $engineRoot
try {
    if($Configure) {
        & cmake -S $SourceDirectory -B $BuildDirectory -G Ninja "-DCMAKE_CXX_COMPILER=$statsCompilerRoot/bin/Hostx64/x64/cl.exe" "-DCMAKE_MT=$statsSdkRoot/bin/$statsSdkVersion/x64/mt.exe" "-DCMAKE_RC_COMPILER=$statsSdkRoot/bin/$statsSdkVersion/x64/rc.exe" "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_PREFIX_PATH=$engineRoot/out/build/vcpkg_installed/x64-windows" @ExtraConfigureArguments
        if($LASTEXITCODE -ne 0){throw 'AYStats configure failed'}
    }
    & cmake --build $BuildDirectory --target @Targets --parallel $Parallel
    if($LASTEXITCODE -ne 0){throw 'AYStats build failed'}
} finally {Pop-Location}
