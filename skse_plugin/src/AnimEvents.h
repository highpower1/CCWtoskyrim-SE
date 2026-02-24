#pragma once

#include "CCWConfig.h"
#include <functional>
#include <vector>

namespace CCW {

// Custom animation event data
struct CCWAnimEvent {
  RE::Actor *actor = nullptr;
  std::string eventName;
  float timestamp = 0.0f;
  std::string clipName; // Which CCW clip triggered this
  int comboStep = 0;    // Current combo chain step
  WeaponCategory weaponType = WeaponCategory::Unarmed;
};

using AnimEventHandler = std::function<void(const CCWAnimEvent &)>;

// Animation Events System - Bridges between Skyrim's animation events
// and the CCW combo system. Also translates CCW TAE event data
// from Elden Ring format into Skyrim-compatible events.
class AnimEvents : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
public:
  static AnimEvents &GetSingleton();

  // Initialization
  bool Initialize();
  void Shutdown();

  // Register this as an event sink on the player
  void RegisterForPlayer();
  void UnregisterForPlayer();

  // Custom event handler registration
  void RegisterHandler(const std::string &eventName, AnimEventHandler handler);
  void UnregisterHandler(const std::string &eventName);

  // Process events from BehaviorHooks
  void ProcessAnimationGraphEvent(RE::BSAnimationGraphEvent *event);

  // BSTEventSink interface
  RE::BSEventNotifyControl
  ProcessEvent(const RE::BSAnimationGraphEvent *event,
               RE::BSTEventSource<RE::BSAnimationGraphEvent> *source) override;

private:
  AnimEvents() = default;

  // Translate vanilla Skyrim events to CCW events
  void TranslateEvent(RE::Actor *actor, const std::string &eventName);

  // Fire CCW custom events
  void FireCCWEvent(const CCWAnimEvent &event);

  // Handlers
  mutable std::shared_mutex m_mutex;
  std::unordered_map<std::string, std::vector<AnimEventHandler>> m_handlers;

  bool m_initialized = false;
  bool m_registeredForPlayer = false;
};

} // namespace CCW
