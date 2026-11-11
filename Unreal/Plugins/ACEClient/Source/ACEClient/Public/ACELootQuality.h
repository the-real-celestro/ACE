#pragma once

#include "CoreMinimal.h"
#include "ACETypes.h"
#include "ACELootScoring.h"

class UACEDatSubsystem;

struct FACEQualityStat
{
    FString Label;
    double Original = 0, Permanent = 0, WithCantrip = 0;
    ACELootScoring::Range Bounds;
    bool bOriginalKnown = false, bPermanentKnown = true, bPercent = false, bLowerBetter = false;
    bool bPotential = true;
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
};
struct FACEQualityReview
{
    bool bEquipment = false, bRated = false, bOriginalKnown = false;
    bool bCannotWield = false, bActivationBlocked = false;
    FString Class, ClassMark, Reason, Requirements, Activation, DamageType;
    double OriginalPoints = 0, CantripPoints = 0, PropertyPoints = 0, Points = 0;
    ACELootScoring::Physical Components;
    ACELootScoring::Grade Grade = ACELootScoring::Grade::Poor;
    TArray<FACEQualityStat> Stats;
    TArray<FACEQualitySpell> Spells;
    int32 Tinkers = 0;
};

namespace ACELootQuality
{
    ACECLIENT_API FACEQualityReview Evaluate(const FACEWorldObject& Item, const FACEAppraisalInfo& Appraisal,
        const FACEPlayerVitals& Character, const TArray<FACEActiveEnchantment>& Active,
        UACEDatSubsystem* Dat, const FACEQualityOriginal* Original = nullptr);
    ACECLIENT_API bool CaptureOriginal(const FACEWorldObject& Item, const FACEAppraisalInfo& Appraisal,
        const TArray<FACEActiveEnchantment>& Active, UACEDatSubsystem* Dat, FACEQualityOriginal& Out);
    ACECLIENT_API FLinearColor Color(ACELootScoring::Grade Grade);
    ACECLIENT_API FString Name(ACELootScoring::Grade Grade);
    ACECLIENT_API FString FormatStat(double Value, bool Percent);
#if WITH_DEV_AUTOMATION_TESTS
    ACECLIENT_API int32 ValidateWeaponBrackets(UACEDatSubsystem* Dat,TArray<FString>& Errors);
#endif
}
