#pragma once

// CCW Animation Framework - Configuration Header
// Compile-time and runtime configuration constants

namespace CCW {

// Plugin info
constexpr auto PLUGIN_NAME = "CCWAnimFramework";
constexpr auto PLUGIN_VERSION_MAJOR = 1;
constexpr auto PLUGIN_VERSION_MINOR = 0;
constexpr auto PLUGIN_VERSION_PATCH = 0;

// Animation paths (relative to Skyrim SE Data/)
constexpr auto ANIM_ROOT = "meshes\\actors\\character\\animations\\ccw\\";
constexpr auto CONFIG_FILE = "SKSE\\Plugins\\CCWAnimFramework.json";

// Combo system tuning
namespace Combo {
constexpr float DEFAULT_COMBO_WINDOW = 0.5f; // Seconds to chain next attack
constexpr float INPUT_BUFFER_DURATION =
    0.3f;                                   // Input buffer lookahead (seconds)
constexpr int MAX_COMBO_LENGTH = 8;         // Maximum combo chain length
constexpr float CANCEL_WINDOW_START = 0.4f; // Normalized time when cancel opens
constexpr float CANCEL_WINDOW_END = 0.85f; // Normalized time when cancel closes
constexpr float HIT_STOP_DURATION = 0.05f; // Hit stop pause (seconds)
constexpr float RECOVERY_TIME = 0.3f;      // Recovery after combo ends
} // namespace Combo

// Animation event names (Skyrim Havok events)
namespace Events {
constexpr auto HIT_FRAME = "CCW_HitFrame";
constexpr auto COMBO_WINDOW_OPEN = "CCW_ComboOpen";
constexpr auto COMBO_WINDOW_CLOSE = "CCW_ComboClose";
constexpr auto CANCEL_WINDOW_OPEN = "CCW_CancelOpen";
constexpr auto CANCEL_WINDOW_CLOSE = "CCW_CancelClose";
constexpr auto ANIMATION_END = "CCW_AnimEnd";
constexpr auto WEAPON_SWING = "weaponSwing"; // Vanilla event
constexpr auto WEAPON_LEFT_SWING = "weaponLeftSwing";
constexpr auto SOUND_PLAY = "SoundPlay.WPNSwingUnarmed";
} // namespace Events

// Weapon type categories (mapped from Skyrim weapon types)
enum class WeaponCategory : uint8_t {
  Unarmed = 0,
  OneHandSword,
  OneHandAxe,
  OneHandMace,
  OneHandDagger,
  TwoHandSword, // Greatsword
  TwoHandAxe,   // Battleaxe/Warhammer
  DualWield,
  SwordAndShield,
  Staff,
  Count
};

// Attack direction (for directional combo system)
enum class AttackDirection : uint8_t {
  Neutral = 0,
  Forward,
  Left,
  Right,
  Back,
  Standing, // No movement
  Sprinting,
  Jumping,
  Count
};

} // namespace CCW
