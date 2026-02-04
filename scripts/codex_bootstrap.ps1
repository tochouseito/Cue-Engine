Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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

& uvx --from git+https://github.com/oraios/serena serena start-mcp-server --context codex --help | Out-Null
& npx -y @modelcontextprotocol/server-sequential-thinking --help | Out-Null
& uvx --from git+https://github.com/ipospelov/mcp-memory-bank mcp_memory_bank --help | Out-Null

Write-Host "Bootstrap done."
