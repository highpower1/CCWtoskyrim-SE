#include "AnimEvents.h"
#include "AnimationManager.h"
#include "ComboSystem.h"


namespace CCW {

AnimEvents &AnimEvents::GetSingleton() {
  static AnimEvents instance;
  return instance;
}

bool AnimEvents::Initialize() {
  logger::info("CCW AnimEvents: Initializing...");
  m_initialized = true;

  // Register for the player's animation graph events
  RegisterForPlayer();

  return true;
}

void AnimEvents::Shutdown() {
  UnregisterForPlayer();
  std::unique_lock lock(m_mutex);
  m_handlers.clear();
  m_initialized = false;
  logger::info("CCW AnimEvents: Shutdown");
}

void AnimEvents::RegisterForPlayer() {
  if (m_registeredForPlayer)
    return;

  auto *player = RE::PlayerCharacter::GetSingleton();
  if (player) {
    player->AddAnimationGraphEventSink(this);
    m_registeredForPlayer = true;
    logger::info("CCW AnimEvents: Registered for player animation events");
  } else {
    logger::warn("CCW AnimEvents: Player not available yet, will retry");
  }
}

void AnimEvents::UnregisterForPlayer() {
  if (!m_registeredForPlayer)
    return;

  auto *player = RE::PlayerCharacter::GetSingleton();
  if (player) {
    player->RemoveAnimationGraphEventSink(this);
    m_registeredForPlayer = false;
  }
}

void AnimEvents::RegisterHandler(const std::string &eventName,
                                 AnimEventHandler handler) {
  std::unique_lock lock(m_mutex);
  m_handlers[eventName].push_back(std::move(handler));
}

void AnimEvents::UnregisterHandler(const std::string &eventName) {
  std::unique_lock lock(m_mutex);
  m_handlers.erase(eventName);
}

RE::BSEventNotifyControl AnimEvents::ProcessEvent(
    const RE::BSAnimationGraphEvent *event,
    RE::BSTEventSource<RE::BSAnimationGraphEvent> *source) {

  if (!event || !m_initialized) {
    return RE::BSEventNotifyControl::kContinue;
  }

  // Get the actor from the event holder
  auto *holder = event->holder;
  if (!holder)
    return RE::BSEventNotifyControl::kContinue;

  auto *actor = holder->As<RE::Actor>();
  if (!actor)
    return RE::BSEventNotifyControl::kContinue;

  std::string eventName = event->tag.c_str();

  // Forward to combo system
  auto &comboSystem = ComboSystem::GetSingleton();
  comboSystem.OnAnimationEvent(actor, event->tag);

  // Translate vanilla events
  TranslateEvent(actor, eventName);

  return RE::BSEventNotifyControl::kContinue;
}

void AnimEvents::ProcessAnimationGraphEvent(RE::BSAnimationGraphEvent *event) {
  if (!event || !m_initialized)
    return;

  auto *holder = event->holder;
  if (!holder)
    return;

  auto *actor = holder->As<RE::Actor>();
  if (!actor)
    return;

  std::string eventName = event->tag.c_str();

  // Forward to combo system
  ComboSystem::GetSingleton().OnAnimationEvent(actor, event->tag);

  // Translate and fire
  TranslateEvent(actor, eventName);
}

void AnimEvents::TranslateEvent(RE::Actor *actor,
                                const std::string &eventName) {
  // Map vanilla Skyrim animation events to CCW events
  // This allows our combo system to react to standard game events

  auto &comboSys = ComboSystem::GetSingleton();
  const auto *comboState = comboSys.GetComboState(actor);

  if (!comboState || !comboState->IsActive())
    return;

  CCWAnimEvent ccwEvent;
  ccwEvent.actor = actor;
  ccwEvent.eventName = eventName;
  ccwEvent.clipName = comboState->currentClipName;
  ccwEvent.comboStep = comboState->comboIndex;
  ccwEvent.weaponType = comboState->weaponCategory;

  // Vanilla event translations
  if (eventName == "weaponSwing" || eventName == "weaponLeftSwing") {
    // Standard weapon swing - check if this aligns with our hit frame
    ccwEvent.eventName = Events::WEAPON_SWING;
    FireCCWEvent(ccwEvent);
  } else if (eventName == "HitFrame" || eventName == "bashRelease") {
    ccwEvent.eventName = Events::HIT_FRAME;
    FireCCWEvent(ccwEvent);
  } else if (eventName == "attackStop") {
    // Vanilla attack end - we use this as a fallback combo window trigger
    ccwEvent.eventName = Events::ANIMATION_END;
    FireCCWEvent(ccwEvent);
  }

  // Check if it's a CCW custom event (CCW_ prefix)
  if (eventName.starts_with("CCW_")) {
    FireCCWEvent(ccwEvent);
  }
}

void AnimEvents::FireCCWEvent(const CCWAnimEvent &event) {
  std::shared_lock lock(m_mutex);

  // Fire handlers for this specific event
  auto it = m_handlers.find(event.eventName);
  if (it != m_handlers.end()) {
    for (auto &handler : it->second) {
      handler(event);
    }
  }

  // Also fire wildcard handlers (registered with "*")
  auto wildcardIt = m_handlers.find("*");
  if (wildcardIt != m_handlers.end()) {
    for (auto &handler : wildcardIt->second) {
      handler(event);
    }
  }
}

} // namespace CCW
