#include "ACELootQuality.h"
#include "UI/ACERetailObjectNames.h"
#include "String/LexFromString.h"

FString ACELootQuality::UnratedReason(const FACEQualityReview& R)
{
    if(R.bRated)return {};
    if(!R.bIdentified)return TEXT("Waiting for identification");
    if(R.Reason.Contains(TEXT("Original roll unknown")))return TEXT("Original roll not recorded");
    if(R.Reason.Contains(TEXT("No verified generation")))return TEXT("Comparison range unavailable");
    if(R.Reason.Contains(TEXT("Active effects")))return TEXT("Spell effects could not be separated");
    if(R.Reason.Contains(TEXT("outside"),ESearchCase::IgnoreCase))return TEXT("Outside known loot ranges");
    if(R.Reason.Contains(TEXT("Covenant")))return TEXT("Covenant protection data incomplete");
    if(R.Reason.Contains(TEXT("protection"),ESearchCase::IgnoreCase))return TEXT("Protection range unavailable");
    return TEXT("Rating data incomplete");
}

FString ACELootQuality::ChatSummary(const FACEWorldObject& Item,const FACEQualityReview& R)
{
    const FString Tier=R.bRated?ACELootQuality::Name(R.Grade).ToUpper():!R.bEquipment?TEXT("ITEM"):R.bIdentified?TEXT("UNRATED"):TEXT("IDENTIFY");
    FString Text=TEXT("[")+Tier+TEXT("] ")+ACERetailObjectNames::Name(Item);
    if(R.bEstimated)Text+=TEXT(" (estimated)");
    if(!R.ClassMark.IsEmpty())Text+=TEXT(" (")+(R.ClassMark==TEXT("2H")?FString(TEXT("Two-Handed")):R.ClassMark==TEXT("H")?FString(TEXT("Heavy")):R.ClassMark==TEXT("L")?FString(TEXT("Light")):FString(TEXT("Finesse")))+TEXT(")");
    bool HasDamage=false,HasValue=false;
    for(const auto& S:R.CurrentStats)
    {
        if(S.Key==TEXT("Variance")||S.Key==TEXT("Mana")||S.Key==TEXT("Spellcraft")||S.Value==TEXT("—"))continue;
        if(S.Value.EndsWith(TEXT("%")))
        {double V=0;if(LexTryParseString(V,*S.Value.LeftChop(1))&&FMath::IsNearlyZero(V))continue;}
        FString Label=S.Label;
        if(S.Key==TEXT("Damage")){Label.Empty();HasDamage=true;}
        else if(S.Key==TEXT("Defense"))Label=TEXT("Melee D");
        else if(S.Key==TEXT("MissileDefense"))Label=TEXT("Missile D");
        else if(S.Key==TEXT("MagicDefense"))Label=TEXT("Magic D");
        else if(S.Key==TEXT("ManaConversion"))Label=TEXT("Mana C");
        else if(S.Key==TEXT("Element")){Label=TEXT("Spell dmg");HasDamage=true;}
        else if(S.Key==TEXT("ElementPvP"))Label=TEXT("PvP");
        else if(S.Key==TEXT("Armor"))Label=TEXT("AL");
        else if(S.Key==TEXT("ElementBonus"))Label=TEXT("Damage bonus");
        else if(S.Key==TEXT("Work"))Label=TEXT("Work");
        else if(S.Key==TEXT("Value"))HasValue=true;
        Text+=TEXT(" - ")+(Label.IsEmpty()?FString():Label+TEXT(" "))+S.Value;
        if((S.Key==TEXT("Damage")||S.Key==TEXT("Element"))&&!R.DamageType.IsEmpty())Text+=TEXT(" ")+R.DamageType;
        Text+=S.SpellContribution;
    }
    if(!HasDamage&&!R.DamageType.IsEmpty())Text+=TEXT(" - ")+R.DamageType;
    if(Item.StackSize>1)Text+=FString::Printf(TEXT(" - Count %d"),Item.StackSize);
    if(!HasValue)Text+=TEXT(" - Value ")+FText::AsNumber(Item.Value).ToString();
    Text+=TEXT(" - Burden ")+FString::FromInt(Item.Burden);
    TArray<FString> Useful;
    for(const auto& Name:R.SpellNames)
        if(const auto* Grade=R.SpellGrades.Find(Name);Grade&&*Grade>=ACELootScoring::Grade::Uncommon)Useful.Add(Name);
    if(!Useful.IsEmpty())Text+=TEXT(" - ")+FString::Join(Useful,TEXT(" · "));
    if(R.bEquipment&&!R.bRated)Text+=TEXT(" - ")+UnratedReason(R);
    return Text;
}
