#pragma once

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "cover.h"

namespace esphome {
namespace aok {

enum AOKButtonAction {
  AOK_BUTTON_PROGRAM = 0,
  AOK_BUTTON_CHANGE_DIR = 1,
};

class AOKButton : public button::Button, public Component {
 public:
  void set_cover(AOKCover *cov) { cover_ = cov; }
  void set_action(AOKButtonAction action) { action_ = action; }

 protected:
  void press_action() override {
    switch (action_) {
      case AOK_BUTTON_PROGRAM:
        cover_->send_program();
        break;
      case AOK_BUTTON_CHANGE_DIR:
        cover_->send_change_direction();
        break;
    }
  }

  AOKCover *cover_{nullptr};
  AOKButtonAction action_{AOK_BUTTON_PROGRAM};
};

}  // namespace aok
}  // namespace esphome
