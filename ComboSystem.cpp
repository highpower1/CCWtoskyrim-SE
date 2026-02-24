#include "ComboSystem.h"
#include "InputBuffer.h"

namespace CCW {

ComboSystem &ComboSystem::GetSingleton() {
  static ComboSystem instance;
  return instance;
}

bool ComboSystem::Initialize() {
  logger::info("CCW ComboSystem: Initializing...");
  m_initialized = true;
  return true;
}

void ComboSystem::Shutdown() {
  std::unique_lock lock(m_mutex);
  m_comboStates.clear();
  m_onHitCallbacks.clear();
  m_onComboEndCallbacks.clear();
  m_onComboChainCallbacks.clear();
  m_initialized = false;
  logger::info("CCW ComboSystem: Shutdown");
}

void ComboSystem::Update(float deltaTime) {
  if (!m_initialized)
    return;

  std::unique_lock lock(m_mutex);

  for (auto it = m_comboStates.begin(); it != m_comboStates.end();) {
    auto &[formID, state] = *it;

    if (!state.IsActive()) {
      ++it;
      continue;
    }

    // Update animation timing
    state.animElapsed += deltaTime;
    auto *clip =
        AnimationManager::GetSingleton().GetClip(state.currentClipName);
    if (clip && clip->duration > 0) {
      state.animProgress = state.animElapsed / clip->duration;
    }

    // Update combo/cancel windows
    if (clip) {
      bool wasInCombo = state.inComboWindow;
      state.inComboWindow = (state.animProgress >= clip->comboWindowStart &&
                             state.animProgress <= clip->comboWindowEnd);
      state.inCancelWindow = (state.animProgress >= clip->cancelWindowStart &&
                              state.animProgress <= clip->cancelWindowEnd);

      // Hit frame detection
      if (!state.hitTriggered && state.animElapsed >= clip->hitFrameTime) {
        state.hitTriggered = true;
        auto *actor = RE::TESForm::LookupByID<RE::Actor>(formID);
        if (actor) {
          for (auto &callback : m_onHitCallbacks) {
            callback(actor, state);
          }
        }
      }

      // Commit ends when cancel window opens
      if (state.inCancelWindow) {
        state.commitActive = false;
      }

      // Check for buffered input when combo window opens
      if (state.inComboWindow && !wasInCombo) {
        auto *actor = RE::TESForm::LookupByID<RE::Actor>(formID);
        if (actor) {
          auto &inputBuffer = InputBuffer::GetSingleton();
          auto bufferedInput = inputBuffer.ConsumeBufferedAttack(actor);
          if (bufferedInput.has_value()) {
            AdvanceCombo(actor, bufferedInput->isHeavy);
          }
        }
      }

      // Animation finished
      if (state.animProgress >= 1.0f) {
        auto *actor = RE::TESForm::LookupByID<RE::Actor>(formID);
        if (actor) {
          EndCombo(actor);
        }
        it = m_comboStates.erase(it);
        continue;
      }
    }

    ++it;
  }
}

bool ComboSystem::TryStartAttack(RE::Actor *actor, bool isHeavy,
                                 AttackDirection dir) {
  if (!actor || !m_initialized)
    return false;

  auto &animMgr = AnimationManager::GetSingleton();
  auto weaponCat = animMgr.DetectWeaponCategory(actor);
  auto *animSet = animMgr.GetAnimationSetForWeapon(weaponCat);

  if (!animSet) {
    // No animation set for this weapon type, fall through to vanilla
    return false;
  }

  const auto &chain = isHeavy ? animSet->heavyAttacks : animSet->lightAttacks;
  if (chain.empty()) {
    return false;
  }

  // Check special attack types first
  if (dir == AttackDirection::Sprinting && animSet->sprintAttack.has_value()) {
    return StartCombo(actor, &animSet->sprintAttack.value(), isHeavy);
  }
  if (dir == AttackDirection::Jumping && animSet->jumpAttack.has_value()) {
    return StartCombo(actor, &animSet->jumpAttack.value(), isHeavy);
  }

  // Start combo chain from the first attack
  return StartCombo(actor, &chain[0], isHeavy);
}

bool ComboSystem::TryChainAttack(RE::Actor *actor, bool isHeavy) {
  if (!actor || !m_initialized)
    return false;

  std::shared_lock lock(m_mutex);
  auto it = m_comboStates.find(actor->GetFormID());
  if (it == m_comboStates.end() || !it->second.IsActive()) {
    return false;
  }

  auto &state = it->second;

  // If in combo window, advance immediately
  if (state.inComboWindow) {
    lock.unlock();
    return AdvanceCombo(actor, isHeavy);
  }

  // If committed (before combo window), buffer the input
  if (state.commitActive) {
    InputBuffer::GetSingleton().BufferAttack(actor, isHeavy);
    return true; // Input buffered, will be consumed when window opens
  }

  return false;
}

void ComboSystem::CancelCombo(RE::Actor *actor) {
  if (!actor)
    return;

  std::unique_lock lock(m_mutex);
  auto it = m_comboStates.find(actor->GetFormID());
  if (it != m_comboStates.end()) {
    auto &state = it->second;
    if (state.inCancelWindow || !state.commitActive) {
      EndCombo(actor);
      m_comboStates.erase(it);
    }
  }
}

const ComboState *ComboSystem::GetComboState(RE::Actor *actor) const {
  if (!actor)
    return nullptr;
  std::shared_lock lock(m_mutex);
  auto it = m_comboStates.find(actor->GetFormID());
  return (it != m_comboStates.end()) ? &it->second : nullptr;
}

bool ComboSystem::IsInCombo(RE::Actor *actor) const {
  auto *state = GetComboState(actor);
  return state && state->IsActive();
}

bool ComboSystem::IsInComboWindow(RE::Actor *actor) const {
  auto *state = GetComboState(actor);
  return state && state->inComboWindow;
}

bool ComboSystem::IsCommitted(RE::Actor *actor) const {
  auto *state = GetComboState(actor);
  return state && state->commitActive;
}

void ComboSystem::RegisterOnHitCallback(ComboEventCallback callback) {
  m_onHitCallbacks.push_back(std::move(callback));
}

void ComboSystem::RegisterOnComboEndCallback(ComboEventCallback callback) {
  m_onComboEndCallbacks.push_back(std::move(callback));
}

void ComboSystem::RegisterOnComboChainCallback(ComboEventCallback callback) {
  m_onComboChainCallbacks.push_back(std::move(callback));
}

void ComboSystem::OnAnimationEvent(RE::Actor *actor,
                                   const RE::BSFixedString &eventName) {
  if (!actor)
    return;

  std::unique_lock lock(m_mutex);
  auto it = m_comboStates.find(actor->GetFormID());
  if (it == m_comboStates.end())
    return;

  auto &state = it->second;
  std::string_view event = eventName.c_str();

  if (event == Events::COMBO_WINDOW_OPEN) {
    state.inComboWindow = true;
  } else if (event == Events::COMBO_WINDOW_CLOSE) {
    state.inComboWindow = false;
  } else if (event == Events::CANCEL_WINDOW_OPEN) {
    state.inCancelWindow = true;
    state.commitActive = false;
  } else if (event == Events::CANCEL_WINDOW_CLOSE) {
    state.inCancelWindow = false;
  } else if (event == Events::HIT_FRAME) {
    if (!state.hitTriggered) {
      state.hitTriggered = true;
      for (auto &callback : m_onHitCallbacks) {
        callback(actor, state);
      }
    }
  } else if (event == Events::ANIMATION_END) {
    EndCombo(actor);
  }
}

void ComboSystem::OnAnimationProgress(RE::Actor *actor, float normalizedTime) {
  if (!actor)
    return;

  std::unique_lock lock(m_mutex);
  auto it = m_comboStates.find(actor->GetFormID());
  if (it != m_comboStates.end()) {
    it->second.animProgress = normalizedTime;
  }
}

bool ComboSystem::StartCombo(RE::Actor *actor, const AnimationClip *clip,
                             bool isHeavy) {
  if (!clip)
    return false;

  std::unique_lock lock(m_mutex);

  ComboState state;
  state.currentClipName = clip->name;
  state.comboIndex = 1;
  state.animProgress = 0.0f;
  state.animElapsed = 0.0f;
  state.isHeavyChain = isHeavy;
  state.commitActive = true;
  state.hitTriggered = false;
  state.weaponCategory = clip->weaponType;

  m_comboStates[actor->GetFormID()] = state;

  lock.unlock();

  // Play the animation
  return PlayAnimation(actor, clip);
}

bool ComboSystem::AdvanceCombo(RE::Actor *actor, bool isHeavy) {
  std::unique_lock lock(m_mutex);

  auto it = m_comboStates.find(actor->GetFormID());
  if (it == m_comboStates.end())
    return false;

  auto &state = it->second;

  // Check max combo length
  if (state.comboIndex >= Combo::MAX_COMBO_LENGTH) {
    return false;
  }

  auto &animMgr = AnimationManager::GetSingleton();
  const AnimationClip *nextClip =
      animMgr.GetNextComboClip(state.currentClipName, isHeavy);

  if (!nextClip)
    return false;

  // Update state for next combo step
  state.currentClipName = nextClip->name;
  state.comboIndex++;
  state.animProgress = 0.0f;
  state.animElapsed = 0.0f;
  state.isHeavyChain = isHeavy;
  state.commitActive = true;
  state.hitTriggered = false;
  state.inComboWindow = false;
  state.inCancelWindow = false;

  lock.unlock();

  // Notify callbacks
  for (auto &callback : m_onComboChainCallbacks) {
    callback(actor, state);
  }

  return PlayAnimation(actor, nextClip);
}

void ComboSystem::EndCombo(RE::Actor *actor) {
  if (!actor)
    return;

  auto it = m_comboStates.find(actor->GetFormID());
  if (it != m_comboStates.end()) {
    auto state = it->second; // Copy for callbacks
    it->second.Reset();

    for (auto &callback : m_onComboEndCallbacks) {
      callback(actor, state);
    }
  }

  // Clear input buffer for this actor
  InputBuffer::GetSingleton().ClearBuffer(actor);
}

bool ComboSystem::PlayAnimation(RE::Actor *actor, const AnimationClip *clip) {
  if (!actor || !clip)
    return false;

  // Queue the animation via Skyrim's animation graph
  // This interfaces with the BehaviorHooks system to replace the
  // vanilla animation with our custom HKX
  RE::BSFixedString animEvent(clip->name.c_str());
  bool result = actor->NotifyAnimationGraph(animEvent);

  if (result) {
    logger::trace("CCW: Playing animation '{}' on actor 0x{:X}", clip->name,
                  actor->GetFormID());
  } else {
    // Fallback: try setting idle animation directly
    logger::warn(
        "CCW: NotifyAnimationGraph failed for '{}', attempting direct play",
        clip->name);
  }

  return result;
}

} // namespace CCW
