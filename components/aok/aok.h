#pragma once

#include "esphome/core/component.h"
#include "esphome/components/remote_transmitter/remote_transmitter.h"

namespace esphome {
namespace aok {

// A-OK protocol commands (from akirjavainen/A-OK reverse engineering)
static const uint8_t AOK_CMD_UP = 0x0B;
static const uint8_t AOK_CMD_DOWN = 0x43;
static const uint8_t AOK_CMD_STOP = 0x23;
static const uint8_t AOK_CMD_AFTER = 0x24;     // sent ~200ms after UP or DOWN
static const uint8_t AOK_CMD_PROGRAM = 0x53;   // pairing
static const uint8_t AOK_CMD_CHANGE_DIR = 0x50; // reverses motor direction

// Protocol timings (microseconds)
static const uint32_t AOK_AGC_HIGH = 5300;
static const uint32_t AOK_AGC_LOW = 530;
static const uint32_t AOK_PULSE_SHORT = 270;
static const uint32_t AOK_PULSE_LONG = 565;
static const uint32_t AOK_RADIO_SILENCE = 5030;

// Frame header byte (always 0xA3)
static const uint8_t AOK_START_BYTE = 0xA3;

class AOKHub : public Component {
 public:
  AOKHub(remote_transmitter::RemoteTransmitterComponent *transmitter,
         uint32_t remote_id, uint8_t repeats)
      : transmitter_(transmitter), remote_id_(remote_id), repeats_(repeats) {}

  void setup() override {}
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Transmit a single A-OK command frame.
  // channel: 16-bit bitmask (0x0001 = channel 1, 0x0002 = ch 2, etc.)
  // cmd:     one of AOK_CMD_* constants
  void send_command(uint16_t channel, uint8_t cmd);

  uint32_t get_remote_id() const { return remote_id_; }

 protected:
  // Encode the 65-bit A-OK frame into raw pulse timings and transmit.
  void encode_and_transmit_(uint16_t channel, uint8_t cmd);

  // Compute the protocol's 8-bit checksum: sum of ID bytes, address bytes, and command.
  static uint8_t compute_checksum_(uint32_t id, uint16_t channel, uint8_t cmd);

  remote_transmitter::RemoteTransmitterComponent *transmitter_;
  uint32_t remote_id_;
  uint8_t repeats_;
};

}  // namespace aok
}  // namespace esphome
