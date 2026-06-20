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
  for (float sp : points) {  // sorted ascending
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

// Returns the nearest stop point to `target` within `tolerance`, or -1 if none.
static float nearest_stop_point(const std::vector<float> &points, float target, float tolerance = 0.03f) {
  float best = -1.0f;
  float best_dist = tolerance;
  for (float sp : points) {
    float d = std::abs(sp - target);
    if (d < best_dist) {
      best_dist = d;
      best = sp;
    }
  }
  return best;
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
    ESP_LOGCONFIG(TAG, "  Resume factor:   %.2f×", this->resume_factor_);
    ESP_LOGCONFIG(TAG, "  Resume buffer:   %u ms", this->resume_buffer_ms_);
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
        // Motor self-parks at its programmed preset.
        this->position = this->stop_position_;
        this->last_operation_ = cover::COVER_OPERATION_IDLE;
        this->current_operation = cover::COVER_OPERATION_IDLE;
        this->publish_state();
        ESP_LOGD(TAG, "Reached stop point %.0f%%.", this->stop_position_ * 100.0f);

        // If there is still further to go toward the final target, schedule a resume.
        bool has_further = (this->final_target_ >= 0.0f) &&
                           (std::abs(this->final_target_ - this->position) > 0.01f);
        if (has_further) {
          float travel_duration = now - this->operation_started_at_;
          uint32_t resume_after_ms = (travel_duration * this->resume_factor_) + this->resume_buffer_ms_;
          ESP_LOGD(TAG, "scheduling resume toward %.0f%% in %u ms (travel duration %.0f ms, factor %.1f, buffer %u ms).", 
                   this->final_target_ * 100.0f, 
                   resume_after_ms, 
                   travel_duration,
                   this->resume_factor_, 
                   this->resume_buffer_ms_);

          this->resume_pending_ = true;
          this->resume_at_ = now + resume_after_ms;

          this->resume_direction_ = (this->final_target_ > this->position)
                                        ? cover::COVER_OPERATION_OPENING
                                        : cover::COVER_OPERATION_CLOSING;
        } else {
          // Stop point IS the final target — clear it.
          this->final_target_ = -1.0f;
        }
      } else {
        // Arbitrary intermediate target — send STOP to physically halt the motor.
        this->final_target_ = -1.0f;
        this->send_stop();
      }
    } else {
      // Auto-stop the estimate when we hit a travel-time limit.
      if (new_pos == 0.0f || new_pos == 1.0f) {
        this->final_target_ = -1.0f;
        this->last_operation_ = cover::COVER_OPERATION_IDLE;
        this->current_operation = cover::COVER_OPERATION_IDLE;
      }
      this->publish_state();
    }
  }

  // 2) Fire a pending resume after the delay has elapsed.
  if (this->resume_pending_ && now >= this->resume_at_) {
    this->resume_pending_ = false;
    ESP_LOGD(TAG, "Resuming toward %.0f%%.", this->final_target_ * 100.0f);
    this->log_next_intercept_("Resuming");
    if (this->resume_direction_ == cover::COVER_OPERATION_OPENING) {
      this->send_up();
    } else {
      this->send_down();
    }
    // send_up/down sets the intercept at the next stop point; if the final
    // target is before that stop point, override to a timed STOP.
    if (this->stop_pending_) {
      bool target_is_before_intercept =
          (this->resume_direction_ == cover::COVER_OPERATION_OPENING && this->final_target_ < this->stop_position_) ||
          (this->resume_direction_ == cover::COVER_OPERATION_CLOSING && this->final_target_ > this->stop_position_);
      if (target_is_before_intercept) {
        // Check if the target snaps to a stop point first.
        this->apply_snapped_or_arbitrary_target(this->final_target_);
      }
    }
  }

  // 3) Send a queued AFTER packet when its delay elapses.
  if (this->after_pending_ && now >= this->after_due_at_) {
    this->after_pending_ = false;
    this->hub_->send_command(this->channel_, AOK_CMD_AFTER);
  }
}

