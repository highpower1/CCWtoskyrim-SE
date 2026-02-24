#pragma once

#include "CCWConfig.h"
#include <chrono>
#include <deque>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace CCW {

// A buffered attack input
struct BufferedInput {
  bool isHeavy = false;
  AttackDirection direction = AttackDirection::Neutral;
  float timestamp = 0.0f; // Game time when buffered
};

// Input Buffer - Elden Ring-style input queuing for responsive combat
class InputBuffer {
public:
  static InputBuffer &GetSingleton();

  // Buffer management
  void BufferAttack(RE::Actor *actor, bool isHeavy,
                    AttackDirection dir = AttackDirection::Neutral);
  void BufferDodge(RE::Actor *actor,
                   AttackDirection dir = AttackDirection::Back);
  std::optional<BufferedInput> ConsumeBufferedAttack(RE::Actor *actor);
  std::optional<BufferedInput> ConsumeBufferedDodge(RE::Actor *actor);
  void ClearBuffer(RE::Actor *actor);

  // Per-frame update - expire old buffered inputs
  void Update(float deltaTime);

  // Configuration
  void SetBufferDuration(float seconds);
  float GetBufferDuration() const { return m_bufferDuration; }

private:
  InputBuffer() = default;

  struct ActorBuffer {
    std::deque<BufferedInput> attackQueue;
    std::deque<BufferedInput> dodgeQueue;
  };

  mutable std::shared_mutex m_mutex;
  std::unordered_map<RE::FormID, ActorBuffer> m_buffers;
  float m_bufferDuration = Combo::INPUT_BUFFER_DURATION;
  float m_gameTime = 0.0f;
};

} // namespace CCW
