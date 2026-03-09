#pragma once
#ifdef CUE_DEBUG
#include <cassert>
#include <cstdio>
#endif
#include <format>
#include <string>
#include <string_view>
#include "Result.h"

namespace Cue
{
    class Assert final
    {
    public:
        template<typename... Args>
        static void cue_assert(bool expr, const char* message, Args&&... args)
        {
#ifdef CUE_DEBUG
            // 1) 成功時は整形コストを払わずに返し、通常経路の負荷を増やさない。
            if (expr)
            {
                return;
            }

            // 2) 失敗時だけメッセージを整形して出力し、標準 assert の式文字列化に依存しない。
            std::string formatted_message = std::vformat(message, std::make_format_args(args...));
            report_assert_failure(formatted_message);
#endif
        }

        template<typename... Args>
        static void cue_assert(Result expr, const char* message, Args&&... args)
        {
#ifdef CUE_DEBUG
            // 1) Result 成功時は何もせず返し、失敗時だけ診断を出す。
            if (expr)
            {
                return;
            }

            // 2) 失敗メッセージを文字列として出力してから止め、assert 式の文字列表現を露出させない。
            std::string formatted_message = std::vformat(message, std::make_format_args(args...));
            report_assert_failure(formatted_message);
#endif
        }

        template<typename... Args>
        static void cue_assert(bool expr, const char* category, const char* message, Args&&... args)
        {
#ifdef CUE_DEBUG
            // 1) 成功時はカテゴリ付き整形を行わず、失敗時だけ詳細文を構築する。
            if (expr)
            {
                return;
            }

            // 2) 呼び出し側の "カテゴリ + フォーマット" 契約を 1 本の表示文へ正規化する。
            std::string formatted_message = std::format("[{}] {}", category, std::vformat(message, std::make_format_args(args...)));
            report_assert_failure(formatted_message);
#endif
        }

        template<typename... Args>
        static void cue_assert(Result expr, const char* category, const char* message, Args&&... args)
        {
#ifdef CUE_DEBUG
            // 1) Result 版もカテゴリ付き文面へ揃え、呼び出し箇所ごとの表記ゆれを防ぐ。
            if (expr)
            {
                return;
            }

            // 2) 失敗時の表示を 1 箇所へ集約し、Result/bool で挙動がズレないようにする。
            std::string formatted_message = std::format("[{}] {}", category, std::vformat(message, std::make_format_args(args...)));
            report_assert_failure(formatted_message);
#endif
        }

    private:
        static void report_assert_failure(std::string_view message)
        {
#ifdef CUE_DEBUG
            // 1) 整形済みメッセージをそのまま stderr へ出し、標準 assert の式文字列より先に原因を見せる。
            std::fputs(message.data(), stderr);
            std::fputc('\n', stderr);
            std::fflush(stderr);

            // 2) 最後は通常の assert で停止し、デバッガの停止挙動は維持する。
            assert(false && "Cue assertion failed.");
#endif
        }
    };
}
