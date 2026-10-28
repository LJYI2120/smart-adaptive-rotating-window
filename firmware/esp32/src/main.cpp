#include <Arduino.h>

#include "board_config.h"
#include "angle_feedback.h"
#include "motion_controller.h"
#include "sensor_hub.h"
#include "sw_protocol.h"

using smart_window::CommandResult;
using smart_window::FaultCode;
using smart_window::Frame;
using smart_window::FrameParser;
using smart_window::MessageType;

namespace {

MotionController motion;
SensorHub sensors;
AngleFeedback angle_feedback;
FrameParser parser;
LightReading light_reading;

uint8_t transmit_sequence = 0u;
uint8_t operating_mode = 4u;
uint32_t last_sensor_start_ms = 0u;
uint32_t last_telemetry_ms = 0u;
uint8_t reported_fault = 0u;
uint32_t last_angle_feedback_ms = 0u;
uint32_t last_angle_change_ms = 0u;
uint16_t last_raw_angle = 0u;
bool raw_angle_valid = false;

void writeU16(uint8_t *output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value & 0xFFu);
  output[1] = static_cast<uint8_t>(value >> 8u);
}

void writeU32(uint8_t *output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value & 0xFFu);
  output[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
  output[2] = static_cast<uint8_t>((value >> 16u) & 0xFFu);
  output[3] = static_cast<uint8_t>((value >> 24u) & 0xFFu);
}

void sendMessage(MessageType type,
                 const uint8_t *payload,
                 uint8_t payload_length) {
  uint8_t frame[smart_window::kMaxFrameSize] = {};
  const size_t frame_length = smart_window::encodeFrame(
      type,
      transmit_sequence++,
      payload,
      payload_length,
      frame,
      sizeof(frame));
  if (frame_length > 0u) {
    Serial.write(frame, frame_length);
  }
}

void sendAck(const Frame &request, CommandResult result) {
  uint8_t payload[3] = {};
  payload[0] = request.sequence;
  payload[1] = request.type;
  payload[2] = static_cast<uint8_t>(result);
  sendMessage(MessageType::kAck, payload, sizeof(payload));
}

void handleHello(const Frame &request) {
  uint8_t payload[7] = {
      'S',
      'W',
      'C',
      '1',
      board::kFirmwareMajor,
      board::kFirmwareMinor,
      board::kFirmwarePatch,
  };
  sendMessage(MessageType::kHelloAck,
              payload,
              static_cast<uint8_t>(sizeof(payload)));
  (void)request;
}

void handleCommand(const Frame &frame, uint32_t now_ms) {
  motion.noteCommand(now_ms);

  switch (static_cast<MessageType>(frame.type)) {
    case MessageType::kHello:
      handleHello(frame);
      break;

    case MessageType::kSetTarget: {
      if (frame.length != 2u) {
        sendAck(frame, CommandResult::kInvalidPayload);
        break;
      }
      const int16_t angle = static_cast<int16_t>(
          static_cast<uint16_t>(frame.payload[0]) |
          (static_cast<uint16_t>(frame.payload[1]) << 8u));
      sendAck(frame,
              motion.setTargetCentideg(angle)
                  ? CommandResult::kSuccess
                  : CommandResult::kRejected);
      break;
    }

    case MessageType::kSetMode:
      if ((frame.length != 1u) || (frame.payload[0] > 4u)) {
        sendAck(frame, CommandResult::kInvalidPayload);
        break;
      }
      operating_mode = frame.payload[0];
      sendAck(frame, CommandResult::kSuccess);
      break;

    case MessageType::kStart: {
      const MotionStartResult result = motion.start(now_ms);
      if (result == MotionStartResult::kStarted) {
        sendAck(frame, CommandResult::kSuccess);
      } else if (result == MotionStartResult::kEmergencyStop) {
        sendAck(frame, CommandResult::kEmergencyStop);
      } else {
        sendAck(frame, CommandResult::kMotorFault);
      }
      break;
    }

    case MessageType::kStop:
      motion.stop();
      operating_mode = 4u;
      sendAck(frame, CommandResult::kSuccess);
      break;

    case MessageType::kClearFault:
      if (motion.clearFault(now_ms)) {
        reported_fault = 0u;
        sendAck(frame, CommandResult::kSuccess);
      } else {
        sendAck(frame, CommandResult::kEmergencyStop);
      }
      break;

    case MessageType::kHeartbeat:
      break;

    default:
      sendAck(frame, CommandResult::kUnknownCommand);
      break;
  }
}

