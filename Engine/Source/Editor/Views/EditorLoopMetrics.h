// EditorLoopMetrics の役割と公開要素を定義する

#pragma once

// Editor ループ計測値をまとめ、表示側が個別タイマーの所有権へ触れないようにする

namespace Cue::Editor
{
    struct EditorLoopMetrics final
    {
        double loopTotalMs = 0.0;
        double pollMessageMs = 0.0;
        double imguiBeginMs = 0.0;
        double projectHubUpdateMs = 0.0;
        double editorUpdateMs = 0.0;
        double imguiEndMs = 0.0;
        double engineBeginMs = 0.0;
        double engineTickMs = 0.0;
        double engineEndMs = 0.0;
        bool didDrawImgui = false;
    };

    struct EditorUpdateMetrics final
    {
        double totalMs = 0.0;
        double pendingScriptActionMs = 0.0;
        double dockspaceMs = 0.0;
        double menuBarMs = 0.0;
        double optionalWindowsMs = 0.0;
        double statisticsMs = 0.0;
        double gameViewMs = 0.0;
        double debugViewMs = 0.0;
        double assetBrowserMs = 0.0;
        double createScriptPopupMs = 0.0;
        double scriptBuildNotificationMs = 0.0;
        double scriptBuildOutputMs = 0.0;
        double hierarchyMs = 0.0;
        double inspectorMs = 0.0;
    };
}
