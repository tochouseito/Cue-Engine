#pragma once

/// **********************************************************************
/// GameScript DLL のロードと ABI 呼び出しを管理する
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <IO/Path.h>

// === Engine includes ===
#include "ScriptModuleApi.h"

// === C++ includes ===
#include <string>
#include <string_view>
#include <vector>

namespace Cue::Script
{
    /// @brief DLL の pointer lifetime から切り離して Engine が所有する公開 field metadata
    struct ScriptFieldInfo final
    {
        std::string name{};
        std::string classValue{};
        Core::Native::ScriptFieldType type =
            Core::Native::ScriptFieldType::Float;
        uint32_t flags = Core::Native::ScriptFieldFlagNone;
        float floatValue = 0.0f;
        int32_t int32Value = 0;
        bool boolValue = false;
        Core::Native::ScriptEntityHandle entityValue{};
    };

    /// @brief Editor と Runtime が共有する Script class metadata
    struct ScriptClassInfo final
    {
        std::string name{};
        std::vector<ScriptFieldInfo> fields{};
        std::vector<std::string> functions{};
        Core::Native::ScriptStateDescriptor stateDescriptor{};
    };

    /// @brief GameScript DLL の所有と ABI 呼び出しを管理
    ///
    /// DLL 解放時に exports 内の関数ポインタも無効になるため、instance を保持する
    /// ScriptRuntime より先の unload を禁止
    class ScriptModule final
    {
    public:
        ScriptModule() = default;
        ~ScriptModule();

        ScriptModule(const ScriptModule&) = delete;
        ScriptModule& operator=(const ScriptModule&) = delete;
        ScriptModule(ScriptModule&&) = delete;
        ScriptModule& operator=(ScriptModule&&) = delete;

        /// @brief 指定 DLL の ABI と exports を検証してロード
        ///
        /// 読み込みに失敗した DLL の関数ポインタを runtime が参照しないよう、成功時だけ利用可能状態へ遷移
        [[nodiscard]] Result load(const Core::IO::Path& a_modulePath) noexcept;

        /// @brief 元の DLL を識別情報として保持し、shadow copy を実際のロード元にする
        [[nodiscard]] Result load_shadow_copy(
            const Core::IO::Path& a_modulePath,
            const Core::IO::Path& a_shadowPath) noexcept;

        /// @brief ロード済み DLL と exports を無効化
        ///
        /// Script instance の破棄後にだけ呼び出し、DLL 内の object を解放できない状態を防止
        void unload() noexcept;

        /// @brief 必要な ABI exports を呼び出せる状態を返却
        [[nodiscard]] bool is_loaded() const noexcept;

        /// @brief Scene に記録された className が module 内で解決できるかを返却
        [[nodiscard]] bool has_class(const char* a_className) const noexcept;

        /// @brief DLL から検証済みで取得した Script class 名一覧を返す
        [[nodiscard]] const std::vector<std::string>& class_names() const noexcept;

        /// @brief DLL から複製した公開 field/function metadata を返す
        [[nodiscard]] const std::vector<ScriptClassInfo>& class_infos() const noexcept;

        /// @brief class 名に対応する metadata を返す
        [[nodiscard]] const ScriptClassInfo*
        find_class_info(std::string_view a_className) const noexcept;

        /// @brief GameScript DLL へ Runtime World 操作用の ABI を接続する
        [[nodiscard]] Result register_engine_api(
            const Core::Native::ScriptEngineApi& a_engineApi) const noexcept;

        /// @brief DLL 側に Script instance の生成を要求
        ///
        /// instance の実体を Engine 側へ公開せず、DLL をまたぐ C++ 所有権を発生させません
        [[nodiscard]] Result create_instance(
            const ScriptInstanceCreateInfo& a_createInfo,
            ScriptInstanceHandle& a_outHandle) const noexcept;

        /// @brief DLL 側が所有する Script instance を破棄
        [[nodiscard]] Result destroy_instance(ScriptInstanceHandle a_handle) const noexcept;

        /// @brief 生成済み instance の OnCreate lifecycle を呼び出し
        [[nodiscard]] Result on_create(ScriptInstanceHandle a_handle) const noexcept;

        /// @brief 生成済み instance の OnUpdate lifecycle を呼び出し
        [[nodiscard]] Result on_update(
            ScriptInstanceHandle a_handle,
            float a_deltaTimeSeconds) const noexcept;

        /// @brief 生成済み instance の公開関数を名前で呼び出す
        [[nodiscard]] Result invoke(
            ScriptInstanceHandle a_handle,
            std::string_view a_functionName) const noexcept;

        /// @brief class の reload state 互換情報を取得する
        [[nodiscard]] Result get_state_descriptor(
            std::string_view a_className,
            Core::Native::ScriptStateDescriptor& a_outDescriptor) const noexcept;

        /// @brief instance の reload state 保存領域サイズを取得する
        [[nodiscard]] Result get_instance_state_size(
            ScriptInstanceHandle a_handle,
            uint32_t& a_outStateSize) const noexcept;

        /// @brief instance の reload state を保存する
        [[nodiscard]] Result serialize_instance(
            ScriptInstanceHandle a_handle,
            void* a_outStateBuffer,
            uint32_t a_stateBufferSize) const noexcept;

        /// @brief instance の reload state を復元する
        [[nodiscard]] Result restore_instance(
            ScriptInstanceHandle a_handle,
            const void* a_stateBuffer,
            uint32_t a_stateBufferSize) const noexcept;

    private:
        [[nodiscard]] Result load_internal(
            const Core::IO::Path& a_modulePath,
            const Core::IO::Path& a_loadPath) noexcept;
        [[nodiscard]] static Result convert_result(ScriptResult a_result) noexcept;

        void* m_nativeHandle = nullptr; // DLL を解放するまで exports の呼び出し先を有効に保つ所有 handle
        Core::IO::Path m_modulePath{};  // エラー調査時にロード元を特定できるよう正規化して保持する
        Core::IO::Path m_loadedPath{};  // shadow copy の寿命を DLL handle と一致させる
        ScriptModuleExports m_exports{}; // ABI 検証済みの関数だけを runtime に公開する
        std::vector<std::string> m_classNames{}; // DLL 解放後の文字列ポインタを Editor が参照しないよう Engine 側に複製する
        std::vector<ScriptClassInfo> m_classInfos{}; // public field/function metadata を Engine 所有へ複製する
    };
} // namespace Cue::Script