void sendTelemetry(uint32_t now_ms) {
  uint8_t payload[18] = {};
  const uint16_t status = motion.statusBits();
  writeU16(&payload[0], static_cast<uint16_t>(motion.currentAngleCentideg()));
  writeU16(&payload[2], status);
  writeU16(&payload[4], light_reading.left);
  writeU16(&payload[6], light_reading.right);
  writeU16(&payload[8], light_reading.up);
  writeU16(&payload[10], light_reading.down);
  payload[12] = operating_mode;
  payload[13] = light_reading.valid_mask;
  writeU32(&payload[14], now_ms / 1000u);
  sendMessage(MessageType::kTelemetry, payload, sizeof(payload));
}

void sendFaultIfChanged() {
  const uint8_t fault = motion.faultCode();
  if ((fault != 0u) && (fault != reported_fault)) {
    const uint8_t payload[2] = {
        fault,
        static_cast<uint8_t>(motion.statusBits() & 0xFFu),
    };
    sendMessage(MessageType::kFault, payload, sizeof(payload));
    reported_fault = fault;
  } else if (fault == 0u) {
    reported_fault = 0u;
  }
}

void serviceAngleFeedback(uint32_t now_ms) {
  if (!board::kEnableAs5600 ||
      ((now_ms - last_angle_feedback_ms) < board::kAngleFeedbackPeriodMs)) {
    return;
  }
  last_angle_feedback_ms = now_ms;

  uint16_t raw_angle = 0u;
  if (!angle_feedback.readRaw(raw_angle)) {
    return;
  }
  if (!raw_angle_valid) {
    last_raw_angle = raw_angle;
    last_angle_change_ms = now_ms;
    raw_angle_valid = true;
    return;
  }

  const uint16_t difference = static_cast<uint16_t>(
      (raw_angle + 4096u - last_raw_angle) % 4096u);
  const bool changed =
      difference >= board::kNoMotionRawDelta &&
      difference <= (4096u - board::kNoMotionRawDelta);
  if (changed) {
    last_raw_angle = raw_angle;
    last_angle_change_ms = now_ms;
  } else if (motion.isMoving() &&
             ((now_ms - last_angle_change_ms) >= board::kNoMotionFaultMs)) {
    motion.markMotorFault();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  const uint32_t now_ms = millis();
  motion.begin(now_ms);
  sensors.begin();
  angle_feedback.begin();
  last_sensor_start_ms = now_ms;
  last_telemetry_ms = now_ms;
  last_angle_feedback_ms = now_ms;
  last_angle_change_ms = now_ms;
}

void loop() {
  const uint32_t now_ms = millis();

  while (Serial.available() > 0) {
    parser.push(static_cast<uint8_t>(Serial.read()));
  }

  Frame frame;
  while (parser.pop(frame)) {
    handleCommand(frame, now_ms);
  }

  sensors.service(now_ms, light_reading);
  motion.markSensorFault(light_reading.valid_mask != 0x0Fu);
  serviceAngleFeedback(now_ms);
  motion.service(now_ms);

  if (!sensors.startMeasurement(now_ms) &&
      ((now_ms - last_sensor_start_ms) >= 500u)) {
    last_sensor_start_ms = now_ms;
    sensors.startMeasurement(now_ms);
  }

  sendFaultIfChanged();
  if ((now_ms - last_telemetry_ms) >= board::kTelemetryPeriodMs) {
    sendTelemetry(now_ms);
    last_telemetry_ms = now_ms;
  }
}
