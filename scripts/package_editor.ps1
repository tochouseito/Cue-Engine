param(
    [string]$BuildDirectory = "out/build/win-x64",
    [string]$EditorConfiguration = "RelWithDebInfo",
    [string]$SdkConfiguration = "Release",
    [string]$OutputRoot = "generated/packaged_editor",
    [string]$TargetTriplet = "x64-windows-static-md",
    [string]$HostTriplet = "x64-windows",
    [switch]$InstallVcpkgDependencies,
    [switch]$ForceConfigure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Invoke-Step
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Action
    )

    Write-Host "[CueEditorPackage] $Message"
    & $Action
}

function Assert-PathExists
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path))
    {
        throw "$Description が見つかりません: $Path"
    }
}

function Copy-Path
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,
        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    Assert-PathExists -Path $Source -Description "コピー元"

    $destinationParent = Split-Path -Parent $Destination
    if (-not [string]::IsNullOrEmpty($destinationParent))
    {
        New-Item -ItemType Directory -Force -Path $destinationParent | Out-Null
    }

    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}

function Get-CMakeCacheValue
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$CachePath,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $CachePath))
    {
        return $null
    }

    $escapedName = [regex]::Escape($Name)
    $line = Get-Content -LiteralPath $CachePath |
        Where-Object { $_ -match "^${escapedName}:[^=]*=" } |
        Select-Object -First 1
    if ($null -eq $line)
    {
        return $null
    }

    return ($line -replace "^${escapedName}:[^=]*=", "")
}

function Resolve-MSBuildExe
{
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\17\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\17\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\17\Community\MSBuild\Current\Bin\MSBuild.exe"
    )

    foreach ($candidate in $candidates)
    {
        if (Test-Path -LiteralPath $candidate)
        {
            return $candidate
        }
    }

    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere)
    {
        $installPath = & $vswhere -latest -products * -property installationPath
        if (-not [string]::IsNullOrWhiteSpace($installPath))
        {
            $candidate = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate)
            {
                return $candidate
            }
        }
    }

    throw "MSBuild.exe が見つかりません。Visual Studio / Build Tools を確認してください。"
}

function Invoke-MSBuildProject
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,
        [Parameter(Mandatory = $true)]
        [string]$Configuration
    )

    Assert-PathExists -Path $ProjectPath -Description "MSBuild project"

    & $script:msbuildExe $ProjectPath `
        "/p:Configuration=$Configuration" `
        "/p:Platform=x64" `
        "/p:BuildProjectReferences=false" `
        "/nologo" `
        "/v:m" `
        "/clp:Summary"
    if ($LASTEXITCODE -ne 0)
    {
        throw "MSBuild に失敗しました: $ProjectPath"
    }
}

$editorOutput = Join-Path $repoRoot "generated/outputs/Editor/$EditorConfiguration"
$appOutput = Join-Path $repoRoot "generated/outputs/App/$SdkConfiguration"
$sdkLibOutput = Join-Path $repoRoot "generated/outputs/Sdk/Lib/$SdkConfiguration"
$packageRoot = Join-Path $repoRoot $OutputRoot
$packageEditorRoot = Join-Path $packageRoot "Editor"
$packageSdkRoot = Join-Path $packageRoot "Sdk"
$cmakeCachePath = Join-Path $repoRoot (Join-Path $BuildDirectory "CMakeCache.txt")
$buildRoot = Join-Path $repoRoot $BuildDirectory
$editorProjectPath = Join-Path $buildRoot "Engine/Source/Editor/Editor.vcxproj"
$msbuildExe = Resolve-MSBuildExe
$configuredTargetTriplet = Get-CMakeCacheValue `
    -CachePath $cmakeCachePath `
    -Name "VCPKG_TARGET_TRIPLET"
$shouldConfigure = $ForceConfigure -or
    -not (Test-Path -LiteralPath $cmakeCachePath) -or
    $configuredTargetTriplet -ne $TargetTriplet -or
    -not (Test-Path -LiteralPath $editorProjectPath)

