#pragma once
#include <deque>
#include <memory>
#include <vector>
#include <Result.h>

namespace Cue::CQRS
{
    class ICommandContext
    {
    public:
        virtual ~ICommandContext() = default;
    };

    class IQueryContext
    {
    public:
        virtual ~IQueryContext() = default;
    };

    class IQueryResult
    {
    public:
        virtual ~IQueryResult() = default;
    };

    class ICommand
    {
    public:
        virtual ~ICommand() = default;
        virtual Result execute(ICommandContext& commandContext) = 0;
    };

    class IUndoableCommand : public ICommand
    {
    public:
        ~IUndoableCommand() override = default;
        virtual Result undo(ICommandContext& commandContext) = 0;
    };

    class IQuery
    {
    public:
        virtual ~IQuery() = default;
        virtual Result execute(const IQueryContext& queryContext, IQueryResult& outResult) const = 0;
    };

    class Bridge final
    {
    public:
        Result submit_command(std::unique_ptr<ICommand> command)
        {
            // 1) null command を拒否し、呼び出し側の組み立て漏れを早期に検出する。
            if (command == nullptr)
            {
                return Result::fail(Facility::Core, Code::InvalidArg, Severity::Error, 0, "Command is null");
            }

            // 2) 実行タイミングは Engine 側で制御できるよう queue へ積むだけにする。
            m_pendingCommands.push_back(std::move(command));
            return Result::ok();
        }

        Result drain_commands(ICommandContext& commandContext)
        {
            // 1) command は submit 順で処理し、Engine の安全なフェーズだけで状態を書き換える。
            while (!m_pendingCommands.empty())
            {
                std::unique_ptr<ICommand> command = std::move(m_pendingCommands.front());
                m_pendingCommands.pop_front();

                Result result = command->execute(commandContext);
                if (!result)
                {
                    return result;
                }

                // 2) undo 対応 command だけ履歴へ残し、新規実行で redo 履歴は破棄する。
                m_redoStack.clear();

                IUndoableCommand* undoableCommand = dynamic_cast<IUndoableCommand*>(command.get());
                if (undoableCommand != nullptr)
                {
                    (void)command.release();
                    m_undoStack.push_back(std::unique_ptr<IUndoableCommand>(undoableCommand));
                }
            }

            // 3) すべて成功したら ok を返し、partial success の境界を呼び出し側へ明確にする。
            return Result::ok();
        }

        Result undo_last_command(ICommandContext& commandContext)
        {
            // 1) undo 対象がなければ失敗を返し、Editor 側で UI 状態と整合させやすくする。
            if (m_undoStack.empty())
            {
                return Result::fail(Facility::Core, Code::InvalidState, Severity::Error, 0, "No command to undo");
            }

            // 2) 失敗時は履歴位置を元へ戻し、履歴破損を防ぐ。
            std::unique_ptr<IUndoableCommand> command = std::move(m_undoStack.back());
            m_undoStack.pop_back();

            Result result = command->undo(commandContext);
            if (!result)
            {
                m_undoStack.push_back(std::move(command));
                return result;
            }

            // 3) undo 成功後だけ redo 履歴へ移し、再適用可能な状態を保持する。
            m_redoStack.push_back(std::move(command));
            return Result::ok();
        }

        Result redo_last_command(ICommandContext& commandContext)
        {
            // 1) redo 対象がなければ失敗を返し、無効な操作を明示する。
            if (m_redoStack.empty())
            {
                return Result::fail(Facility::Core, Code::InvalidState, Severity::Error, 0, "No command to redo");
            }

            // 2) 再実行に失敗した場合は redo 履歴へ戻し、再試行余地を残す。
            std::unique_ptr<IUndoableCommand> command = std::move(m_redoStack.back());
            m_redoStack.pop_back();

            Result result = command->execute(commandContext);
            if (!result)
            {
                m_redoStack.push_back(std::move(command));
                return result;
            }

            // 3) 成功時だけ undo 履歴へ戻し、履歴の往復を保証する。
            m_undoStack.push_back(std::move(command));
            return Result::ok();
        }

        Result execute_query(const IQuery& query, const IQueryContext& queryContext, IQueryResult& outResult) const
        {
            // 1) query は Engine が公開した read model だけを読む契約にし、書き込み経路を混ぜない。
            return query.execute(queryContext, outResult);
        }

        [[nodiscard]] bool has_pending_commands() const noexcept
        {
            return !m_pendingCommands.empty();
        }

        [[nodiscard]] bool can_undo() const noexcept
        {
            return !m_undoStack.empty();
        }

        [[nodiscard]] bool can_redo() const noexcept
        {
            return !m_redoStack.empty();
        }

    private:
        std::deque<std::unique_ptr<ICommand>> m_pendingCommands;
        std::vector<std::unique_ptr<IUndoableCommand>> m_undoStack;
        std::vector<std::unique_ptr<IUndoableCommand>> m_redoStack;
    };
}
