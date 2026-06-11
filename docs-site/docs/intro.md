---
sidebar_position: 1
title: はじめに
---

# Cue Engine Docs

Cue Engine の Renderer、Script、Runtime 仕様をまとめるリファレンスサイトです。

## 最初に読むページ

- [環境要件](./Manual/Requirements.md)
- [Renderer の使い方](./Manual/Renderer.md)
- [Script の使い方](./Manual/Script.md)
- [Scene / Asset / 物理 / Navigation](./Manual/Runtime.md)

## 現在の前提

- Renderer は Windows 専用です。
- Engine / Renderer 開発には Visual Studio、CMake、PowerShell、vcpkg が必要です。
- GameScript は CMake でビルドします。
- GameScript の読み込み構成は `scriptBuildConfiguration` と同じです。
- Scene は World に複数読み込めますが、GameObject 名は World 全体で一意です。
- Asset import は `.png`、`.wav`、`.obj` の外部ドロップに対応しています。
