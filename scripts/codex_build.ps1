param(
    [string]$Solution = "Cue Engine.slnx",
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [switch]$Rebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-SolutionPath
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (Test-Path $Path)
    {
        return $Path
    }

    $solutionName = [System.IO.Path]::GetFileName($Path)
    $candidates = @(
        (Join-Path "out/build/win-x64" $solutionName),
        (Join-Path "out/build/win-x64" ($solutionName -replace " ", "")),
        (Join-Path "generated" $solutionName),
        (Join-Path "." $solutionName)
    )

    foreach ($candidate in $candidates)
    {
        if (Test-Path $candidate)
        {
            return $candidate
        }
    }

    return $null
}

function Find-MSBuildExe
{
    # 1) ユーザー環境で確認済みの Insiders パスを最優先
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\17\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\17\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\17\Community\MSBuild\Current\Bin\MSBuild.exe"
    )

    foreach ($p in $candidates)
    {
        if (Test-Path $p)
        {
            return $p
        }
    }

    # 2) vswhere があるならそれで探す（インストール構成差に強い）
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere)
    {
        $installPath = & $vswhere -latest -products * -property installationPath
        if ($installPath)
        {
            $p = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $p)
            {
                return $p
            }
        }
    }

    throw "MSBuild.exe が見つかりません。Visual Studio / Build Tools を確認してください。"
}

# 1) 入力チェック
$resolvedSolution = Resolve-SolutionPath -Path $Solution
if (-not $resolvedSolution)
{
    throw "Solution が見つかりません: $Solution"
}

# 2) MSBuild の解決
$msbuild = Find-MSBuildExe

# 3) ターゲット選択
$target = if ($Rebuild) { "Rebuild" } else { "Build" }

# 4) ビルド実行（NuGet restore はしない）
& $msbuild $resolvedSolution `
    "/t:$target" `
    "/p:Configuration=$Configuration" `
    "/p:Platform=$Platform" `
    "/nologo" `
    "/v:m" `
    "/clp:Summary;Verbosity=minimal"

exit $LASTEXITCODE
