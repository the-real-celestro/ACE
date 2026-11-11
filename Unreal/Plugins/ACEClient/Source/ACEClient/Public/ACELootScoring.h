#pragma once

// Deliberately independent of Unreal: the shipped evaluator and offline review
// use the same arithmetic. Scores describe utility, never drop probabilities.
#include <algorithm>
#include <cmath>

namespace ACELootScoring
{
enum class Grade { Poor, Common, Uncommon, Rare, Epic, Legendary };
inline Grade Band(double Points)
{
    return Points < 20 ? Grade::Poor : Points < 50 ? Grade::Common
        : Points < 75 ? Grade::Uncommon : Points < 90 ? Grade::Rare
        : Points < 97 ? Grade::Epic : Grade::Legendary;
}
inline const char* Name(Grade Value)
{
    constexpr const char* Names[] = {"Poor", "Common", "Uncommon", "Rare", "Epic", "Legendary"};
    return Names[static_cast<int>(Value)];
}
struct Range
{
    double Low = 0, High = 0;
    bool Variable() const { return std::isfinite(Low) && std::isfinite(High) && High > Low + 1.e-8; }
    bool Contains(double Value, double Epsilon = 1.e-4) const
    { return std::isfinite(Value) && Value >= Low - Epsilon && Value <= High + Epsilon; }
};
inline double Position(double Value, Range Bounds)
{
    return Bounds.Variable() && std::isfinite(Value)
        ? std::clamp((Value - Bounds.Low) / (Bounds.High - Bounds.Low), 0., 1.) : 0.;
}
inline double MeanHit(double Maximum, double Variance) { return Maximum * (1. - Variance / 2.); }
struct Physical
{
    double Damage = 0, Attack = 0, Defense = 0;
    double Total() const { return Damage + Attack + Defense; }
};
inline Physical Melee(double Mean, double Attack, double Defense,
    Range MeanBounds, Range AttackBounds, Range DefenseBounds)
{
    const double WD = MeanBounds.Variable() ? 85 : 0;
    const double WA = AttackBounds.Variable() ? 10 : 0;
    const double WF = DefenseBounds.Variable() ? 5 : 0;
    const double Sum = WD + WA + WF;
    if (Sum <= 0) return {};
    return {100 * WD / Sum * Position(Mean, MeanBounds),
        100 * WA / Sum * Position(Attack, AttackBounds),
        100 * WF / Sum * Position(Defense, DefenseBounds)};
}
enum class Role { Irrelevant, Damage, Attack, Defender, MeleeDefense, MagicDefense,
    MissileDefense, WeaponSkill, PrimaryAttribute, SecondaryAttribute, SurvivalAttribute,
    SupportSkill, Ward, Armor, ManaEfficiency, Speed };
inline double LegendaryPoints(Role Effect, bool Caster = false)
{
    switch (Effect)
    {
    case Role::Damage: return Caster ? 10 : 8;
    case Role::Attack: return 5;
    case Role::Defender: return Caster ? 12 : 6;
    case Role::MeleeDefense: case Role::MagicDefense: return Caster ? 7 : 5;
    case Role::MissileDefense: return 4;
    case Role::WeaponSkill: return 5;
    case Role::PrimaryAttribute: return 4.2;
    case Role::SecondaryAttribute: return 2.8;
    case Role::SurvivalAttribute: return 2;
    case Role::SupportSkill: case Role::Ward: return 2;
    case Role::Armor: return 3;
    case Role::ManaEfficiency: return 3;
    case Role::Speed: case Role::Irrelevant: return 0;
    }
    return 0;
}
inline double CantripPoints(Role Effect, double NativeAmount, double LegendaryAmount, bool Caster = false)
{
    if (!std::isfinite(NativeAmount) || !std::isfinite(LegendaryAmount) || std::abs(LegendaryAmount) < 1.e-10) return 0;
    return LegendaryPoints(Effect, Caster) * std::clamp(std::abs(NativeAmount / LegendaryAmount), 0., 1.);
}
// There is no global bonus ceiling: distinct useful packages must retain order
// above the Legendary threshold. Duplicate categories are removed by the adapter.
inline double Overall(double Original, double Cantrips) { return Original + std::max(0., Cantrips); }
}
