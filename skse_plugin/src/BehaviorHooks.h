#pragma once

#include "CCWConfig.h"
#include <functional>
#include <vector>

namespace CCW {

// Behavior Hooks - Hooks into Skyrim's Havok Behavior system
// to intercept and replace animations at runtime
class BehaviorHooks {
public:
  static BehaviorHooks &GetSingleton();

  // Install all hooks
  bool Install();
  void Uninstall();

  // Animation clip path override
  // When Havok tries to load a clip, check if we have a CCW replacement
  void RegisterClipOverride(const std::string &vanillaClipName,
                            const std::string &ccwClipPath);
  void UnregisterClipOverride(const std::string &vanillaClipName);

  // Check if a clip should be overridden
  const std::string *GetClipOverride(const std::string &clipName) const;

  // Dynamic animation graph variable manipulation
  void SetGraphVariable(RE::Actor *actor, const RE::BSFixedString &varName,
                        float value);
  void SetGraphVariable(RE::Actor *actor, const RE::BSFixedString &varName,
                        int value);
  void SetGraphVariable(RE::Actor *actor, const RE::BSFixedString &varName,
                        bool value);

private:
  BehaviorHooks() = default;

  // Hook targets
  // These are the actual function hooks installed into the game

  // Hook: hkbClipGenerator::Activate
  // Called when a clip is about to start playing
  static void Hook_ClipGenerator_Activate(RE::hkbClipGenerator *clipGen,
                                          const RE::hkbContext &context);
  static inline REL::Relocation<decltype(&Hook_ClipGenerator_Activate)>
      _original_ClipGenerator_Activate;

  // Hook: hkbClipGenerator::Generate
  // Called each frame to generate animation output
  static void
  Hook_ClipGenerator_Generate(RE::hkbClipGenerator *clipGen,
                              const RE::hkbContext &context,
                              const RE::hkbGeneratorOutput **output);
  static inline REL::Relocation<decltype(&Hook_ClipGenerator_Generate)>
      _original_ClipGenerator_Generate;

  // Hook: Actor::ProcessAnimationGraphEvent
  // Called when animation graph events fire
  static void Hook_ProcessAnimGraphEvent(
      RE::BSTEventSink<RE::BSAnimationGraphEvent> *sink,
      RE::BSAnimationGraphEvent *event,
      RE::BSTEventSource<RE::BSAnimationGraphEvent> *source);

  // Clip override table
  mutable std::shared_mutex m_overrideMutex;
  std::unordered_map<std::string, std::string> m_clipOverrides;

  bool m_installed = false;
};

} // namespace CCW
