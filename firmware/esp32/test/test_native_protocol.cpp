#include <cassert>
#include <cstdint>
#include <cstring>

#include "sw_protocol.h"

int main() {
  using namespace smart_window;

  assert(crc16Ccitt(reinterpret_cast<const std::uint8_t *>("123456789"),
                    9u) == 0x29B1u);

  const std::uint8_t payload[2] = {0x8E, 0xFF};
  std::uint8_t encoded[kMaxFrameSize] = {};
  const size_t length =
      encodeFrame(MessageType::kSetTarget,
                  7u,
                  payload,
                  sizeof(payload),
                  encoded,
                  sizeof(encoded));
  assert(length == 10u);

  FrameParser parser;
  Frame frame;
  for (size_t index = 0u; index < length; ++index) {
    parser.push(encoded[index]);
  }
  assert(parser.pop(frame));
  assert(frame.type == static_cast<std::uint8_t>(MessageType::kSetTarget));
  assert(frame.sequence == 7u);
  assert(frame.length == 2u);
  assert(frame.payload[0] == 0x8Eu);
  assert(frame.payload[1] == 0xFFu);

  const std::uint8_t angle_payload[2] = {0x2E, 0xFB};
  const std::uint8_t expected_frame[10] = {
      0xAA, 0x55, 0x01, 0x10, 0x07, 0x02, 0x2E, 0xFB, 0x3A, 0x0B,
  };
  const size_t expected_length =
      encodeFrame(MessageType::kSetTarget,
                  7u,
                  angle_payload,
                  sizeof(angle_payload),
                  encoded,
                  sizeof(encoded));
  assert(expected_length == sizeof(expected_frame));
  assert(std::memcmp(encoded, expected_frame, sizeof(expected_frame)) == 0);

  encoded[length - 1u] ^= 0x80u;
  for (size_t index = 0u; index < length; ++index) {
    parser.push(encoded[index]);
  }
  assert(!parser.pop(frame));
  assert(parser.errorCount() > 0u);
  return 0;
}
