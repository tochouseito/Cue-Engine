Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-NativeStep
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Action
    )

    & $Action > $null
    if ($LASTEXITCODE -ne 0)
    {
        throw "$Name に失敗しました。exit code: $LASTEXITCODE"
    }
}

# 1) 依存コマンドの存在確認
$cmds = @("uvx", "npx")
foreach ($c in $cmds)
{
    $ok = Get-Command $c -ErrorAction SilentlyContinue
    if (-not $ok)
    {
        throw "$c が見つかりません。インストールして PATH を通してください。"
    }
}

# 2) MCP の依存取得を“起動試行”で前倒し（ネットが必要）
Write-Host "Bootstrap: starting MCP servers once to warm caches..."

Invoke-NativeStep -Name "Serena MCP bootstrap" -Action {
    uvx --from git+https://github.com/oraios/serena serena start-mcp-server --context codex --help
}
Invoke-NativeStep -Name "Sequential Thinking MCP bootstrap" -Action {
    npx -y @modelcontextprotocol/server-sequential-thinking --help
}
Invoke-NativeStep -Name "Memory Bank MCP bootstrap" -Action {
    uvx --from git+https://github.com/ipospelov/mcp-memory-bank mcp_memory_bank --help
}

Write-Host "Bootstrap done."
