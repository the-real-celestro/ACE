#pragma once

// Deliberately independent of Unreal: the shipped evaluator and offline review
// use the same arithmetic. Scores describe utility, never drop probabilities.
#include <algorithm>
#include <cmath>
#include <limits>

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
// Health and healing are valuable across character builds. Give the best
// native rolls progressively stronger credit, separate from spell packages.
// Public loot jewelry rolls 1..3. Higher custom values keep their ordering
// without pretending they belong to that verified generation bracket.
inline double NativeGearPoints(int Property,int Rating,int NormalMaximum=3)
{
    if(Rating<=0||Property<370||Property>379)return 0;
    // Best ordinary armor rolls are +2; jewelry/clothing rolls reach +3.
    // These are quality credits, not a claim that one rating gives this % benefit.
    constexpr double Best[]={45,45,30,30,35,35,45,25,25,50};
    const int Top=NormalMaximum==2?2:3;
    const double Curve3[]={0,.25,.60,1};
    const double Curve2[]={0,.40,1};
    const double Roll=Rating<=Top?(Top==2?Curve2[Rating]:Curve3[Rating]):1.+.25*(Rating-Top);
    return Best[Property-370]*Roll;
}
// ACE rolls six independent essence ratings: absent=0, otherwise 1d10
// with a tier-dependent chance of a second d10. All six can reach 20 together.
// This is combination quality, not a percentile or a drop-probability claim.
struct EssenceCombination
{
    bool Valid=false;
    int Total=0;
    double Points()const{return Valid?100.*Total/120.:0.;}
};
inline EssenceCombination Essence(const int* Ratings)
{
    int Total=0;
    for(int I=0;I<6;++I){if(Ratings[I]<0||Ratings[I]>20)return {};Total+=Ratings[I];}
    return {true,Total};
}
// Item-local banes are replaceable protection, not a high-value wearable spell
// package. Even a full Legendary bane earns at most a quarter quality point.
inline double ArmorBanePoints(double Multiplier, double Bonus)
{
    if (!std::isfinite(Multiplier) || !std::isfinite(Bonus)) return 0;
    return std::clamp(std::clamp(Multiplier + Bonus, 0., 2.) - std::clamp(Multiplier, 0., 2.), 0., .25);
}
struct CovenantProtection
{
    double ArmorRoll=0,Protection=0;
    double Total()const{return ArmorRoll+Protection;}
};
// Seven damage types in property order 13..19. The denominator is the
// bracket's best AL with ordinary built-in Impenetrability VIII and capped
// protections. Stacking cantrips may exceed that benchmark; resistances may not.
inline CovenantProtection Covenant(double Armor,Range Bounds,double SelfImpen,const double* Resistances)
{
    if(!Bounds.Variable()||!Bounds.Contains(Armor)||!std::isfinite(SelfImpen)||SelfImpen<0)return {};
    double Mean=0,Weakest=1.e100;
    for(int I=0;I<7;++I)
    {
        if(!std::isfinite(Resistances[I]))return {};
        const double Low=Bounds.Low*(I<3?1.3:.6),High=(Bounds.High+240)*2;
        const double Effective=(Armor+SelfImpen)*std::clamp(Resistances[I],0.,2.);
        const double Quality=std::max(0.,(Effective-Low)/(High-Low));
        Mean+=Quality/7;Weakest=std::min(Weakest,Quality);
    }
    return {20*Position(Armor,Bounds),60*(.75*Mean+.25*Weakest)};
}
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
    SupportSkill, Ward, Armor, ManaEfficiency, Speed, BaselineAttribute, Shield,
    Regeneration, Crafting, Utility, Movement, Social, Appraisal, HealthVital, StaminaVital, ManaVital, NativeRating };
