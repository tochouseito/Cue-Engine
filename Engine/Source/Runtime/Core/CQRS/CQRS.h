#pragma once

/// ********************************************************************************
/// コマンド・クエリー・レスポンス・イベント
/// ********************************************************************************

// === Base includes ===
#include "CueResult.h"

// === C++ includes ===
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace Cue::Core::CQRS
{
    /// @brief コマンドコンテキスト
    class ICommandContext
    {
    public:
        virtual ~ICommandContext() = default;
    };

    /// @brief クエリーコンテキスト
    class IQueryContext
    {
    public:
        virtual ~IQueryContext() = default;
    };

    /// @brief クエリー結果
    class IQueryResult
    {
    public:
        virtual ~IQueryResult() = default;
    };

    /// @brief コマンドインターフェース
    class ICommand
    {
    public:
        virtual ~ICommand() = default;
        virtual Result execute(ICommandContext&) = 0;
    };

    /// @brief アンドゥ可能なコマンドインターフェース
    class IUndoableCommand : public ICommand
    {
    public:
        ~IUndoableCommand() override = default;
        virtual Result undo(ICommandContext&) = 0;
    };

    /// @brief クエリーインターフェース
    class IQuery
    {
    public:
        virtual ~IQuery() = default;
        virtual Result execute(const IQueryContext&, IQueryResult&) const = 0;
    };

    /// @brief ブリッジ
    class Bridge final
    {
    public:

        /// @brief コマンドを実行
        Result submit_command(std::unique_ptr<ICommand>);

        /// @brief コマンドを実行(アンドゥ可能)
        Result drain_commands(ICommandContext&);

        /// @brief 最後のコマンドをアンドゥ
        Result undo_last_command(ICommandContext&);

        /// @brief 最後のコマンドをリドゥ
        Result redo_last_command(ICommandContext&);

        /// @brief 次の command drain で Undo を実行する
        [[nodiscard]] Result request_undo();

        /// @brief 次の command drain で Redo を実行する
        [[nodiscard]] Result request_redo();

        /// @brief クエリーを実行
        Result execute_query(const IQuery& a_query, const IQueryContext& a_context, IQueryResult& outResult) const
        {
            return a_query.execute(a_context, outResult);
        }

        /// @brief 保留中のコマンドがあるか
        [[nodiscard]] bool has_pending_commands() const noexcept
        {
            return !m_pendingCommands.empty();
        }

        /// @brief アンドゥ可能なコマンドがあるか
        [[nodiscard]] bool can_undo() const noexcept
        {
            return !m_undoStack.empty();
        }

        /// @brief リドゥ可能なコマンドがあるか
        [[nodiscard]] bool can_redo() const noexcept
        {
            return !m_redoStack.empty();
        }
    private:
        enum class HistoryRequest : uint8_t
        {
            undo,
            redo,
        };

        std::deque<std::unique_ptr<ICommand>> m_pendingCommands; // 保留中のコマンド
        std::deque<HistoryRequest> m_pendingHistoryRequests; // Engine 更新境界で処理する履歴操作
        std::vector<std::unique_ptr<IUndoableCommand>> m_undoStack; // アンドゥ可能なコマンドのスタック
        std::vector<std::unique_ptr<IUndoableCommand>> m_redoStack; // リドゥ可能なコマンドのスタック
    };
}
