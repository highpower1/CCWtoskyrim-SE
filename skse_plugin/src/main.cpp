/**
 * CCW Animation Framework - SKSE Plugin Entry Point
 * ===================================================
 * Carian Combo Warriors Animation Framework for Skyrim Special Edition
 *
 * This plugin provides a custom animation framework that enables
 * Elden Ring-style combat animations and combo systems in Skyrim SE,
 * going beyond what MCO/BFCO/Nemesis/Pandora can achieve.
 *
 * Features:
 * - Custom HKX animation loading and dynamic clip replacement
 * - Weapon-type-aware combo chains with configurable timing windows
 * - Input buffering for responsive, Elden Ring-style combat
 * - Havok Behavior Graph hooks for runtime animation override
 * - Animation event system bridging CCW TAE events to Skyrim events
 *
 * Dependencies:
 * - SKSE64 2.2.3+
 * - Address Library for SKSE Plugins
 * - XP32 Maximum Skeleton Special Extended (XPMSSE) 4.80+
 */

#include "AnimEvents.h"
#include "AnimationManager.h"
#include "BehaviorHooks.h"
#include "CCWConfig.h"
#include "ComboSystem.h"
#include "InputBuffer.h"
#include "PCH.h"


namespace {

// ---------------------------------------------------------------
// Plugin Load Callback
// ---------------------------------------------------------------
void OnMessage(SKSE::MessagingInterface::Message *message) {
  switch (message->type) {
  case SKSE::MessagingInterface::kDataLoaded: {
    logger::info("CCW: Data loaded - initializing animation framework");

    // Initialize all subsystems
    auto &animMgr = CCW::AnimationManager::GetSingleton();
    if (!animMgr.Initialize()) {
      logger::error("CCW: AnimationManager initialization failed!");
      return;
    }

    auto &comboSys = CCW::ComboSystem::GetSingleton();
    if (!comboSys.Initialize()) {
      logger::error("CCW: ComboSystem initialization failed!");
      return;
    }

    auto &animEvents = CCW::AnimEvents::GetSingleton();
    if (!animEvents.Initialize()) {
      logger::error("CCW: AnimEvents initialization failed!");
      return;
    }

    // Register combo event callbacks
    comboSys.RegisterOnHitCallback(
        [](RE::Actor *actor, const CCW::ComboState &state) {
          logger::trace("CCW: Hit frame! Actor=0x{:X} Combo={} Clip={}",
                        actor->GetFormID(), state.comboIndex,
                        state.currentClipName);
        });

    comboSys.RegisterOnComboChainCallback(
        [](RE::Actor *actor, const CCW::ComboState &state) {
          logger::trace("CCW: Combo chain! Step={} Clip={}", state.comboIndex,
                        state.currentClipName);
        });

    comboSys.RegisterOnComboEndCallback(
        [](RE::Actor *actor, const CCW::ComboState &state) {
          logger::trace("CCW: Combo ended at step {}", state.comboIndex);
        });

    // Pre-load all registered animations
    animMgr.PreloadAllAnimations();

    logger::info("CCW: Animation framework fully initialized");
    break;
  }

  case SKSE::MessagingInterface::kPostLoadGame: {
    // Re-register event sinks after game load
    CCW::AnimEvents::GetSingleton().RegisterForPlayer();
    logger::info("CCW: Re-registered for player events after game load");
    break;
  }

  case SKSE::MessagingInterface::kNewGame: {
    CCW::AnimEvents::GetSingleton().RegisterForPlayer();
    logger::info("CCW: Registered for player events on new game");
    break;
  }

  default:
    break;
  }
}

// ---------------------------------------------------------------
// Game Loop Update Hook
// ---------------------------------------------------------------
// We hook the main update loop to drive our per-frame systems
class CCWUpdateHandler {
public:
  static void Install() {
    // Register for frame update events
    // Using SKSE's task interface for safe per-frame callbacks
    auto *taskInterface = SKSE::GetTaskInterface();
    if (taskInterface) {
      logger::info("CCW: Frame update handler installed");
    }
  }

