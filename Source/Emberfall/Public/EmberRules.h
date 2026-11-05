#pragma once

// Shared, engine-independent rules. All persistent multiplayer rewards must be
// validated and committed by an authoritative server before reaching a client.
namespace EmberRules
{
enum class GearTier
{
    Wanderer = 0,
    Tempered = 1,
    Dawnbound = 2
};

constexpr int MaxTier = static_cast<int>(GearTier::Dawnbound);
constexpr int MaxProgressionCount = 1000000;
constexpr float MaxHealth = 100.0f;
constexpr float MaxStamina = 100.0f;
constexpr float DodgeCost = 28.0f;
constexpr float HeavyCost = 22.0f;
constexpr float HealCost = 25.0f;

inline constexpr float WeaponDamage(int Tier)
{
    return Tier <= 0 ? 24.0f : Tier == 1 ? 30.0f : 38.0f;
}

inline constexpr int ForgeCost(int Tier)
{
    return Tier <= 0 ? 3 : Tier == 1 ? 6 : 0;
}

inline constexpr bool CanForge(int Tier, int Sigils)
{
    return Tier >= 0 && Tier < MaxTier && Sigils >= ForgeCost(Tier);
}

struct Reward
{
    int Sigils;
    int Victories;
};

// Clamp before addition so even corrupted local values cannot overflow int.
inline constexpr int IncrementProgression(int Value)
{
    return Value < 0 ? 1 : Value >= MaxProgressionCount ? MaxProgressionCount : Value + 1;
}

inline constexpr Reward GrantBossReward(int CurrentSigils, int CurrentVictories)
{
    return {IncrementProgression(CurrentSigils), IncrementProgression(CurrentVictories)};
}

// World boss participation never opts a player into PvP. Both players must
// explicitly opt in, and both must currently be inside a designated PvP zone.
inline constexpr bool CanDamagePlayer(bool AttackerOptedIn, bool TargetOptedIn,
                                      bool AttackerInZone, bool TargetInZone)
{
    return AttackerOptedIn && TargetOptedIn && AttackerInZone && TargetInZone;
}
} // namespace EmberRules
