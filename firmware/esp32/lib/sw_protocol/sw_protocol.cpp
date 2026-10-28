#include "sw_protocol.h"

#include <cstring>

namespace smart_window {

std::uint16_t crc16Ccitt(const std::uint8_t *data, std::size_t length) {
  std::uint16_t crc = 0xFFFFu;
  if (data == nullptr) {
    return crc;
  }
  for (std::size_t index = 0; index < length; ++index) {
    crc ^= static_cast<std::uint16_t>(data[index]) << 8u;
    for (std::uint8_t bit = 0; bit < 8u; ++bit) {
      if ((crc & 0x8000u) != 0u) {
        crc = static_cast<std::uint16_t>((crc << 1u) ^ 0x1021u);
      } else {
        crc = static_cast<std::uint16_t>(crc << 1u);
      }
    }
  }
  return crc;
}

std::size_t encodeFrame(MessageType type,
                        std::uint8_t sequence,
                        const std::uint8_t *payload,
                        std::uint8_t payload_length,
                        std::uint8_t *output,
                        std::size_t output_capacity) {
  const std::size_t frame_size = kFrameOverhead + payload_length;
  if ((output == nullptr) || (frame_size > output_capacity) ||
      (payload_length > kMaxPayload) ||
      ((payload == nullptr) && (payload_length > 0u))) {
    return 0u;
  }

  output[0] = kSync0;
  output[1] = kSync1;
  output[2] = kProtocolVersion;
  output[3] = static_cast<std::uint8_t>(type);
  output[4] = sequence;
  output[5] = payload_length;
  if (payload_length > 0u) {
    std::memcpy(&output[6], payload, payload_length);
  }
  const std::uint16_t crc =
      crc16Ccitt(&output[2], 4u + static_cast<std::size_t>(payload_length));
  output[6u + payload_length] = static_cast<std::uint8_t>(crc & 0xFFu);
  output[7u + payload_length] = static_cast<std::uint8_t>(crc >> 8u);
  return frame_size;
}

void FrameParser::reset() {
  length_ = 0u;
  errors_ = 0u;
}

void FrameParser::push(std::uint8_t value) {
  if (length_ >= sizeof(buffer_)) {
    removePrefix(1u);
  }
  buffer_[length_++] = value;
}

void FrameParser::removePrefix(std::size_t count) {
  if (count >= length_) {
    length_ = 0u;
    return;
  }
  std::memmove(buffer_, &buffer_[count], length_ - count);
  length_ -= count;
}

std::ptrdiff_t FrameParser::findSync() const {
  if (length_ < 2u) {
    return -1;
  }
  for (std::size_t index = 0u; index + 1u < length_; ++index) {
    if ((buffer_[index] == kSync0) && (buffer_[index + 1u] == kSync1)) {
      return static_cast<std::ptrdiff_t>(index);
    }
  }
  return -1;
}

bool FrameParser::pop(Frame &frame) {
  while (true) {
    const std::ptrdiff_t sync = findSync();
    if (sync < 0) {
      if ((length_ > 0u) && (buffer_[length_ - 1u] == kSync0)) {
        buffer_[0] = kSync0;
        length_ = 1u;
      } else {
        length_ = 0u;
      }
      return false;
    }
    if (sync > 0) {
      removePrefix(static_cast<std::size_t>(sync));
    }
    if (length_ < 6u) {
      return false;
    }

    const std::uint8_t version = buffer_[2];
    const std::uint8_t payload_length = buffer_[5];
    if ((version != kProtocolVersion) || (payload_length > kMaxPayload)) {
      ++errors_;
      removePrefix(1u);
      continue;
    }

    const std::size_t total = kFrameOverhead + payload_length;
    if (length_ < total) {
      return false;
    }

    const std::uint16_t expected =
        static_cast<std::uint16_t>(buffer_[total - 2u]) |
        (static_cast<std::uint16_t>(buffer_[total - 1u]) << 8u);
    const std::uint16_t actual = crc16Ccitt(&buffer_[2], total - 4u);
    if (expected != actual) {
      ++errors_;
      removePrefix(1u);
      continue;
    }

    frame.type = buffer_[3];
    frame.sequence = buffer_[4];
    frame.length = payload_length;
    if (payload_length > 0u) {
      std::memcpy(frame.payload, &buffer_[6], payload_length);
    }
    removePrefix(total);
    return true;
  }
}

}  // namespace smart_window
