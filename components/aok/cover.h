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
  void add_stop_point(float pct) { stop_points_.push_back(pct / 100.0f); }
  void set_resume_buffer(uint32_t ms) { resume_buffer_ms_ = ms; }
  void set_resume_factor(float f) { resume_factor_ = f; }
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
  void start_movement_(cover::CoverOperation direction, float next_stop, uint8_t cmd_normal, uint8_t cmd_inverted);

 protected:
  void schedule_after_();

  void apply_snapped_or_arbitrary_target(float target);
  void log_next_intercept_(const char *context);

  AOKHub *hub_{nullptr};
  uint16_t channel_{0};
  uint32_t travel_time_{30000};  // ms; only used for position estimation
  uint32_t after_delay_{250};
  bool send_after_{true};
  bool inverted_{false};

  // Pending AFTER (0x24) packet bookkeeping.
  bool after_pending_{false};
  uint32_t after_due_at_{0};

  // Programmed stop points (0.0–1.0), sorted ascending.
  std::vector<float> stop_points_;
  // Resume timing after parking at a stop point:
  //   resume_at = operation_started_at + estimated_travel_ms * resume_factor_ + resume_buffer_ms_
  uint32_t resume_buffer_ms_{3000};  // fixed safety buffer (default 3 s)
  float resume_factor_{0.2f};        // travel-time multiplier (default 0.2×)

  // Stop at target
  bool stop_pending_{false};
  float stop_position_{0.0f};
  bool stop_is_point_{false};  // true = motor self-parks; false = send STOP

  // Resume after stop-point park: continue toward final_target_.
  bool resume_pending_{false};
  uint32_t resume_at_{0};       // millis() timestamp to fire the next command
  float final_target_{-1.0f};   // original requested target (0.0–1.0), -1 = none
  cover::CoverOperation resume_direction_{cover::COVER_OPERATION_IDLE};

  // Position estimation (optional but nice to have).
  cover::CoverOperation last_operation_{cover::COVER_OPERATION_IDLE};
  uint32_t operation_started_at_{0};
  float position_at_op_start_{0.5f};
};

}  // namespace aok
}  // namespace esphome
