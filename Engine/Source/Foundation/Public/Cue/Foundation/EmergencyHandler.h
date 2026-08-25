#pragma once

#include <string_view>

namespace cue
{
/**
 * @brief Allocation を伴う通常診断が利用できない場合の終了境界
 *
 * Error や Logger 自身の構築失敗でも確実に Process を停止できるよう、通常の診断経路から独立した最終手段を提供する
 * Handler の Owner は、この非所有参照を保持する全 Object より長く生存させる
 */
class EmergencyHandler
{
  public:
    EmergencyHandler() = default;
    virtual ~EmergencyHandler() = default;

    EmergencyHandler(const EmergencyHandler &) = delete;
    EmergencyHandler &operator=(const EmergencyHandler &) = delete;
    EmergencyHandler(EmergencyHandler &&) = delete;
    EmergencyHandler &operator=(EmergencyHandler &&) = delete;

    /**
     * @brief 追加 Allocation を行わず Process を終了する
     * @param a_message 静的 Storage を持つ診断 Message
     *
     * `[[noreturn]]` 契約に従い実装は復帰してはならず、復帰した場合の動作は保証しない
     */
    [[noreturn]] virtual void terminate(std::string_view a_message) noexcept = 0;
};
} // namespace cue
