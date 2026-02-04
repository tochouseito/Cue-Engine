# 推奨コマンド
- ビルド: `pwsh -NoProfile -File scripts/codex_build.ps1`（既定は Debug|x64、NuGet restore 無し）。
- MCP 依存の起動確認: `pwsh -NoProfile -File scripts/codex_bootstrap.ps1`。
- 参照: `Cue Engine.slnx`（MSBuild 用）。