inline double SpellRankFactor(int Rank)
{
    constexpr double Values[]={0,.25,.55,.94,1};
    return Values[std::clamp(Rank,0,4)];
}
inline Grade SpellUsefulness(int Rank,double Relevance)
{
    return Band(100*SpellRankFactor(Rank)*(std::isfinite(Relevance)?std::clamp(Relevance,0.,1.):0));
}
inline double WearableRelevanceFactor(Role Effect,bool UnderclothesImpen=false,bool CharacterArmor=false)
{
    if(UnderclothesImpen)return 1.05;
    switch(Effect)
    {
    case Role::WeaponSkill:case Role::MagicDefense:return 1;
    case Role::MeleeDefense:return .99;
    case Role::PrimaryAttribute:case Role::SurvivalAttribute:return .98;
    case Role::Defender:return .96;
    case Role::SecondaryAttribute:return .94;
    case Role::BaselineAttribute:return .90;
    case Role::SupportSkill:return .89;
    case Role::Shield:return .82;
    case Role::MissileDefense:return .85;
    case Role::Ward:return .65;
    case Role::Armor:return CharacterArmor?.90:.55;
    case Role::ManaEfficiency:return .72;
    case Role::Regeneration:return .72;
    case Role::Crafting:return .40;
    case Role::Utility:return .60;
    case Role::Movement:return .50;
    case Role::Social:return .45;
    case Role::Appraisal:return .30;
    case Role::Damage:case Role::Attack:return 1;
    default:return 0;
    }
}
// Potential of the strongest build-relevant effect at the set's highest DAT
// tier. This is quality credit for a piece, not a currently active stat bonus.
inline double EquipmentSetPoints(Role Effect,double Amount)
{
    if(!std::isfinite(Amount)||Amount<=0)return 0;
    switch(Effect)
    {
    case Role::WeaponSkill:case Role::PrimaryAttribute:case Role::MeleeDefense:case Role::MagicDefense:
    case Role::SecondaryAttribute:case Role::SurvivalAttribute:case Role::BaselineAttribute:
    case Role::SupportSkill:case Role::MissileDefense:case Role::Shield:
    case Role::Crafting:case Role::Utility:case Role::Movement:case Role::Social:case Role::Appraisal:
        return 24*WearableRelevanceFactor(Effect)*std::clamp(Amount/20.,0.,1.);
    case Role::Ward:return 16*std::clamp(Amount/.2,0.,1.);
    case Role::Armor:return 18*std::clamp(Amount/80.,0.,1.);
    case Role::Regeneration:return 14*std::clamp(Amount/.6,0.,1.);
    case Role::HealthVital:return 24*std::clamp(Amount/20.,0.,1.);
    case Role::StaminaVital:return 14*std::clamp(Amount/40.,0.,1.);
    case Role::ManaVital:return 14*std::clamp(Amount/30.,0.,1.);
    case Role::NativeRating:return 30*std::clamp(Amount/20.,0.,1.);
    default:return 0;
    }
}
inline double LegendaryPoints(Role Effect, bool Caster = false)
{
    switch (Effect)
    {
    case Role::Damage: return Caster ? 10 : 8;
    case Role::Attack: return 5;
    case Role::Defender: return Caster ? 14 : 7;
    case Role::MeleeDefense: case Role::MagicDefense: return Caster ? 7 : 5;
    case Role::MissileDefense: return 4;
    case Role::WeaponSkill: return 5;
    case Role::PrimaryAttribute: return 6;
    case Role::SecondaryAttribute: return 5.5;
    case Role::SurvivalAttribute: return 6;
    case Role::BaselineAttribute: return 5;
    case Role::SupportSkill:case Role::Shield:return 3;
    case Role::Regeneration:return 2.5;
    case Role::Utility:return 1.5;
    case Role::Movement:return 1;
    case Role::Social:case Role::Appraisal:case Role::Crafting:return .5;
    case Role::HealthVital:case Role::StaminaVital:case Role::ManaVital:case Role::NativeRating:return 0;
    case Role::Ward:return 2;
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

struct TinkerAxis
{
    double Current=0;
    Range Bounds;
    double Add=0,Multiply=1,Preference=1;
};
struct TinkerEstimate
{
    bool Valid=false;
    int Histories=0;
    double Base[8]={},Low=std::numeric_limits<double>::max(),High=0;
    int Operations[8]={};
};
// Reconstruct feasible public-ACE histories, never an observed original.
// Unassigned attempts may have changed speed, value, burden, activation or imbue.
// Preferences choose one plausible history; Low/High retain the ambiguity.
template<typename ScoreFn>
TinkerEstimate InferTinkers(const TinkerAxis* Axes,int Count,int Attempts,int Reserved,ScoreFn Score)
{
    TinkerEstimate Out;
    if(!Axes||Count<1||Count>8||Attempts<1||Attempts>10||Reserved<0||Reserved>Attempts)return Out;
    const int Budget=Attempts-Reserved;double Values[8]={},Best=-1;int Ops[8]={};
    auto Factorial=[](int N){double V=1;for(int I=2;I<=N;++I)V*=I;return V;};
    auto Visit=[&](auto&& Self,int Axis,int Left,double Weight)->void
    {
        if(Axis==Count)
        {
            const double Points=Score(Values);if(!std::isfinite(Points))return;
            ++Out.Histories;Out.Low=std::min(Out.Low,Points);Out.High=std::max(Out.High,Points);
            Weight*=std::pow(.18,Left)/Factorial(Left);
            if(Weight>Best)
            {
                Best=Weight;Out.Valid=true;
                for(int I=0;I<Count;++I){Out.Base[I]=Values[I];Out.Operations[I]=Ops[I];}
            }
            return;
        }
        const auto& A=Axes[Axis];
        if(!std::isfinite(A.Current)||!std::isfinite(A.Add)||A.Multiply<=0||!std::isfinite(A.Multiply))return;
        const int Limit=A.Add==0&&A.Multiply==1?0:Left;
        for(int N=0;N<=Limit;++N)
        {
            const double Value=(A.Current-N*A.Add)/std::pow(A.Multiply,N);
            if(!A.Bounds.Contains(Value))continue;
            Values[Axis]=std::clamp(Value,A.Bounds.Low,A.Bounds.High);Ops[Axis]=N;
            Self(Self,Axis+1,Left-N,Weight*std::pow(A.Preference,N)/Factorial(N));
        }
    };
    Visit(Visit,0,Budget,1);return Out;
}
}
