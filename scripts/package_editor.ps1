param(
    [string]$BuildDirectory = "out/build/win-x64",
    [string]$EditorConfiguration = "RelWithDebInfo",
    [string]$SdkConfiguration = "Release",
    [string]$OutputRoot = "generated/packaged_editor"
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

$editorOutput = Join-Path $repoRoot "generated/outputs/Editor/$EditorConfiguration"
$appOutput = Join-Path $repoRoot "generated/outputs/App/$SdkConfiguration"
$sdkLibOutput = Join-Path $repoRoot "generated/outputs/Sdk/Lib/$SdkConfiguration"
$packageRoot = Join-Path $repoRoot $OutputRoot
$packageEditorRoot = Join-Path $packageRoot "Editor"
$packageSdkRoot = Join-Path $packageRoot "Sdk"

Invoke-Step -Message "CMake configure を更新します。" -Action {
    cmake -S . -B $BuildDirectory
    if ($LASTEXITCODE -ne 0)
    {
        throw "CMake configure に失敗しました。"
    }
}

Invoke-Step -Message "Editor を $EditorConfiguration でビルドします。" -Action {
    cmake --build $BuildDirectory --config $EditorConfiguration --target Editor
    if ($LASTEXITCODE -ne 0)
    {
        throw "Editor のビルドに失敗しました。"
    }
}

Invoke-Step -Message "SDK 用ライブラリを $SdkConfiguration でビルドします。" -Action {
    cmake --build $BuildDirectory --config $SdkConfiguration --target CueApp
    if ($LASTEXITCODE -ne 0)
    {
        throw "SDK 用ライブラリのビルドに失敗しました。"
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

    Get-ChildItem -LiteralPath $editorOutput -Filter "*.dll" -File | ForEach-Object {
        Copy-Path -Source $_.FullName `
            -Destination (Join-Path $packageEditorRoot $_.Name)
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
    Copy-Path -Source $sdkLibOutput `
        -Destination (Join-Path $packageSdkRoot "generated/outputs/Sdk/Lib/$SdkConfiguration")
    Copy-Path -Source (Join-Path $repoRoot "vcpkg.json") `
        -Destination (Join-Path $packageSdkRoot "vcpkg.json")
    Copy-Path -Source (Join-Path $repoRoot "vcpkg-configuration.json") `
        -Destination (Join-Path $packageSdkRoot "vcpkg-configuration.json")
}

Write-Host "[CueEditorPackage] packaged_editor を作成しました: $packageRoot"
