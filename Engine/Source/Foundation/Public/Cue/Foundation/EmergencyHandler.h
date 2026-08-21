#pragma once

#include <string_view>

namespace cue
{
/**
 * @brief Allocationを伴う通常診断が利用できない場合の終了境界
 *
 * HandlerのOwnerは、この非所有参照を保持する全Objectより長く生存させる
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
     * @brief 追加Allocationを行わずProcessを終了する
     * @param a_message 静的Storageを持つ診断Message
     */
    [[noreturn]] virtual void terminate(std::string_view a_message) noexcept = 0;
};
} // namespace cue
