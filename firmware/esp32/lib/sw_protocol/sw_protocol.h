#ifndef SMART_WINDOW_PROTOCOL_H
#define SMART_WINDOW_PROTOCOL_H

#include <cstddef>
#include <cstdint>

namespace smart_window {

constexpr std::uint8_t kSync0 = 0xAA;
constexpr std::uint8_t kSync1 = 0x55;
constexpr std::uint8_t kProtocolVersion = 0x01;
constexpr std::size_t kMaxPayload = 64;
constexpr std::size_t kFrameOverhead = 8;
constexpr std::size_t kMaxFrameSize = kFrameOverhead + kMaxPayload;

enum class MessageType : std::uint8_t {
  kHello = 0x01,
  kSetTarget = 0x10,
  kSetMode = 0x11,
  kStart = 0x12,
  kStop = 0x13,
  kClearFault = 0x14,
  kHeartbeat = 0x15,
  kHelloAck = 0x81,
  kTelemetry = 0x82,
  kAck = 0x83,
  kFault = 0x84,
};

enum class CommandResult : std::uint8_t {
  kSuccess = 0,
  kRejected = 1,
  kInvalidPayload = 2,
  kUnknownCommand = 3,
  kNotHomed = 4,
  kEmergencyStop = 5,
  kMotorFault = 6,
  kLimitReached = 7,
};

enum class FaultCode : std::uint8_t {
  kNone = 0,
  kBadFrame = 1,
  kCommTimeout = 2,
  kLimit = 3,
  kEmergencyStop = 4,
  kSensor = 5,
  kMotor = 6,
  kHomingTimeout = 7,
  kInternal = 8,
};

enum StatusFlag : std::uint16_t {
  kStatusEnabled = 1u << 0,
  kStatusHoming = 1u << 1,
  kStatusMoving = 1u << 2,
  kStatusLimitMin = 1u << 3,
  kStatusLimitMax = 1u << 4,
  kStatusEmergencyStop = 1u << 5,
  kStatusCommTimeout = 1u << 6,
  kStatusSensorFault = 1u << 7,
  kStatusMotorFault = 1u << 8,
  kStatusHomed = 1u << 9,
};

struct Frame {
  std::uint8_t type = 0;
  std::uint8_t sequence = 0;
  std::uint8_t length = 0;
  std::uint8_t payload[kMaxPayload] = {};
};

std::uint16_t crc16Ccitt(const std::uint8_t *data, std::size_t length);

std::size_t encodeFrame(MessageType type,
                        std::uint8_t sequence,
                        const std::uint8_t *payload,
                        std::uint8_t payload_length,
                        std::uint8_t *output,
                        std::size_t output_capacity);

class FrameParser {
 public:
  void reset();
  void push(std::uint8_t value);
  bool pop(Frame &frame);

  std::uint32_t errorCount() const { return errors_; }

 private:
  void removePrefix(std::size_t count);
  std::ptrdiff_t findSync() const;

  std::uint8_t buffer_[kMaxFrameSize] = {};
  std::size_t length_ = 0;
  std::uint32_t errors_ = 0;
};

}  // namespace smart_window

#endif