void AOKCover::log_next_intercept_(const char *context) {
  // Use last_operation_ so this is correct whether called from send_up/down
  // directly or after a resume (where resume_direction_ would also be set,
  // but last_operation_ is always the authoritative current direction).
  auto dir = this->last_operation_;
  const char *mode = this->stop_is_point_ ? "stop point" : "timed stop";

  if (this->stop_pending_) {
    ESP_LOGD(TAG, "%s: moving toward %.0f%% — next intercept at %.0f%% (%s).",
             context,
             this->final_target_ >= 0.0f ? this->final_target_ * 100.0f
                                         : (dir == cover::COVER_OPERATION_OPENING ? 100.0f : 0.0f),
             this->stop_position_ * 100.0f,
             mode);
  } else {
    float implied_target = (dir == cover::COVER_OPERATION_OPENING) ? 100.0f : 0.0f;
    ESP_LOGD(TAG, "%s: moving toward %.0f%% — no intercept, running to limit.",
             context,
             this->final_target_ >= 0.0f ? this->final_target_ * 100.0f : implied_target);
  }
}

void AOKCover::apply_snapped_or_arbitrary_target(float target) {
  float snapped = nearest_stop_point(this->stop_points_, target);
  stop_pending_ = true;
  if (snapped >= 0.0f) {
    this->stop_position_ = snapped;
    this->stop_is_point_ = true;
  } else {
    this->stop_position_ = target;
    this->stop_is_point_ = false;
  }
}

void AOKCover::control(const cover::CoverCall &call) {
  // Any new command cancels a pending resume.
  this->resume_pending_ = false;
  this->stop_pending_ = false;

  if (call.get_stop()) {
    this->final_target_ = -1.0f;
    this->send_stop();
    return;
  }

  if (call.get_position().has_value()) {
    float target = *call.get_position();
    this->final_target_ = target;

    if (target > this->position) {
      this->send_up();
      // send_up() already intercepts at the next stop point.
      // If the explicit target is *before* that intercept, we need to override.
      if (this->stop_pending_ && target < this->stop_position_) {
        this->apply_snapped_or_arbitrary_target(target);
      }
    } else if (target < this->position) {
      this->send_down();
      if (this->stop_pending_ && target > this->stop_position_) {
        this->apply_snapped_or_arbitrary_target(target);
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

void AOKCover::start_movement_(cover::CoverOperation direction, float next_stop, uint8_t cmd_normal, uint8_t cmd_inverted) {
  this->position_at_op_start_ = this->position;
  this->operation_started_at_ = millis();
  this->last_operation_ = direction;
  this->current_operation = direction;

  bool opening = (direction == cover::COVER_OPERATION_OPENING);

  if (this->final_target_ >= 0.0f) {
    bool target_at_or_beyond_stop = next_stop >= 0.0f &&
        (opening ? this->final_target_ >= next_stop - 0.01f
                 : this->final_target_ <= next_stop + 0.01f);
    if (target_at_or_beyond_stop) {
      this->stop_pending_ = true;
      this->stop_position_ = next_stop;
      this->stop_is_point_ = true;
    } else {
      this->apply_snapped_or_arbitrary_target(this->final_target_);
    }
  } else {
    if (next_stop >= 0.0f) {
      this->stop_pending_ = true;
      this->stop_position_ = next_stop;
      this->stop_is_point_ = true;
    } else {
      this->stop_pending_ = false;
    }
  }

  this->publish_state();
  this->schedule_after_();
  this->log_next_intercept_("Starting");
  this->hub_->send_command(this->channel_, this->inverted_ ? cmd_inverted : cmd_normal);
}

void AOKCover::send_up() {
  start_movement_(cover::COVER_OPERATION_OPENING,
                  next_stop_opening(this->stop_points_, this->position),
                  AOK_CMD_UP, AOK_CMD_DOWN);
}

void AOKCover::send_down() {
  start_movement_(cover::COVER_OPERATION_CLOSING,
                  next_stop_closing(this->stop_points_, this->position),
                  AOK_CMD_DOWN, AOK_CMD_UP);
}

void AOKCover::send_stop() {
  this->last_operation_ = cover::COVER_OPERATION_IDLE;
  this->current_operation = cover::COVER_OPERATION_IDLE;
  this->stop_pending_ = false;
  this->resume_pending_ = false;
  this->final_target_ = -1.0f;
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
