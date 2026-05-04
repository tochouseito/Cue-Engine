param(
    [string]$Version = "",
    [string]$TagName = "",
    [string]$Repository = "",
    [string]$Channel = "stable",
    [string]$PackageRoot = "generated/packaged_editor",
    [string]$ArtifactRoot = "generated/release",
    [string]$BuildDirectory = "out/build/win-x64",
    [string]$EditorConfiguration = "RelWithDebInfo",
    [string]$SdkConfiguration = "Release",
    [string]$UpdaterConfiguration = "Release",
    [string]$TargetTriplet = "x64-windows-static-md",
    [string]$HostTriplet = "x64-windows",
    [switch]$InstallVcpkgDependencies,
    [switch]$ForceConfigure,
    [switch]$SkipPackageBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Resolve-Version
{
    param(
        [string]$ExplicitVersion,
        [string]$ExplicitTag
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitVersion))
    {
        return $ExplicitVersion.TrimStart("v")
    }

    if (-not [string]::IsNullOrWhiteSpace($ExplicitTag))
    {
        return $ExplicitTag.TrimStart("v")
    }

    $vcpkgManifest = Join-Path $repoRoot "vcpkg.json"
    if (Test-Path -LiteralPath $vcpkgManifest)
    {
        $manifest = Get-Content -LiteralPath $vcpkgManifest -Raw | ConvertFrom-Json
        if ($manifest.PSObject.Properties.Name -contains "version-string" -and
            -not [string]::IsNullOrWhiteSpace($manifest."version-string"))
        {
            return [string]$manifest."version-string"
        }
    }

    throw "Version を解決できません。-Version または -TagName を指定してください。"
}

function Convert-ToForwardSlashPath
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return $Path.Replace("\", "/")
}

function New-Sha256
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$resolvedVersion = Resolve-Version -ExplicitVersion $Version -ExplicitTag $TagName
$resolvedTag = if ([string]::IsNullOrWhiteSpace($TagName))
{
    "v$resolvedVersion"
}
else
{
    $TagName
}

$platform = "win-x64"
$packageName = "CueEngineEditor_$resolvedVersion`_$platform"
$zipName = "$packageName.zip"
$manifestName = "$packageName.json"
$packagePath = Join-Path $repoRoot $PackageRoot
$artifactPath = Join-Path $repoRoot $ArtifactRoot
$zipPath = Join-Path $artifactPath $zipName
$manifestPath = Join-Path $artifactPath $manifestName
$updaterName = "CueUpdater_$resolvedVersion`_$platform.exe"
$updaterOutputPath = Join-Path $repoRoot "generated/outputs/Updater/$UpdaterConfiguration/CueUpdater.exe"
$updaterArtifactPath = Join-Path $artifactPath $updaterName
$versionManifestPath = Join-Path $packagePath "version.json"

if (-not $SkipPackageBuild)
{
    & (Join-Path $PSScriptRoot "package_editor.ps1") `
        -BuildDirectory $BuildDirectory `
        -EditorConfiguration $EditorConfiguration `
        -SdkConfiguration $SdkConfiguration `
        -OutputRoot $PackageRoot `
        -TargetTriplet $TargetTriplet `
        -HostTriplet $HostTriplet `
        -InstallVcpkgDependencies:$InstallVcpkgDependencies `
        -ForceConfigure:$ForceConfigure
    if ($LASTEXITCODE -ne 0)
    {
        throw "package_editor.ps1 に失敗しました。"
    }
}

if (-not (Test-Path -LiteralPath $packagePath))
{
    throw "package_editor の出力が見つかりません: $packagePath"
}

if (Test-Path -LiteralPath $artifactPath)
{
    Remove-Item -LiteralPath $artifactPath -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $artifactPath | Out-Null

if (-not $SkipPackageBuild)
{
    cmake --build $BuildDirectory --config $UpdaterConfiguration --target CueUpdater
    if ($LASTEXITCODE -ne 0)
    {
        throw "CueUpdater のビルドに失敗しました。"
    }
}

if (Test-Path -LiteralPath $updaterOutputPath)
{
    Copy-Item -LiteralPath $updaterOutputPath -Destination $updaterArtifactPath -Force
}
else
{
    Write-Warning "CueUpdater.exe が見つからないため、Release asset には含めません: $updaterOutputPath"
}

$createdAtUtc = (Get-Date).ToUniversalTime().ToString("o")
$versionManifest = [ordered]@{
    version = $resolvedVersion
    channel = $Channel
    platform = $platform
    package = "CueEngineEditor"
    createdAtUtc = $createdAtUtc
}
$versionManifest |
    ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $versionManifestPath -Encoding utf8

if (Test-Path -LiteralPath $zipPath)
{
    Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive `
    -Path (Join-Path $packagePath "*") `
    -DestinationPath $zipPath `
    -Force

$zipItem = Get-Item -LiteralPath $zipPath
$downloadUrl = $null
if (-not [string]::IsNullOrWhiteSpace($Repository))
{
    $downloadUrl =
        "https://github.com/$Repository/releases/download/$resolvedTag/$zipName"
}

$releaseManifest = [ordered]@{
    version = $resolvedVersion
    channel = $Channel
    platform = $platform
    package = "CueEngineEditor"
    tag = $resolvedTag
    assetName = $zipName
    downloadUrl = $downloadUrl
    sha256 = New-Sha256 -Path $zipPath
    sizeBytes = $zipItem.Length
    minimumUpdaterVersion = "0.1.0"
    createdAtUtc = $createdAtUtc
    installRootEntries = @(
        "Editor",
        "Sdk",
        "version.json"
    )
}

$releaseManifest |
    ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Host "[CueEditorRelease] package: $(Convert-ToForwardSlashPath -Path $zipPath)"
Write-Host "[CueEditorRelease] manifest: $(Convert-ToForwardSlashPath -Path $manifestPath)"
