#include "AnimationManager.h"

namespace CCW {

AnimationManager &AnimationManager::GetSingleton() {
  static AnimationManager instance;
  return instance;
}

bool AnimationManager::Initialize() {
  logger::info("CCW AnimationManager: Initializing...");

  if (m_initialized) {
    logger::warn("CCW AnimationManager: Already initialized");
    return true;
  }

  // Load animation configuration files
  if (!LoadAnimationConfigs()) {
    logger::error("CCW AnimationManager: Failed to load animation configs");
    return false;
  }

  m_initialized = true;
  logger::info("CCW AnimationManager: Initialized with {} animation sets",
               m_animSets.size());
  return true;
}

void AnimationManager::Shutdown() {
  std::unique_lock lock(m_mutex);
  m_animSets.clear();
  m_clipLookup.clear();
  m_weaponSetMap.clear();
  m_loadedHKX.clear();
  m_initialized = false;
  logger::info("CCW AnimationManager: Shutdown");
}

bool AnimationManager::RegisterAnimationSet(const AnimationSet &set) {
  std::unique_lock lock(m_mutex);

  if (m_animSets.contains(set.name)) {
    logger::warn("CCW AnimationManager: Overwriting animation set '{}'",
                 set.name);
  }

  m_animSets[set.name] = set;
  m_weaponSetMap[set.weaponCategory] = set.name;

  // Build clip lookup table
  auto &storedSet = m_animSets[set.name];
  auto registerClips = [this](std::vector<AnimationClip> &clips) {
    for (auto &clip : clips) {
      m_clipLookup[clip.name] = &clip;
    }
  };

  registerClips(storedSet.lightAttacks);
  registerClips(storedSet.heavyAttacks);
  registerClips(storedSet.specialAttacks);

  // Register optional clips
  auto registerOptional = [this](std::optional<AnimationClip> &clip) {
    if (clip.has_value()) {
      m_clipLookup[clip->name] = &clip.value();
    }
  };
  registerOptional(storedSet.sprintAttack);
  registerOptional(storedSet.jumpAttack);
  registerOptional(storedSet.guardCounter);
  registerOptional(storedSet.backstep);
  registerOptional(storedSet.dodgeRoll);

  logger::info("CCW AnimationManager: Registered set '{}' for weapon type {}",
               set.name, static_cast<int>(set.weaponCategory));
  return true;
}

void AnimationManager::UnregisterAnimationSet(const std::string &name) {
  std::unique_lock lock(m_mutex);

  auto it = m_animSets.find(name);
  if (it != m_animSets.end()) {
    // Remove clips from lookup
    auto removeClips = [this](const std::vector<AnimationClip> &clips) {
      for (const auto &clip : clips) {
        m_clipLookup.erase(clip.name);
      }
    };
    removeClips(it->second.lightAttacks);
    removeClips(it->second.heavyAttacks);
    removeClips(it->second.specialAttacks);

    m_weaponSetMap.erase(it->second.weaponCategory);
    m_animSets.erase(it);
  }
}

const AnimationSet *
AnimationManager::GetAnimationSet(const std::string &name) const {
  std::shared_lock lock(m_mutex);
  auto it = m_animSets.find(name);
  return (it != m_animSets.end()) ? &it->second : nullptr;
}

const AnimationSet *
AnimationManager::GetAnimationSetForWeapon(WeaponCategory category) const {
  std::shared_lock lock(m_mutex);
  auto it = m_weaponSetMap.find(category);
  if (it != m_weaponSetMap.end()) {
    auto setIt = m_animSets.find(it->second);
    if (setIt != m_animSets.end()) {
      return &setIt->second;
    }
  }
  return nullptr;
}

const AnimationClip *
AnimationManager::GetClip(const std::string &clipName) const {
  std::shared_lock lock(m_mutex);
  auto it = m_clipLookup.find(clipName);
  return (it != m_clipLookup.end()) ? it->second : nullptr;
}

const AnimationClip *
AnimationManager::GetNextComboClip(const std::string &currentClip,
                                   bool isHeavy) const {
  std::shared_lock lock(m_mutex);

  // Find which set and chain the current clip belongs to
  for (const auto &[setName, set] : m_animSets) {
    const auto &chain = isHeavy ? set.heavyAttacks : set.lightAttacks;

    for (size_t i = 0; i < chain.size(); ++i) {
      if (chain[i].name == currentClip) {
        // Return next clip in chain (loop back to start if at end)
        size_t nextIdx = (i + 1) % chain.size();
        return &chain[nextIdx];
      }
    }
  }

  return nullptr;
}

bool AnimationManager::LoadAnimationHKX(const std::string &hkxPath) {
  // In a full implementation, this would interface with Skyrim's
  // BSResource system to load the HKX file into memory
  std::unique_lock lock(m_mutex);

  if (m_loadedHKX.contains(hkxPath)) {
    return true;
  }

  // Verify the file exists
  auto dataHandler = RE::TESDataHandler::GetSingleton();
  if (!dataHandler) {
    logger::error("CCW: Cannot access TESDataHandler");
    return false;
  }

  // Check if file exists in the Data directory
  std::filesystem::path fullPath =
      std::filesystem::path(RE::BSResourceNiBinaryStream::GetPrefix()) /
      hkxPath;

  if (!std::filesystem::exists(fullPath)) {
    logger::warn("CCW: HKX file not found: {}", hkxPath);
    return false;
  }

  m_loadedHKX.insert(hkxPath);
  logger::info("CCW: Loaded HKX: {}", hkxPath);
  return true;
}

void AnimationManager::PreloadAllAnimations() {
  std::shared_lock lock(m_mutex);

  for (const auto &[name, set] : m_animSets) {
    auto loadClips = [this](const std::vector<AnimationClip> &clips) {
      for (const auto &clip : clips) {
        LoadAnimationHKX(clip.hkxPath);
      }
    };
    loadClips(set.lightAttacks);
    loadClips(set.heavyAttacks);
    loadClips(set.specialAttacks);
  }
}

bool AnimationManager::IsAnimationLoaded(const std::string &hkxPath) const {
  std::shared_lock lock(m_mutex);
  return m_loadedHKX.contains(hkxPath);
}

WeaponCategory AnimationManager::DetectWeaponCategory(RE::Actor *actor) const {
  if (!actor)
    return WeaponCategory::Unarmed;

  auto *equippedRight = actor->GetEquippedObject(false); // Right hand
  auto *equippedLeft = actor->GetEquippedObject(true);   // Left hand

  if (!equippedRight)
    return WeaponCategory::Unarmed;

  auto *weapon = equippedRight->As<RE::TESObjectWEAP>();
  if (!weapon)
    return WeaponCategory::Unarmed;

  // Check for dual wield
  bool hasDualWield = false;
  if (equippedLeft) {
    auto *leftWeapon = equippedLeft->As<RE::TESObjectWEAP>();
    if (leftWeapon)
      hasDualWield = true;
  }

  // Check for shield
  bool hasShield = false;
  if (equippedLeft) {
    auto *shield = equippedLeft->As<RE::TESObjectARMO>();
    if (shield && shield->IsShield())
      hasShield = true;
  }

  using WT = RE::WEAPON_TYPE;
  switch (weapon->GetWeaponType()) {
  case WT::kOneHandSword:
    if (hasShield)
      return WeaponCategory::SwordAndShield;
    if (hasDualWield)
      return WeaponCategory::DualWield;
    return WeaponCategory::OneHandSword;

  case WT::kOneHandAxe:
    return WeaponCategory::OneHandAxe;

  case WT::kOneHandMace:
    return WeaponCategory::OneHandMace;

  case WT::kOneHandDagger:
    return WeaponCategory::OneHandDagger;

  case WT::kTwoHandSword:
    return WeaponCategory::TwoHandSword;

  case WT::kTwoHandAxe:
    return WeaponCategory::TwoHandAxe;

  case WT::kStaff:
    return WeaponCategory::Staff;

  default:
    return WeaponCategory::Unarmed;
  }
}

std::vector<std::string> AnimationManager::GetAvailableSetNames() const {
  std::shared_lock lock(m_mutex);
  std::vector<std::string> names;
  names.reserve(m_animSets.size());
  for (const auto &[name, _] : m_animSets) {
    names.push_back(name);
  }
  return names;
}

bool AnimationManager::LoadAnimationConfigs() {
  // Look for animation set JSON configs in the SKSE plugin config directory
  namespace fs = std::filesystem;

  fs::path configDir = fs::path(ANIM_ROOT);

  // Also check relative to the DLL location
  auto pluginPath = SKSE::GetPluginConfigPath();
  fs::path animConfigDir = pluginPath.parent_path() / "CCWAnimSets";

  if (fs::exists(animConfigDir)) {
    for (const auto &entry : fs::directory_iterator(animConfigDir)) {
      if (entry.path().extension() == ".json") {
        LoadAnimationSetFromFile(entry.path());
      }
    }
  }

  // Create a default animation set for testing if none were loaded
  if (m_animSets.empty()) {
    logger::info("CCW AnimationManager: No config files found. "
                 "Creating default placeholder animation set.");

    AnimationSet defaultSet;
    defaultSet.name = "ccw_default_greatsword";
    defaultSet.weaponCategory = WeaponCategory::TwoHandSword;

    // Placeholder clips - paths should match extracted/converted CCW animations
    for (int i = 1; i <= 4; ++i) {
      AnimationClip clip;
      clip.name = fmt::format("ccw_gs_light_{}", i);
      clip.hkxPath =
          fmt::format("{}greatsword\\attack_light_{}.hkx", ANIM_ROOT, i);
      clip.duration = 0.8f + (i * 0.1f);
      clip.hitFrameTime = 0.3f;
      clip.comboWindowStart = 0.4f;
      clip.comboWindowEnd = 0.75f;
      clip.weaponType = WeaponCategory::TwoHandSword;
      defaultSet.lightAttacks.push_back(clip);
    }

    for (int i = 1; i <= 2; ++i) {
      AnimationClip clip;
      clip.name = fmt::format("ccw_gs_heavy_{}", i);
      clip.hkxPath =
          fmt::format("{}greatsword\\attack_heavy_{}.hkx", ANIM_ROOT, i);
      clip.duration = 1.2f + (i * 0.15f);
      clip.hitFrameTime = 0.5f;
      clip.comboWindowStart = 0.6f;
      clip.comboWindowEnd = 0.9f;
      clip.weaponType = WeaponCategory::TwoHandSword;
      defaultSet.heavyAttacks.push_back(clip);
    }

    RegisterAnimationSet(defaultSet);
  }

  return true;
}

bool AnimationManager::LoadAnimationSetFromFile(
    const std::filesystem::path &configPath) {
  logger::info("CCW AnimationManager: Loading config: {}", configPath.string());

  // JSON config parsing
  // Expected format:
  // {
  //   "name": "ccw_greatsword",
  //   "weaponCategory": "TwoHandSword",
  //   "lightAttacks": [
  //     { "name": "gs_l1", "hkx": "path.hkx", "duration": 0.9, ... }
  //   ],
  //   ...
  // }

  try {
    std::ifstream file(configPath);
    if (!file.is_open()) {
      logger::error("CCW: Cannot open config: {}", configPath.string());
      return false;
    }

    // Manual JSON parsing (lightweight, no dependency required)
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // For now, log that we found the config
    // Full JSON parsing would use nlohmann_json if available
    logger::info("CCW: Found animation config ({} bytes): {}", content.size(),
                 configPath.filename().string());

    return true;

  } catch (const std::exception &e) {
    logger::error("CCW: Config parse error: {}", e.what());
    return false;
  }
}

} // namespace CCW
