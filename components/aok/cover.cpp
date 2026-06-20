#include "cover.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <algorithm>

namespace esphome {
namespace aok {

static const char *const TAG = "aok.cover";

void AOKCover::setup() {
  // Sort stop points ascending so snap logic can iterate in order.
  std::sort(this->stop_points_.begin(), this->stop_points_.end());

  // Restore last known position if available; otherwise assume fully open.
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(this);
  } else {
    this->position = cover::COVER_OPEN;
  }
}

// Returns the next stop point strictly ahead of `pos` in the opening direction,
// or -1 if none exists.
static float next_stop_opening(const std::vector<float> &points, float pos) {
  for (float sp : points) {           // sorted ascending
    if (sp > pos + 0.01f) return sp;
  }
  return -1.0f;
}

// Returns the next stop point strictly ahead of `pos` in the closing direction,
// or -1 if none exists.
static float next_stop_closing(const std::vector<float> &points, float pos) {
  for (int i = (int) points.size() - 1; i >= 0; --i) {  // iterate descending
    if (points[i] < pos - 0.01f) return points[i];
  }
  return -1.0f;
}

void AOKCover::dump_config() {
  LOG_COVER("", "A-OK Cover", this);
  ESP_LOGCONFIG(TAG, "  Channel bitmask: 0x%04X", this->channel_);
  ESP_LOGCONFIG(TAG, "  Travel time:     %u ms", this->travel_time_);
  ESP_LOGCONFIG(TAG, "  AFTER delay:     %u ms (enabled=%s)",
                this->after_delay_, this->send_after_ ? "yes" : "no");
  if (!this->stop_points_.empty()) {
    std::string pts;
    for (float sp : this->stop_points_) {
      if (!pts.empty()) pts += ", ";
      char buf[8];
      snprintf(buf, sizeof(buf), "%.0f%%", sp * 100.0f);
      pts += buf;
    }
    ESP_LOGCONFIG(TAG, "  Stop points:     %s", pts.c_str());
  }
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
    this->position = new_pos;
    if (this->stop_pending_ &&
        ((new_pos >= this->stop_position_ && this->last_operation_ == cover::COVER_OPERATION_OPENING) ||
         (new_pos <= this->stop_position_ && this->last_operation_ == cover::COVER_OPERATION_CLOSING))) {
      this->stop_pending_ = false;
      if (this->stop_is_point_) {
        ESP_LOGD(TAG, "Reached programmed stop point %.0f%% — snapping position, motor self-stops.",
                 this->stop_position_ * 100.0f);
        this->position = this->stop_position_;
        this->last_operation_ = cover::COVER_OPERATION_IDLE;
        this->current_operation = cover::COVER_OPERATION_IDLE;
        this->publish_state();
      } else {
        // Arbitrary intermediate target — send STOP to halt the motor.
        this->send_stop();
      }
    } else
    // Auto-stop the estimate when we hit a limit (motor stops itself there).
    if (new_pos == 0.0f || new_pos == 1.0f) {
      this->last_operation_ = cover::COVER_OPERATION_IDLE;
      this->current_operation = cover::COVER_OPERATION_IDLE;
      this->publish_state();
    } else {
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
  this->stop_pending_ = false;
  if (call.get_stop()) {
    this->send_stop();
    return;
  }
  if (call.get_position().has_value()) {
    float target = *call.get_position();
    if (target > this->position) {
      this->send_up();
      // send_up() already sets the intercept at the next stop point.
      // If the explicit target is *before* that stop point, override with a
      // timed STOP (motor won't self-park at an arbitrary intermediate pos).
      if (this->stop_pending_ && target < this->stop_position_) {
        this->stop_position_ = target;
        this->stop_is_point_ = false;
      }
    } else if (target < this->position) {
      this->send_down();
      if (this->stop_pending_ && target > this->stop_position_) {
        this->stop_position_ = target;
        this->stop_is_point_ = false;
      }
    } else {
      if (target == 0.0f) {
        this->send_down();
      } else if (target == 1.0f) {
        this->send_up();
      }
    }
  }
}

void AOKCover::send_up() {
  this->position_at_op_start_ = this->position;
  this->operation_started_at_ = millis();
  this->last_operation_ = cover::COVER_OPERATION_OPENING;
  this->current_operation = cover::COVER_OPERATION_OPENING;
  // Intercept at the next programmed stop point in the opening direction.
  float next = next_stop_opening(this->stop_points_, this->position);
  if (next > 0.0f) {
    this->stop_pending_ = true;
    this->stop_position_ = next;
    this->stop_is_point_ = true;
  } else {
    this->stop_pending_ = false;
  }
  this->publish_state();
  this->schedule_after_();
  if (this->inverted_) {
    this->hub_->send_command(this->channel_, AOK_CMD_DOWN);
  } else {
    this->hub_->send_command(this->channel_, AOK_CMD_UP);
  }
}

void AOKCover::send_down() {
  this->position_at_op_start_ = this->position;
  this->operation_started_at_ = millis();
  this->last_operation_ = cover::COVER_OPERATION_CLOSING;
  this->current_operation = cover::COVER_OPERATION_CLOSING;
  // Intercept at the next programmed stop point in the closing direction.
  float next = next_stop_closing(this->stop_points_, this->position);
  if (next >= 0.0f) {
    this->stop_pending_ = true;
    this->stop_position_ = next;
    this->stop_is_point_ = true;
  } else {
    this->stop_pending_ = false;
  }
  this->publish_state();
  this->schedule_after_();
  if (this->inverted_) {
    this->hub_->send_command(this->channel_, AOK_CMD_UP);
  } else {
    this->hub_->send_command(this->channel_, AOK_CMD_DOWN);
  }
}

void AOKCover::send_stop() {
  this->last_operation_ = cover::COVER_OPERATION_IDLE;
  this->current_operation = cover::COVER_OPERATION_IDLE;
  // Cancel any pending AFTER since STOP resets motor state on its own.
  this->after_pending_ = false;
  this->publish_state();
  this->hub_->send_command(this->channel_, AOK_CMD_STOP);
}

void AOKCover::schedule_after_() {
  if (!this->send_after_)
    return;
  this->after_pending_ = true;
  this->after_due_at_ = millis() + this->after_delay_;
}

}  // namespace aok
}  // namespace esphome
