#include "EmberRules.h"

#include <cassert>
#include <iostream>
#include <limits>

int main()
{
    int Checks = 0;
    const auto Check = [&Checks](bool Condition, const char* Description)
    {
        ++Checks;
        if (!Condition)
        {
            std::cerr << "FAILED: " << Description << '\n';
        }
        assert(Condition);
    };

    using namespace EmberRules;
    Check(WeaponDamage(0) == 24.0f, "starter weapon damage");
    Check(WeaponDamage(1) == 30.0f, "first earned upgrade damage");
    Check(WeaponDamage(2) == 38.0f, "final earned upgrade damage");
    Check(WeaponDamage(-1) == WeaponDamage(0), "damage clamps below minimum tier");
    Check(WeaponDamage(std::numeric_limits<int>::max()) == WeaponDamage(MaxTier),
          "damage clamps above maximum tier");
    Check(ForgeCost(0) == 3, "first upgrade requires three victories");
    Check(ForgeCost(1) == 6, "second upgrade requires six additional victories");
    Check(ForgeCost(2) == 0, "there is no upgrade after maximum tier");
    Check(!CanForge(0, -1), "negative currency cannot forge");
    Check(!CanForge(-1, 100), "invalid negative tier cannot forge");
    Check(!CanForge(MaxTier + 1, 100), "invalid excessive tier cannot forge");
    Check(!CanForge(MaxTier, 100), "maximum tier cannot charge a free upgrade");
    Check(!CanForge(0, 2), "one missing sigil blocks first upgrade");
    Check(CanForge(0, 3), "exact first upgrade cost accepted");
    Check(!CanForge(1, 5), "one missing sigil blocks second upgrade");
    Check(CanForge(1, 6), "exact second upgrade cost accepted");

    int Tier = 0;
    Reward Progress{0, 0};
    for (int Victory = 1; Victory <= 9; ++Victory)
    {
        Progress = GrantBossReward(Progress.Sigils, Progress.Victories);
        Check(Progress.Victories == Victory, "every kill records one victory");
        const bool ExpectedUpgrade = Victory == 3 || Victory == 9;
        Check(CanForge(Tier, Progress.Sigils) == ExpectedUpgrade,
              "upgrades become available only after earning the required sigils");
        if (CanForge(Tier, Progress.Sigils))
        {
            Progress.Sigils -= ForgeCost(Tier);
            ++Tier;
        }
        Check(Progress.Sigils >= 0, "forging never produces negative currency");
    }
    Check(Tier == MaxTier && Progress.Sigils == 0 && Progress.Victories == 9,
          "nine victories fund both upgrades with no purchased power");

    const Reward Negative = GrantBossReward(-10, std::numeric_limits<int>::min());
    Check(Negative.Sigils == 1 && Negative.Victories == 1,
          "corrupted negative progress is repaired before granting a reward");
    const Reward Full = GrantBossReward(MaxProgressionCount, MaxProgressionCount);
    Check(Full.Sigils == MaxProgressionCount && Full.Victories == MaxProgressionCount,
          "progress saturates at the supported maximum");
    const Reward Overflow = GrantBossReward(std::numeric_limits<int>::max(),
                                           std::numeric_limits<int>::max());
    Check(Overflow.Sigils == MaxProgressionCount && Overflow.Victories == MaxProgressionCount,
          "corrupted excessive progress cannot overflow");

    for (int Flags = 0; Flags < 16; ++Flags)
    {
        const bool Allowed = CanDamagePlayer((Flags & 1) != 0, (Flags & 2) != 0,
                                             (Flags & 4) != 0, (Flags & 8) != 0);
        Check(Allowed == (Flags == 15),
              "PvP requires both players to opt in and both to be in the PvP zone");
    }
    Check(DodgeCost > 0.0f && DodgeCost <= MaxStamina, "dodge has an affordable positive cost");
    Check(HeavyCost > 0.0f && HeavyCost <= MaxStamina, "heavy attack has an affordable positive cost");
    Check(HealCost > 0.0f && HealCost <= MaxStamina, "healing has an affordable positive cost");

    std::cout << "PASS: " << Checks << " progression, combat cost, and PvP rule assertions\n";
    return 0;
}
