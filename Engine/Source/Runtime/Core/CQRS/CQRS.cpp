#include "CQRS.h"

namespace Cue::Core::CQRS
{
    Result Bridge::submit_command(std::unique_ptr<ICommand> a_command)
    {
        // null command を拒否し、呼び出し側の組み立て漏れを早期に検出する。
        if (a_command == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "Command must not be null");
        }

        // 実行タイミングは Engine 側で制御できるよう queue へ積むだけにする。
        m_pendingCommands.push_back(std::move(a_command));
        return Result::ok();
    }

    Result Bridge::drain_commands(ICommandContext& a_commandContext)
    {
        // command は submit 順で処理し、Engine の安全なフェーズだけで状態を書き換える。
        while (!m_pendingCommands.empty())
        {
            std::unique_ptr<ICommand> command = std::move(m_pendingCommands.front());
            m_pendingCommands.pop_front();

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
                (void)command.release();
                m_undoStack.push_back(std::unique_ptr<IUndoableCommand>(undoableCommand));
            }
        }

        // すべて成功したら ok を返し、partial success の境界を呼び出し側へ明確にする。
        return Result::ok();
    }

    Result Bridge::undo_last_command(ICommandContext& a_commandContext)
    {
        // undo 対象がなければ失敗を返し、Renderer 側で UI 状態と整合させやすくする。
        if (m_undoStack.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error, "No command to undo");
        }

        // 失敗時は履歴位置を元へ戻し、履歴破損を防ぐ。
        std::unique_ptr<IUndoableCommand> command = std::move(m_undoStack.back());
        m_undoStack.pop_back();

        Result result = command->undo(a_commandContext);
        if (!result)
        {
            m_undoStack.push_back(std::move(command));
            return result;
        }

        // undo 成功後だけ redo 履歴へ移し、再適用可能な状態を保持する。
        m_redoStack.push_back(std::move(command));
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
        std::unique_ptr<IUndoableCommand> command = std::move(m_redoStack.back());
        m_redoStack.pop_back();

        Result result = command->execute(a_commandContext);
        if (!result)
        {
            m_redoStack.push_back(std::move(command));
            return result;
        }

        // 成功時だけ undo 履歴へ戻し、履歴の往復を保証する。
        m_undoStack.push_back(std::move(command));
        return Result::ok();
    }
}
