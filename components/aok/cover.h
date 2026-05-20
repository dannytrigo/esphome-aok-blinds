#pragma once

#include "esphome/core/component.h"
#include "esphome/components/cover/cover.h"
#include "aok.h"

namespace esphome {
namespace aok {

class AOKCover : public cover::Cover, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  cover::CoverTraits get_traits() override;
  void control(const cover::CoverCall &call) override;

  void set_hub(AOKHub *hub) { hub_ = hub; }
  void set_channel(uint16_t channel) { channel_ = channel; }
  void set_travel_time(uint32_t ms) { travel_time_ = ms; }
  void set_after_delay(uint32_t ms) { after_delay_ = ms; }
  void set_send_after(bool s) { send_after_ = s; }
  void set_inverted(bool i) { inverted_ = i; }

  // Exposed for the pairing button service.
  void send_program() { hub_->send_command(channel_, AOK_CMD_PROGRAM); }
  void send_change_direction() { hub_->send_command(channel_, AOK_CMD_CHANGE_DIR); }

  void send_up();
  void send_down();
  void send_stop();

 protected:
  void schedule_after_();

  AOKHub *hub_{nullptr};
  uint16_t channel_{0};
  uint32_t travel_time_{30000};  // ms; only used for position estimation
  uint32_t after_delay_{250};
  bool send_after_{true};
  bool inverted_{false};

  // Pending AFTER (0x24) packet bookkeeping.
  bool after_pending_{false};
  uint32_t after_due_at_{0};

  // Stop at target
  bool stop_pending_{false};
  float stop_position_{0.0f};

  // Position estimation (optional but nice to have).
  cover::CoverOperation last_operation_{cover::COVER_OPERATION_IDLE};
  uint32_t operation_started_at_{0};
  float position_at_op_start_{0.5f};
};

}  // namespace aok
}  // namespace esphome
