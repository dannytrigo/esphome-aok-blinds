#include "aok.h"
#include "esphome/core/log.h"

namespace esphome {
namespace aok {

static const char *const TAG = "aok";

void AOKHub::dump_config() {
  ESP_LOGCONFIG(TAG, "A-OK Hub:");
  ESP_LOGCONFIG(TAG, "  Remote ID: 0x%06X", this->remote_id_ & 0xFFFFFF);
  ESP_LOGCONFIG(TAG, "  Repeats:   %u", this->repeats_);
}

uint8_t AOKHub::compute_checksum_(uint32_t id, uint16_t channel, uint8_t cmd) {
  // Per the protocol: 8-bit sum of the ID bytes, channel bytes, and command byte.
  // The start byte (0xA3) is NOT included.
  uint32_t sum = ((id >> 16) & 0xFF) + ((id >> 8) & 0xFF) + (id & 0xFF)
               + ((channel >> 8) & 0xFF) + (channel & 0xFF)
               + cmd;
  return static_cast<uint8_t>(sum & 0xFF);
}

void AOKHub::send_command(uint16_t channel, uint8_t cmd) {
  ESP_LOGD(TAG, "TX  remote=0x%06X  channel=0x%04X  cmd=0x%02X",
           this->remote_id_ & 0xFFFFFF, channel, cmd);
  this->encode_and_transmit_(channel, cmd);
}

void AOKHub::encode_and_transmit_(uint16_t channel, uint8_t cmd) {
  uint32_t id = this->remote_id_ & 0xFFFFFF;
  uint8_t checksum = compute_checksum_(id, channel, cmd);

  // Assemble the 65 bits, MSB first:
  //   [0xA3 start : 8][ID : 24][channel : 16][cmd : 8][checksum : 8][trailing 1 : 1]
  bool bits[65];
  int p = 0;

  auto put_bits = [&](uint32_t value, int width) {
    for (int i = width - 1; i >= 0; i--) {
      bits[p++] = (value >> i) & 1;
    }
  };

  put_bits(AOK_START_BYTE, 8);
  put_bits(id, 24);
  put_bits(channel, 16);
  put_bits(cmd, 8);
  put_bits(checksum, 8);
  bits[64] = true;  // trailing 1

  // Begin a transmission. RemoteTransmitter's TransmitCall takes care of
  // repeating the whole call N times via set_send_times(), so we only need
  // to build ONE frame and let the framework repeat it.
  auto call = this->transmitter_->transmit();
  auto *data = call.get_data();
  data->set_carrier_frequency(0);
  data->reserve(2 + 2 * 65 + 1);

  // AGC: HIGH ~5.3ms, LOW ~530us
  data->item(AOK_AGC_HIGH, AOK_AGC_LOW);

  // Tri-state encoding:
  //   bit 0 = HIGH short, LOW long  (100)
  //   bit 1 = HIGH long,  LOW short (110)
  for (int i = 0; i < 65; i++) {
    if (bits[i]) {
      data->item(AOK_PULSE_LONG, AOK_PULSE_SHORT);
    } else {
      data->item(AOK_PULSE_SHORT, AOK_PULSE_LONG);
    }
  }

  // Inter-frame radio silence. ESPHome's TransmitCall handles repeats by
  // replaying the whole buffer, so the silence at the end becomes the gap.
  data->space(AOK_RADIO_SILENCE);

  call.set_send_times(this->repeats_);
  call.set_send_wait(0);
  call.perform();
}

}  // namespace aok
}  // namespace esphome
