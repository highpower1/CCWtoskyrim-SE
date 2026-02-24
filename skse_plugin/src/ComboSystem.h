#pragma once

#include "AnimationManager.h"
#include "CCWConfig.h"
#include <chrono>
#include <deque>
#include <functional>
#include <optional>


namespace CCW {

// Represents the current state of a combo chain
struct ComboState {
  std::string currentClipName; // Currently playing animation
  int comboIndex = 0;          // Position in combo chain (0 = not attacking)
  float animProgress = 0.0f;   // Normalized animation progress [0, 1]
  float animElapsed = 0.0f;    // Seconds since animation started
  bool inComboWindow = false;  // Is combo input currently accepted?
  bool inCancelWindow = false; // Can the animation be cancelled?
  bool isHeavyChain = false;   // Is this a heavy attack chain?
  bool hitTriggered = false;   // Has the hit frame been reached?
  bool commitActive = false;   // Is the player committed to the action?
  WeaponCategory weaponCategory = WeaponCategory::Unarmed;

  void Reset() {
    currentClipName.clear();
    comboIndex = 0;
    animProgress = 0.0f;
    animElapsed = 0.0f;
    inComboWindow = false;
    inCancelWindow = false;
    isHeavyChain = false;
    hitTriggered = false;
    commitActive = false;
  }

  bool IsActive() const { return comboIndex > 0; }
};

// Callback types for combo events
using ComboEventCallback = std::function<void(RE::Actor *, const ComboState &)>;

// Combo System - Manages attack chains and timing windows
class ComboSystem {
public:
  static ComboSystem &GetSingleton();

  // Initialization
  bool Initialize();
  void Shutdown();

  // Per-frame update (called from game loop hook)
  void Update(float deltaTime);

  // Attack input handling
  bool TryStartAttack(RE::Actor *actor, bool isHeavy = false,
                      AttackDirection dir = AttackDirection::Neutral);
  bool TryChainAttack(RE::Actor *actor, bool isHeavy = false);
  void CancelCombo(RE::Actor *actor);

  // State queries
  const ComboState *GetComboState(RE::Actor *actor) const;
  bool IsInCombo(RE::Actor *actor) const;
  bool IsInComboWindow(RE::Actor *actor) const;
  bool IsCommitted(RE::Actor *actor) const;

  // Event callbacks
  void RegisterOnHitCallback(ComboEventCallback callback);
  void RegisterOnComboEndCallback(ComboEventCallback callback);
  void RegisterOnComboChainCallback(ComboEventCallback callback);

  // Animation event notifications (called by AnimEvents system)
  void OnAnimationEvent(RE::Actor *actor, const RE::BSFixedString &eventName);
  void OnAnimationProgress(RE::Actor *actor, float normalizedTime);

private:
  ComboSystem() = default;
  ~ComboSystem() = default;
  ComboSystem(const ComboSystem &) = delete;
  ComboSystem &operator=(const ComboSystem &) = delete;

  // Internal combo logic
  bool StartCombo(RE::Actor *actor, const AnimationClip *clip, bool isHeavy);
  bool AdvanceCombo(RE::Actor *actor, bool isHeavy);
  void EndCombo(RE::Actor *actor);
  void UpdateComboWindows(RE::Actor *actor, ComboState &state);

  // Play a clip on an actor
  bool PlayAnimation(RE::Actor *actor, const AnimationClip *clip);

  // Per-actor combo states
  mutable std::shared_mutex m_mutex;
  std::unordered_map<RE::FormID, ComboState> m_comboStates;

  // Callbacks
  std::vector<ComboEventCallback> m_onHitCallbacks;
  std::vector<ComboEventCallback> m_onComboEndCallbacks;
  std::vector<ComboEventCallback> m_onComboChainCallbacks;

  bool m_initialized = false;
};

} // namespace CCW