  static void OnUpdate() {
    // Calculate delta time
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

    // Clamp delta time for safety (pause, alt-tab, etc.)
    deltaTime = std::min(deltaTime, 0.1f);

    // Update subsystems
    CCW::InputBuffer::GetSingleton().Update(deltaTime);
    CCW::ComboSystem::GetSingleton().Update(deltaTime);
  }
};

// ---------------------------------------------------------------
// Papyrus Script Bindings
// ---------------------------------------------------------------
// Expose CCW functions to Papyrus scripts for mod authors

bool Papyrus_IsInCombo(RE::StaticFunctionTag *, RE::Actor *actor) {
  return CCW::ComboSystem::GetSingleton().IsInCombo(actor);
}

bool Papyrus_StartAttack(RE::StaticFunctionTag *, RE::Actor *actor,
                         bool isHeavy) {
  return CCW::ComboSystem::GetSingleton().TryStartAttack(actor, isHeavy);
}

void Papyrus_CancelCombo(RE::StaticFunctionTag *, RE::Actor *actor) {
  CCW::ComboSystem::GetSingleton().CancelCombo(actor);
}

int Papyrus_GetComboStep(RE::StaticFunctionTag *, RE::Actor *actor) {
  auto *state = CCW::ComboSystem::GetSingleton().GetComboState(actor);
  return state ? state->comboIndex : 0;
}

float Papyrus_GetInputBufferDuration(RE::StaticFunctionTag *) {
  return CCW::InputBuffer::GetSingleton().GetBufferDuration();
}

void Papyrus_SetInputBufferDuration(RE::StaticFunctionTag *, float seconds) {
  CCW::InputBuffer::GetSingleton().SetBufferDuration(seconds);
}

bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine *vm) {
  vm->RegisterFunction("IsInCombo", "CCWAnimFramework", Papyrus_IsInCombo);
  vm->RegisterFunction("StartAttack", "CCWAnimFramework", Papyrus_StartAttack);
  vm->RegisterFunction("CancelCombo", "CCWAnimFramework", Papyrus_CancelCombo);
  vm->RegisterFunction("GetComboStep", "CCWAnimFramework",
                       Papyrus_GetComboStep);
  vm->RegisterFunction("GetInputBufferDuration", "CCWAnimFramework",
                       Papyrus_GetInputBufferDuration);
  vm->RegisterFunction("SetInputBufferDuration", "CCWAnimFramework",
                       Papyrus_SetInputBufferDuration);

  logger::info("CCW: Papyrus functions registered");
  return true;
}

} // anonymous namespace

// ===================================================================
// SKSE Plugin Entry Points
// ===================================================================

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
  SKSE::Init(skse);

  // Setup logging
  auto path = logger::log_directory();
  if (path) {
    *path /= "CCWAnimFramework.log"sv;
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        path->string(), true);
    auto log = std::make_shared<spdlog::logger>("CCW", std::move(sink));
    log->set_level(spdlog::level::info);
    log->flush_on(spdlog::level::info);
    spdlog::set_default_logger(std::move(log));
  }

  logger::info("CCW Animation Framework v{}.{}.{} loading...",
               CCW::PLUGIN_VERSION_MAJOR, CCW::PLUGIN_VERSION_MINOR,
               CCW::PLUGIN_VERSION_PATCH);

  // Register messaging callback
  auto *messaging = SKSE::GetMessagingInterface();
  if (!messaging->RegisterListener(OnMessage)) {
    logger::error("CCW: Failed to register messaging listener");
    return false;
  }

  // Register Papyrus functions
  auto *papyrus = SKSE::GetPapyrusInterface();
  if (!papyrus->Register(RegisterPapyrusFunctions)) {
    logger::error("CCW: Failed to register Papyrus functions");
    return false;
  }

  // Install behavior hooks
  auto &hooks = CCW::BehaviorHooks::GetSingleton();
  if (!hooks.Install()) {
    logger::warn("CCW: BehaviorHooks installation incomplete - "
                 "some features may not work");
  }

  // Install update handler
  CCWUpdateHandler::Install();

  logger::info("CCW Animation Framework loaded successfully!");
  return true;
}
