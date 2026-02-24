#pragma once

#include "CCWConfig.h"
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>


namespace CCW {

// Represents a single animation clip that can be played
struct AnimationClip {
  std::string name;              // Unique identifier (e.g., "1hs_light_1")
  std::string hkxPath;           // Path to .hkx file relative to Data/
  float duration = 0.0f;         // Total duration in seconds
  float hitFrameTime = 0.0f;     // When hit detection activates
  float comboWindowStart = 0.0f; // When combo input opens
  float comboWindowEnd = 0.0f;   // When combo input closes
  float cancelWindowStart = 0.0f;
  float cancelWindowEnd = 0.0f;
  bool hasRootMotion = false; // Whether this clip drives character movement
  WeaponCategory weaponType = WeaponCategory::Unarmed;
  AttackDirection direction = AttackDirection::Neutral;
};

// A set of animations for a specific weapon type
struct AnimationSet {
  std::string name; // Set name (e.g., "ccw_greatsword")
  WeaponCategory weaponCategory;
  std::vector<AnimationClip> lightAttacks;
  std::vector<AnimationClip> heavyAttacks;
  std::vector<AnimationClip> specialAttacks;
  std::optional<AnimationClip> sprintAttack;
  std::optional<AnimationClip> jumpAttack;
  std::optional<AnimationClip> guardCounter;
  std::optional<AnimationClip> backstep;
  std::optional<AnimationClip> dodgeRoll;
};

// Animation Manager - Core animation registration and lookup system
class AnimationManager {
public:
  // Singleton access
  static AnimationManager &GetSingleton();

  // Initialization
  bool Initialize();
  void Shutdown();

  // Animation Set Management
  bool RegisterAnimationSet(const AnimationSet &set);
  void UnregisterAnimationSet(const std::string &name);
  const AnimationSet *GetAnimationSet(const std::string &name) const;
  const AnimationSet *GetAnimationSetForWeapon(WeaponCategory category) const;

  // Animation Clip Lookup
  const AnimationClip *GetClip(const std::string &clipName) const;
  const AnimationClip *GetNextComboClip(const std::string &currentClip,
                                        bool isHeavy = false) const;

  // HKX File Management
  bool LoadAnimationHKX(const std::string &hkxPath);
  void PreloadAllAnimations();
  bool IsAnimationLoaded(const std::string &hkxPath) const;

  // Runtime State
  WeaponCategory DetectWeaponCategory(RE::Actor *actor) const;
  std::vector<std::string> GetAvailableSetNames() const;

private:
  AnimationManager() = default;
  ~AnimationManager() = default;
  AnimationManager(const AnimationManager &) = delete;
  AnimationManager &operator=(const AnimationManager &) = delete;

  // Load animation sets from configuration files
  bool LoadAnimationConfigs();
  bool LoadAnimationSetFromFile(const std::filesystem::path &configPath);

  // Data
  mutable std::shared_mutex m_mutex;
  std::unordered_map<std::string, AnimationSet> m_animSets; // name → set
  std::unordered_map<std::string, AnimationClip *>
      m_clipLookup; // clipName → clip
  std::unordered_map<WeaponCategory, std::string>
      m_weaponSetMap;                // weapon → set name
  std::set<std::string> m_loadedHKX; // loaded HKX paths

  bool m_initialized = false;
};

} // namespace CCW
