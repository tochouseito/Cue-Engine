param(
    [string]$Solution = "DramaEngine.slnx",
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [switch]$Rebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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
if (-not (Test-Path $Solution))
{
    throw "Solution が見つかりません: $Solution"
}

# 2) MSBuild の解決
$msbuild = Find-MSBuildExe

# 3) ターゲット選択
$target = if ($Rebuild) { "Rebuild" } else { "Build" }

# 4) ビルド実行（NuGet restore はしない）
& $msbuild $Solution `
    "/t:$target" `
    "/p:Configuration=$Configuration" `
    "/p:Platform=$Platform" `
    "/m" `
    "/nologo" `
    "/v:m" `
    "/clp:Summary;Verbosity=minimal"

exit $LASTEXITCODE
