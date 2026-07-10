#include "CQRS.h"

namespace Cue::Core::CQRS
{
    Result Bridge::submit_command(std::unique_ptr<ICommand> a_command,
                                  uint64_t a_historyTransactionId)
    {
        // null command を拒否し、呼び出し側の組み立て漏れを早期に検出する。
        if (a_command == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "Command must not be null");
        }

        // 実行タイミングは Engine 側で制御できるよう queue へ積むだけにする。
        PendingCommand pendingCommand{};
        pendingCommand.command = std::move(a_command);
        pendingCommand.historyTransactionId = a_historyTransactionId;
        m_pendingCommands.push_back(std::move(pendingCommand));
        return Result::ok();
    }

    Result Bridge::drain_commands(ICommandContext& a_commandContext)
    {
        // command は submit 順で処理し、Engine の安全なフェーズだけで状態を書き換える。
        while (!m_pendingCommands.empty())
        {
            PendingCommand pendingCommand = std::move(m_pendingCommands.front());
            m_pendingCommands.pop_front();

            std::unique_ptr<ICommand> command = std::move(pendingCommand.command);

            Result result = command->execute(a_commandContext);
            if (!result)
            {
                return result;
            }

            // undo 対応 command だけ履歴へ残し、新規実行で redo 履歴は破棄する。
            m_redoStack.clear();

            IUndoableCommand* undoableCommand = dynamic_cast<IUndoableCommand*>(command.get());
            if (undoableCommand != nullptr)
            {
                const bool canMerge =
                    pendingCommand.historyTransactionId != 0 &&
                    !m_undoStack.empty() &&
                    m_undoStack.back().transactionId == pendingCommand.historyTransactionId;
                if (canMerge && m_undoStack.back().command->try_merge(*undoableCommand))
                {
                    continue;
                }

                HistoryEntry historyEntry{};
                historyEntry.command = std::unique_ptr<IUndoableCommand>(
                    static_cast<IUndoableCommand*>(command.release()));
                historyEntry.id = m_nextHistoryId++;
                historyEntry.transactionId = pendingCommand.historyTransactionId;
                m_undoStack.push_back(std::move(historyEntry));
            }
        }

        while (!m_pendingHistoryRequests.empty())
        {
            const HistoryRequest request = m_pendingHistoryRequests.front();
            m_pendingHistoryRequests.pop_front();

            const Result result = request == HistoryRequest::undo
                ? undo_last_command(a_commandContext)
                : redo_last_command(a_commandContext);
            if (!result)
            {
                return result;
            }
        }

        // すべて成功したら ok を返し、partial success の境界を呼び出し側へ明確にする。
        return Result::ok();
    }

    Result Bridge::undo_last_command(ICommandContext& a_commandContext)
    {
        // undo 対象がなければ失敗を返し、Editor 側で UI 状態と整合させやすくする。
        if (m_undoStack.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error, "No command to undo");
        }

        // 失敗時は履歴位置を元へ戻し、履歴破損を防ぐ。
        HistoryEntry historyEntry = std::move(m_undoStack.back());
        m_undoStack.pop_back();

        Result result = historyEntry.command->undo(a_commandContext);
        if (!result)
        {
            m_undoStack.push_back(std::move(historyEntry));
            return result;
        }

        // undo 成功後だけ redo 履歴へ移し、再適用可能な状態を保持する。
        m_redoStack.push_back(std::move(historyEntry));
        return Result::ok();
    }

    Result Bridge::redo_last_command(ICommandContext& a_commandContext)
    {
        // redo 対象がなければ失敗を返し、無効な操作を明示する。
        if (m_redoStack.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error, "No command to redo");
        }

        // 再実行に失敗した場合は redo 履歴へ戻し、再試行余地を残す。
        HistoryEntry historyEntry = std::move(m_redoStack.back());
        m_redoStack.pop_back();

        Result result = historyEntry.command->execute(a_commandContext);
        if (!result)
        {
            m_redoStack.push_back(std::move(historyEntry));
            return result;
        }

        // 成功時だけ undo 履歴へ戻し、履歴の往復を保証する。
        m_undoStack.push_back(std::move(historyEntry));
        return Result::ok();
    }

    Result Bridge::request_undo()
    {
        if (!can_undo())
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "No command to undo");
        }

        // Editor thread から GameWorld を直接変更せず、通常 Command と同じ更新境界へ要求を送る
        m_pendingHistoryRequests.push_back(HistoryRequest::undo);
        return Result::ok();
    }

    Result Bridge::request_redo()
    {
        if (!can_redo())
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "No command to redo");
        }

        // Editor thread から GameWorld を直接変更せず、通常 Command と同じ更新境界へ要求を送る
        m_pendingHistoryRequests.push_back(HistoryRequest::redo);
        return Result::ok();
    }

    uint64_t Bridge::history_cursor() const noexcept
    {
        return m_undoStack.empty() ? m_historyRootId : m_undoStack.back().id;
    }

    void Bridge::reset_history() noexcept
    {
        // Scene 切替後に以前の GameWorld を対象とする Command を実行しないよう履歴要求も破棄する。
        m_undoStack.clear();
        m_redoStack.clear();
        m_pendingHistoryRequests.clear();
        m_historyRootId = m_nextHistoryId++;
    }
}
