#pragma once

#include "CoreMinimal.h"
#include "ACETypes.h"
#include "ACELootScoring.h"

class UACEDatSubsystem;

struct FACEQualityStat
{
    FString Key, Label;
    double Original = 0, Permanent = 0, WithCantrip = 0;
    ACELootScoring::Range Bounds;
    bool bOriginalKnown = false, bPermanentKnown = true, bPercent = false, bLowerBetter = false;
    bool bOriginalEstimated = false;
    bool bPotential = true;
    // Armor previews use effective armor points, matching the displayed total.
    FString CantripPreview;
};
struct FACEQualitySpell
{
    int32 SpellId = 0, Family = 0, Rank = 0;
    FString Name, Effect, Status;
    double Points = 0, WearableBenefit = 0;
    bool bActive = false;
};
struct FACEQualityOriginal
{
    // Only an observed, unbuffed, zero-tinker appraisal can populate this.
    int32 WCID = 0, Setup = 0, Icon = 0;
    FString Name;
    TMap<FString, double> Stats;
    bool bEstimated=false;
    double EstimateLow=0,EstimateHigh=0;
};
struct FACEQualityRequirement
{
    FString Label, Have;
    bool bMet=false,bKnown=true;
};
struct FACEQualityReview
{
    bool bEquipment = false, bRated = false, bOriginalKnown = false, bIdentified = false;
    bool bCannotWield = false, bActivationBlocked = false;
    bool bEstimated=false;
    double EstimateLow=0,EstimateHigh=0;
    FString Class, ClassMark, Reason, Requirements, Activation, DamageType;
    double OriginalPoints = 0, CantripPoints = 0, PropertyPoints = 0, Points = 0;
    ACELootScoring::Physical Components;
    ACELootScoring::Grade Grade = ACELootScoring::Grade::Poor;
    TArray<FACEQualityStat> Stats;
    TArray<FACEQualitySpell> Spells;
    TArray<FACEItemStatLine> CurrentStats;
    TArray<FString> Properties, SpellNames;
    // Per-spell usefulness colors share the contextual quality factors.
    TMap<FString,ACELootScoring::Grade> SpellGrades;
    TArray<FACEQualityRequirement> Checks;
    int32 Tinkers = 0;
};

namespace ACELootQuality
{
    /** Numeric shortfall filter only; structural/training/server gates remain strict. */
    ACECLIENT_API bool CanUseWithin(const FACEAppraisalInfo& Appraisal,const FACEPlayerVitals& Character,
        int32 SkillGap=0,int32 LevelGap=0,int32 AttributeGap=0,int32 LoreGap=0,const FACEWorldObject* Item=nullptr);
    ACECLIENT_API int32 AttackSkill(const FACEWorldObject& Item,const FACEAppraisalInfo& Appraisal);
    ACECLIENT_API bool IsSummoningEssence(const FACEWorldObject& Item);
    ACECLIENT_API FACEQualityReview Evaluate(const FACEWorldObject& Item, const FACEAppraisalInfo& Appraisal,
        const FACEPlayerVitals& Character, const TArray<FACEActiveEnchantment>& Active,
        UACEDatSubsystem* Dat, const FACEQualityOriginal* Original = nullptr);
    ACECLIENT_API bool CaptureOriginal(const FACEWorldObject& Item, const FACEAppraisalInfo& Appraisal,
        const TArray<FACEActiveEnchantment>& Active, UACEDatSubsystem* Dat, FACEQualityOriginal& Out);
    ACECLIENT_API FLinearColor Color(ACELootScoring::Grade Grade);
    ACECLIENT_API FString Name(ACELootScoring::Grade Grade);
    ACECLIENT_API FString FormatStat(double Value, bool Percent);
    ACECLIENT_API FString UnratedReason(const FACEQualityReview& Review);
    ACECLIENT_API FString ChatSummary(const FACEWorldObject& Item,const FACEQualityReview& Review);
#if WITH_DEV_AUTOMATION_TESTS
    ACECLIENT_API int32 ValidateWeaponBrackets(UACEDatSubsystem* Dat,TArray<FString>& Errors);
#endif
}
