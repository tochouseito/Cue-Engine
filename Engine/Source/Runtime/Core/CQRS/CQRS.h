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

        /// @brief 同一操作内の後続 Command を統合できる場合に最終状態を取り込む
        virtual bool try_merge(const IUndoableCommand&)
        {
            return false;
        }
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
        Result submit_command(std::unique_ptr<ICommand> a_command,
                              uint64_t a_historyTransactionId = 0);

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

        /// @brief 現在の Undo 位置を識別する値を返す
        [[nodiscard]] uint64_t history_cursor() const noexcept;

        /// @brief 別 Scene へ切り替えるため過去 Scene の履歴を破棄する
        void reset_history() noexcept;

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
        struct PendingCommand
        {
            std::unique_ptr<ICommand> command{};
            uint64_t historyTransactionId = 0;
        };

        struct HistoryEntry
        {
            std::unique_ptr<IUndoableCommand> command{};
            uint64_t id = 0;
            uint64_t transactionId = 0;
        };

        enum class HistoryRequest : uint8_t
        {
            undo,
            redo,
        };

        std::deque<PendingCommand> m_pendingCommands; // 保留中のコマンド
        std::deque<HistoryRequest> m_pendingHistoryRequests; // Engine 更新境界で処理する履歴操作
        std::vector<HistoryEntry> m_undoStack; // アンドゥ可能なコマンドのスタック
        std::vector<HistoryEntry> m_redoStack; // リドゥ可能なコマンドのスタック
        uint64_t m_nextHistoryId = 2;
        uint64_t m_historyRootId = 1;
    };
}
