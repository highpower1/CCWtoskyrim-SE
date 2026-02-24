#include "InputBuffer.h"

namespace CCW {

InputBuffer &InputBuffer::GetSingleton() {
  static InputBuffer instance;
  return instance;
}

void InputBuffer::BufferAttack(RE::Actor *actor, bool isHeavy,
                               AttackDirection dir) {
  if (!actor)
    return;

  std::unique_lock lock(m_mutex);

  BufferedInput input;
  input.isHeavy = isHeavy;
  input.direction = dir;
  input.timestamp = m_gameTime;

  auto &buffer = m_buffers[actor->GetFormID()];

  // Limit queue size to prevent memory issues
  if (buffer.attackQueue.size() >= 3) {
    buffer.attackQueue.pop_front();
  }

  buffer.attackQueue.push_back(input);

  logger::trace("CCW InputBuffer: Buffered {} attack for actor 0x{:X}",
                isHeavy ? "heavy" : "light", actor->GetFormID());
}

void InputBuffer::BufferDodge(RE::Actor *actor, AttackDirection dir) {
  if (!actor)
    return;

  std::unique_lock lock(m_mutex);

  BufferedInput input;
  input.isHeavy = false;
  input.direction = dir;
  input.timestamp = m_gameTime;

  auto &buffer = m_buffers[actor->GetFormID()];
  if (buffer.dodgeQueue.size() >= 2) {
    buffer.dodgeQueue.pop_front();
  }
  buffer.dodgeQueue.push_back(input);
}

std::optional<BufferedInput>
InputBuffer::ConsumeBufferedAttack(RE::Actor *actor) {
  if (!actor)
    return std::nullopt;

  std::unique_lock lock(m_mutex);

  auto it = m_buffers.find(actor->GetFormID());
  if (it == m_buffers.end() || it->second.attackQueue.empty()) {
    return std::nullopt;
  }

  auto &queue = it->second.attackQueue;

  // Find the first non-expired input
  while (!queue.empty()) {
    auto &front = queue.front();
    if (m_gameTime - front.timestamp > m_bufferDuration) {
      queue.pop_front(); // Expired
      continue;
    }
    // Valid buffered input
    BufferedInput result = front;
    queue.pop_front();
    logger::trace("CCW InputBuffer: Consumed buffered attack for actor 0x{:X}",
                  actor->GetFormID());
    return result;
  }

  return std::nullopt;
}

std::optional<BufferedInput>
InputBuffer::ConsumeBufferedDodge(RE::Actor *actor) {
  if (!actor)
    return std::nullopt;

  std::unique_lock lock(m_mutex);

  auto it = m_buffers.find(actor->GetFormID());
  if (it == m_buffers.end() || it->second.dodgeQueue.empty()) {
    return std::nullopt;
  }

  auto &queue = it->second.dodgeQueue;
  while (!queue.empty()) {
    auto &front = queue.front();
    if (m_gameTime - front.timestamp > m_bufferDuration) {
      queue.pop_front();
      continue;
    }
    BufferedInput result = front;
    queue.pop_front();
    return result;
  }

  return std::nullopt;
}

void InputBuffer::ClearBuffer(RE::Actor *actor) {
  if (!actor)
    return;

  std::unique_lock lock(m_mutex);
  m_buffers.erase(actor->GetFormID());
}

void InputBuffer::Update(float deltaTime) {
  std::unique_lock lock(m_mutex);

  m_gameTime += deltaTime;

  // Periodically clean up empty buffers
  for (auto it = m_buffers.begin(); it != m_buffers.end();) {
    auto &buffer = it->second;

    // Remove expired entries
    while (!buffer.attackQueue.empty() &&
           m_gameTime - buffer.attackQueue.front().timestamp >
               m_bufferDuration) {
      buffer.attackQueue.pop_front();
    }
    while (!buffer.dodgeQueue.empty() &&
           m_gameTime - buffer.dodgeQueue.front().timestamp >
               m_bufferDuration) {
      buffer.dodgeQueue.pop_front();
    }

    // Remove empty actor entries
    if (buffer.attackQueue.empty() && buffer.dodgeQueue.empty()) {
      it = m_buffers.erase(it);
    } else {
      ++it;
    }
  }
}

void InputBuffer::SetBufferDuration(float seconds) {
  m_bufferDuration = std::clamp(seconds, 0.05f, 1.0f);
}

} // namespace CCW
