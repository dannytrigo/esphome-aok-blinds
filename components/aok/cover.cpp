#include "cover.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace aok {

static const char *const TAG = "aok.cover";

void AOKCover::setup() {
  // Restore last known position if available; otherwise assume fully open.
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(this);
  } else {
    this->position = cover::COVER_OPEN;
  }
}

void AOKCover::dump_config() {
  LOG_COVER("", "A-OK Cover", this);
  ESP_LOGCONFIG(TAG, "  Channel bitmask: 0x%04X", this->channel_);
  ESP_LOGCONFIG(TAG, "  Travel time:     %u ms", this->travel_time_);
  ESP_LOGCONFIG(TAG, "  AFTER delay:     %u ms (enabled=%s)",
                this->after_delay_, this->send_after_ ? "yes" : "no");
}

cover::CoverTraits AOKCover::get_traits() {
  cover::CoverTraits traits;
  // We don't get real feedback from the motor, so position is assumed.
  traits.set_supports_position(true);
  traits.set_supports_stop(true);
  traits.set_is_assumed_state(true);
  return traits;
}

void AOKCover::loop() {
  const uint32_t now = millis();

  // 1) Update position estimate if a movement is in progress.
  if (this->last_operation_ != cover::COVER_OPERATION_IDLE) {
    float elapsed = (now - this->operation_started_at_) / float(this->travel_time_);
    float delta = (this->last_operation_ == cover::COVER_OPERATION_OPENING) ? elapsed : -elapsed;
    float new_pos = clamp(this->position_at_op_start_ + delta, 0.0f, 1.0f);
    if (new_pos != this->position) {
      this->position = new_pos;
      this->publish_state();
    }
    // Auto-stop the estimate when we hit a limit (motor stops itself there).
    if (new_pos == 0.0f || new_pos == 1.0f) {
      this->last_operation_ = cover::COVER_OPERATION_IDLE;
      this->current_operation = cover::COVER_OPERATION_IDLE;
      this->publish_state();
    }
  }

  // 2) Send a queued AFTER packet when its delay elapses.
  if (this->after_pending_ && now >= this->after_due_at_) {
    this->after_pending_ = false;
    this->hub_->send_command(this->channel_, AOK_CMD_AFTER);
  }
}

void AOKCover::control(const cover::CoverCall &call) {
  if (call.get_stop()) {
    this->send_stop();
    return;
  }
  if (call.get_position().has_value()) {
    float target = *call.get_position();
    if (target > this->position) {
      this->send_up();
    } else if (target < this->position) {
      this->send_down();
    }
    // Note: we have no way to stop the motor at an intermediate position
    // other than firing a STOP at the right moment based on travel_time.
    // That's left out for now to keep the component simple. Use UP/DOWN/STOP.
  }
}

void AOKCover::send_up() {
  if (this->inverted_) {
    this->hub_->send_command(this->channel_, AOK_CMD_DOWN);
  } else {
    this->hub_->send_command(this->channel_, AOK_CMD_UP);
  }
  this->position_at_op_start_ = this->position;
  this->operation_started_at_ = millis();
  this->last_operation_ = cover::COVER_OPERATION_OPENING;
  this->current_operation = cover::COVER_OPERATION_OPENING;
  this->publish_state();
  this->schedule_after_();
}

void AOKCover::send_down() {
  if (this->inverted_) {
    this->hub_->send_command(this->channel_, AOK_CMD_UP);
  } else {
    this->hub_->send_command(this->channel_, AOK_CMD_DOWN);
  }
  this->position_at_op_start_ = this->position;
  this->operation_started_at_ = millis();
  this->last_operation_ = cover::COVER_OPERATION_CLOSING;
  this->current_operation = cover::COVER_OPERATION_CLOSING;
  this->publish_state();
  this->schedule_after_();
}

void AOKCover::send_stop() {
  this->hub_->send_command(this->channel_, AOK_CMD_STOP);
  this->last_operation_ = cover::COVER_OPERATION_IDLE;
  this->current_operation = cover::COVER_OPERATION_IDLE;
  // Cancel any pending AFTER since STOP resets motor state on its own.
  this->after_pending_ = false;
  this->publish_state();
}

void AOKCover::schedule_after_() {
  if (!this->send_after_)
    return;
  this->after_pending_ = true;
  this->after_due_at_ = millis() + this->after_delay_;
}

}  // namespace aok
}  // namespace esphome
