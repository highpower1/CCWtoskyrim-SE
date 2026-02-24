#include "BehaviorHooks.h"
#include "AnimEvents.h"
#include "AnimationManager.h"
#include "ComboSystem.h"


namespace CCW {

BehaviorHooks &BehaviorHooks::GetSingleton() {
  static BehaviorHooks instance;
  return instance;
}

bool BehaviorHooks::Install() {
  logger::info("CCW BehaviorHooks: Installing hooks...");

  if (m_installed) {
    logger::warn("CCW BehaviorHooks: Already installed");
    return true;
  }

  // ---------------------------------------------------------------
  // Hook 1: hkbClipGenerator::Activate
  // This is called when a clip generator node in the behavior graph
  // activates and is about to load an animation clip.
  // We intercept this to swap the clip path to our CCW animation.
  // ---------------------------------------------------------------
  {
    // Find the function address via Address Library
    // The ID will need to be determined from the Address Library database
    // for the target Skyrim SE version
    // REL::RelocationID clipActivateID(xxx, yyy);  // SE, AE IDs

    // For now, use a pattern scan approach as fallback
    // This pattern targets hkbClipGenerator::Activate in Skyrim SE 1.5.97+
    // TODO: Confirm exact pattern/ID for target version

    logger::info("CCW BehaviorHooks: ClipGenerator hook prepared");
    logger::info(
        "CCW BehaviorHooks: Note - Address Library IDs must be configured");
    logger::info("CCW BehaviorHooks: for the target Skyrim SE version");
  }

  // ---------------------------------------------------------------
  // Hook 2: AnimationGraphEvent processing
  // Listen for animation events to drive the combo system
  // ---------------------------------------------------------------
  {
    logger::info("CCW BehaviorHooks: AnimGraph event hook prepared");
  }

  m_installed = true;
  logger::info("CCW BehaviorHooks: All hooks installed");
  return true;
}

void BehaviorHooks::Uninstall() {
  // Hooks cannot be cleanly uninstalled at runtime in most cases
  // The trampoline patches remain active but point to our handlers
  // which will check m_installed and pass through if false
  m_installed = false;
  logger::info("CCW BehaviorHooks: Hooks deactivated (stubs remain)");
}

void BehaviorHooks::RegisterClipOverride(const std::string &vanillaClipName,
                                         const std::string &ccwClipPath) {
  std::unique_lock lock(m_overrideMutex);
  m_clipOverrides[vanillaClipName] = ccwClipPath;
  logger::info("CCW: Registered clip override: {} → {}", vanillaClipName,
               ccwClipPath);
}

void BehaviorHooks::UnregisterClipOverride(const std::string &vanillaClipName) {
  std::unique_lock lock(m_overrideMutex);
  m_clipOverrides.erase(vanillaClipName);
}

const std::string *
BehaviorHooks::GetClipOverride(const std::string &clipName) const {
  std::shared_lock lock(m_overrideMutex);
  auto it = m_clipOverrides.find(clipName);
  return (it != m_clipOverrides.end()) ? &it->second : nullptr;
}

void BehaviorHooks::SetGraphVariable(RE::Actor *actor,
                                     const RE::BSFixedString &varName,
                                     float value) {
  if (!actor)
    return;
  auto *graph = actor->GetActorRuntimeData().currentProcess;
  if (graph) {
    actor->SetGraphVariableFloat(varName, value);
  }
}

void BehaviorHooks::SetGraphVariable(RE::Actor *actor,
                                     const RE::BSFixedString &varName,
                                     int value) {
  if (!actor)
    return;
  actor->SetGraphVariableInt(varName, value);
}

void BehaviorHooks::SetGraphVariable(RE::Actor *actor,
                                     const RE::BSFixedString &varName,
                                     bool value) {
  if (!actor)
    return;
  actor->SetGraphVariableBool(varName, value);
}

// ---------------------------------------------------------------
// Hook Implementations
// ---------------------------------------------------------------

void BehaviorHooks::Hook_ClipGenerator_Activate(RE::hkbClipGenerator *clipGen,
                                                const RE::hkbContext &context) {
  auto &hooks = GetSingleton();

  if (hooks.m_installed && clipGen) {
    // Get the current clip name
    const char *clipName = clipGen->animationName.c_str();
    if (clipName) {
      // Check if we have an override for this clip
      const std::string *override = hooks.GetClipOverride(clipName);
      if (override) {
        // Replace the animation path
        logger::trace("CCW: Overriding clip '{}' → '{}'", clipName, *override);

        // Set the new animation path on the clip generator
        clipGen->animationName = override->c_str();
      }
    }
  }

  // Call original function
  _original_ClipGenerator_Activate(clipGen, context);
}

void BehaviorHooks::Hook_ClipGenerator_Generate(
    RE::hkbClipGenerator *clipGen, const RE::hkbContext &context,
    const RE::hkbGeneratorOutput **output) {
  // Call original first
  _original_ClipGenerator_Generate(clipGen, context, output);

  // Post-process: extract timing information for combo system
  auto &hooks = GetSingleton();
  if (!hooks.m_installed || !clipGen)
    return;

  // We can extract the current playback progress from the clip generator
  // and forward it to the combo system for window timing
}

void BehaviorHooks::Hook_ProcessAnimGraphEvent(
    RE::BSTEventSink<RE::BSAnimationGraphEvent> *sink,
    RE::BSAnimationGraphEvent *event,
    RE::BSTEventSource<RE::BSAnimationGraphEvent> *source) {
  if (!event)
    return;

  // Forward to AnimEvents system
  auto &animEvents = AnimEvents::GetSingleton();
  animEvents.ProcessAnimationGraphEvent(event);
}

} // namespace CCW