if ($shouldConfigure)
{
    Invoke-Step -Message "CMake configure を更新します。" -Action {
        if ([string]::IsNullOrWhiteSpace($env:VCPKG_ROOT))
        {
            throw "VCPKG_ROOT が設定されていません。"
        }

        $toolchainPath = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
        Assert-PathExists -Path $toolchainPath -Description "vcpkg toolchain"

        $manifestInstallValue = if ($InstallVcpkgDependencies) { "ON" } else { "OFF" }

        cmake -S . -B $BuildDirectory `
            "-DCMAKE_TOOLCHAIN_FILE=$toolchainPath" `
            "-DVCPKG_OVERLAY_TRIPLETS=$repoRoot/config/vcpkg/triplets" `
            "-DVCPKG_TARGET_TRIPLET=$TargetTriplet" `
            "-DVCPKG_HOST_TRIPLET=$HostTriplet" `
            "-DVCPKG_MANIFEST_INSTALL=$manifestInstallValue"
        if ($LASTEXITCODE -ne 0)
        {
            throw "CMake configure に失敗しました。"
        }
    }
}
else
{
    Invoke-Step -Message "既存の CMake build tree を使用します。" -Action {
        Write-Host "[CueEditorPackage] $cmakeCachePath"
    }
}

Invoke-Step -Message "Editor を $EditorConfiguration でビルドします。" -Action {
    Invoke-MSBuildProject `
        -ProjectPath $editorProjectPath `
        -Configuration $EditorConfiguration
}

Invoke-Step -Message "SDK 用ライブラリを $SdkConfiguration でビルドします。" -Action {
    $sdkProjects = @(
        "Engine/Source/Runtime/Base/Base.vcxproj",
        "Engine/Source/Runtime/Math/CueMath.vcxproj",
        "Engine/Source/Runtime/Core/Core.vcxproj",
        "Engine/Source/Runtime/Audio/Audio.vcxproj",
        "Engine/Source/Runtime/PAL/PAL.vcxproj",
        "Engine/Source/Runtime/PAL/Win/win_platform.vcxproj",
        "Engine/Source/Runtime/Audio/XAudio2/xaudio2_backend.vcxproj",
        "Engine/Source/Runtime/ECS/ECS.vcxproj",
        "Engine/Source/Runtime/Physics/Physics.vcxproj",
        "Engine/Source/Runtime/RHI/RHI.vcxproj",
        "Engine/Source/Runtime/Engine/Engine.vcxproj",
        "Engine/Source/Runtime/Physics/Jolt/jolt_physics_backend.vcxproj",
        "Engine/Source/Runtime/RHI/D3D12/d3d12_backend.vcxproj",
        "Engine/Source/App/CueApp.vcxproj"
    )

    foreach ($project in $sdkProjects)
    {
        Invoke-MSBuildProject `
            -ProjectPath (Join-Path $buildRoot $project) `
            -Configuration $SdkConfiguration
    }
}

Invoke-Step -Message "既存の packaged_editor を削除します。" -Action {
    if (Test-Path -LiteralPath $packageRoot)
    {
        Remove-Item -LiteralPath $packageRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $packageEditorRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $packageSdkRoot | Out-Null
}

Invoke-Step -Message "Editor 実行物を staging します。" -Action {
    Copy-Path -Source (Join-Path $editorOutput "Editor.exe") `
        -Destination (Join-Path $packageEditorRoot "Editor.exe")

    $editorDlls = @(
        "dxcompiler.dll",
        "dxil.dll"
    )
    foreach ($dllName in $editorDlls)
    {
        Copy-Path -Source (Join-Path $editorOutput $dllName) `
            -Destination (Join-Path $packageEditorRoot $dllName)
    }

    Copy-Path -Source (Join-Path $editorOutput "EngineResources") `
        -Destination (Join-Path $packageEditorRoot "EngineResources")
    Copy-Path -Source (Join-Path $editorOutput "config") `
        -Destination (Join-Path $packageEditorRoot "config")
}

Invoke-Step -Message "SDK を staging します。" -Action {
    Copy-Path -Source (Join-Path $repoRoot "Engine/Source/App") `
        -Destination (Join-Path $packageSdkRoot "Engine/Source/App")
    Copy-Path -Source (Join-Path $repoRoot "Engine/Source/Runtime") `
        -Destination (Join-Path $packageSdkRoot "Engine/Source/Runtime")
    Copy-Path -Source (Join-Path $repoRoot "Tools/CMake") `
        -Destination (Join-Path $packageSdkRoot "Tools/CMake")
    Copy-Path -Source (Join-Path $repoRoot "config/vcpkg/triplets") `
        -Destination (Join-Path $packageSdkRoot "config/vcpkg/triplets")
    Copy-Path -Source $sdkLibOutput `
        -Destination (Join-Path $packageSdkRoot "generated/outputs/Sdk/Lib/$SdkConfiguration")
    Copy-Path -Source (Join-Path $repoRoot "vcpkg.json") `
        -Destination (Join-Path $packageSdkRoot "vcpkg.json")
    Copy-Path -Source (Join-Path $repoRoot "vcpkg-configuration.json") `
        -Destination (Join-Path $packageSdkRoot "vcpkg-configuration.json")
}

Write-Host "[CueEditorPackage] packaged_editor を作成しました: $packageRoot"
