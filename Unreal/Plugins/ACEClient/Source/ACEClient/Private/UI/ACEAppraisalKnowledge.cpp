#include "ACEAppraisalKnowledge.h"

namespace ACEAppraisalKnowledge
{
TArray<ACEAppraisalFormatting::FCreatureDetailLine> CreatureInsights(const FACEAppraisalInfo& Info,
    const FACEWorldObject* Subject,UACEDatSubsystem* Dat,const FACEWorldKnowledge& Knowledge)
{
    TArray<ACEAppraisalFormatting::FCreatureDetailLine> Rows;
    if(!Info.bIsCreature||ACEAppraisalFormatting::UsesCharacterExamination(Info)||(Subject&&(Subject->bIsPlayer||Subject->IsVendor()||!Subject->IsAttackable())))return Rows;
    const auto* C=Subject?Knowledge.Find(uint32(Subject->WeenieClassId)):nullptr;
    const FLinearColor Gold(.88f,.76f,.48f),Muted(.7f,.77f,.81f);
    auto Wide=[&](const FString& Text,FLinearColor Color=FLinearColor::White){Rows.Add({TEXT(""),Text,Color,true});};
    auto Heading=[&](const TCHAR* Text){Wide(TEXT(""));Wide(Text,Gold);};
    Heading(TEXT("Attacks"));
    TArray<FString> Types;
    const uint32 AttackTypes=Info.bSuccess&&Info.DamageType?uint32(Info.DamageType):C?C->MeleeTypes:0;
    for(const auto& E:{TPair<uint32,const TCHAR*>(1,TEXT("Slashing")),{2,TEXT("Piercing")},{4,TEXT("Bludgeoning")},
        {8,TEXT("Cold")},{16,TEXT("Fire")},{32,TEXT("Acid")},{64,TEXT("Lightning")},{1024,TEXT("Nether")}})
        if(AttackTypes&E.Key)Types.Add(E.Value);
    Wide(Types.IsEmpty()?TEXT("Melee damage types: unknown"):TEXT("Melee: ")+FString::Join(Types,TEXT(" / ")));
    if(C)
    {
        if(C->Spells.IsEmpty())Wide(TEXT("No spells listed in this reference."),Muted);
        for(uint32 Spell:C->Spells)
        {
            FString Name;uint32 Icon=0,School=0,Power=0,Category=0,Flags=0;double Duration=0;
            if(!Dat||!Dat->TryGetSpellInfo(Spell,Name,Icon))Name=FString::Printf(TEXT("Spell %u"),Spell);
            FString Kind=TEXT("Spell");
            if(Dat&&Dat->TryGetPluginSpellInfo(Spell,School,Power,Category,Flags,Duration))
                Kind=(Flags&4)?TEXT("Self/support"):(School==3||School==4)?TEXT("Debuff"):TEXT("Attack spell");
            Wide(Kind+TEXT(": ")+Name);
        }
        Wide(TEXT("Known repertoire; cast frequency is not measured."),Muted);
    }
    else Wide(TEXT("Spell repertoire: no reference for this creature."),Muted);
    Heading(TEXT("Damage vulnerabilities"));
    TArray<FString> Best;double Highest=-1;int32 Known=0;
    struct R {uint32 Key;const TCHAR* Name;};
    const R Resists[]={{64,TEXT("Slashing")},{65,TEXT("Piercing")},{66,TEXT("Bludgeoning")},{67,TEXT("Fire")},
        {68,TEXT("Cold")},{69,TEXT("Acid")},{70,TEXT("Lightning")},{166,TEXT("Nether")}};
    TArray<FString> Values;
    for(const auto& R:Resists)
    {
        const double* Live=Info.bSuccess?Info.FloatProperties.Find(R.Key):nullptr;
        const double* Base=C?C->Resistances.Find(R.Key):nullptr;
        const double* V=Live?Live:Base;
        if(!V||!FMath::IsFinite(*V)||*V<0)continue;
        ++Known;
        Values.Add(FString::Printf(TEXT("%s: x%.2f (%s)"),R.Name,*V,Live?TEXT("appraised"):TEXT("reference")));
        if(*V>Highest+.0001){Highest=*V;Best={R.Name};}else if(FMath::IsNearlyEqual(*V,Highest,.0001))Best.Add(R.Name);
    }
    if(Known)Wide(TEXT("Most susceptible among known types: ")+FString::Join(Best,TEXT(" / ")));
    for(const auto& V:Values)Wide(V);
    if(!Known)Wide(TEXT("Damage multipliers: unknown."),Muted);
    else Wide(TEXT("Higher multiplier means more damage taken. Armor, spells and debuffs also affect damage."),Muted);
    Heading(TEXT("Trophies & fixed drops"));
    if(!C)Wide(TEXT("Drop information unavailable for this creature."),Muted);
    else if(C->Drops.IsEmpty())Wide(TEXT("No direct drops listed; other loot may still drop."),Muted);
    else for(const auto& Drop:C->Drops)
        Wide(Drop.Name+(Drop.BaselineChance.IsSet()?FString::Printf(TEXT(" — baseline %.2f%%"),Drop.BaselineChance.GetValue()*100):TEXT(" — rate unknown")));
    if(C)
    {
        Wide(Knowledge.GetSource()+TEXT(" · reference information"),Muted);
        Wide(TEXT("Rates assume standard trophy settings. Server changes, events, procedural loot and scripted rewards are not covered."),Muted);
    }
    return Rows;
}
TArray<ACEAppraisalFormatting::FCreatureDetailLine> CompactCreatureInsights(const FACEAppraisalInfo& Info,
    const FACEWorldObject* Subject,UACEDatSubsystem* Dat,const FACEWorldKnowledge& Knowledge)
{
    using FRow=ACEAppraisalFormatting::FCreatureDetailLine;
    const auto Full=CreatureInsights(Info,Subject,Dat,Knowledge);
    TArray<FRow> Rows;if(Full.IsEmpty())return Rows;
    const auto* C=Subject?Knowledge.Find(uint32(Subject->WeenieClassId)):nullptr;
    const FString Reference=Knowledge.GetSource()+TEXT(". Public baseline; server content may differ. ");
    const FLinearColor Gold(.88f,.76f,.48f),Muted(.7f,.77f,.81f);
    auto Heading=[&](const TCHAR* Title,const FString& Tip){FRow R;R.Value=Title;R.bFullWidth=true;R.Color=Gold;R.Tooltip=Tip;Rows.Add(R);};
    auto Tile=[&](int Group,uint32 Icon,const FString& Text,const FString& Tip,FLinearColor Color=FLinearColor::White)
    {FRow R;R.Value=Tip;R.DisplayValue=Text;R.IconDid=Icon;R.TileGroup=Group;R.Color=Color;R.Tooltip=Tip;Rows.Add(R);};
    auto Icon=[&](uint32 Spell){FString Name;uint32 Did=0;if(Dat)Dat->TryGetSpellInfo(Spell,Name,Did);return Did;};
    struct D {uint32 Bit,Resistance,Spell;const TCHAR* Name;const TCHAR* Short;};
    const D Damage[]={{1,64,92,TEXT("Slashing"),TEXT("Slash")},{2,65,86,TEXT("Piercing"),TEXT("Pierce")},
        {4,66,64,TEXT("Bludgeoning"),TEXT("Blunt")},{16,67,27,TEXT("Fire"),TEXT("Fire")},
        {8,68,28,TEXT("Cold"),TEXT("Cold")},{32,69,58,TEXT("Acid"),TEXT("Acid")},
        {64,70,75,TEXT("Lightning"),TEXT("Shock")},{1024,166,0,TEXT("Nether"),TEXT("Neth")}};
    auto Unknown=[&](const TCHAR* Text){FRow R;R.Value=Text;R.bFullWidth=true;R.Color=Muted;Rows.Add(R);};
    Heading(TEXT("Vulnerabilities"),Reference+TEXT("Damage taken, highest first. 100% is normal; 150% means 1.5 times normal damage before armor and other effects. Unknown types are omitted."));
    Unknown(TEXT("Damage taken · 100% = normal"));
    struct FVulnerability {int32 Index;double Value;bool Live;};
    TArray<FVulnerability> Vulnerabilities;
    for(int32 I=0;I<UE_ARRAY_COUNT(Damage);++I)
    {
        const double* Live=Info.bSuccess?Info.FloatProperties.Find(Damage[I].Resistance):nullptr;
        const double* V=Live?Live:C?C->Resistances.Find(Damage[I].Resistance):nullptr;
        if(V&&FMath::IsFinite(*V)&&*V>=0)Vulnerabilities.Add({I,*V,Live!=nullptr});
    }
    Vulnerabilities.StableSort([](const FVulnerability& A,const FVulnerability& B){return A.Value>B.Value;});
    const double Highest=Vulnerabilities.IsEmpty()?-1:Vulnerabilities[0].Value;
    const double Lowest=Vulnerabilities.IsEmpty()?-1:Vulnerabilities.Last().Value;
    for(const auto& V:Vulnerabilities)
    {
        const auto& D=Damage[V.Index];
        const FLinearColor Color=Highest>Lowest+.0001?(FMath::IsNearlyEqual(V.Value,Highest,.0001)?FLinearColor(.55f,1,.55f):FLinearColor(.6f,.8f,1)):FLinearColor::White;
        const uint32 Did=Icon(D.Spell);
        const FString Percent=FString::SanitizeFloat(FMath::RoundToDouble(V.Value*1000.)/10.,0)+TEXT("%");
        Tile(2,Did,FString(D.Name)+TEXT(" ")+Percent,
            FString::Printf(TEXT("%s: x%.2f (%s). Higher means more damage taken."),D.Name,V.Value,V.Live?TEXT("appraised"):TEXT("reference")),Color);
        Rows.Last().Label=D.Name;
    }
    if(Vulnerabilities.IsEmpty())Unknown(TEXT("Unknown"));
    Heading(TEXT("Common Attacks"),Reference+TEXT("Known attack types and offensive spell repertoire; use frequency is not measured. Self/support spells are excluded when spell metadata identifies them."));
    Heading(TEXT("Melee"),Reference+TEXT("Known melee damage types."));
    const uint32 Types=Info.bSuccess&&Info.DamageType?uint32(Info.DamageType):C?C->MeleeTypes:0;
    for(const auto& D:Damage)if(Types&D.Bit)Tile(1,Icon(D.Spell),D.Name,FString(TEXT("Melee: "))+D.Name);
    if(!Types)Unknown(TEXT("Unknown"));
    Heading(TEXT("Magic"),Reference+TEXT("Offensive spells and debuffs; frequency unknown."));
    int32 MagicCount=0;
    if(C)for(uint32 Spell:C->Spells)
    {
        FString Name;uint32 Did=0,School=0,Power=0,Category=0,Flags=0;double Duration=0;
        const bool Metadata=Dat&&Dat->TryGetPluginSpellInfo(Spell,School,Power,Category,Flags,Duration);
        if(Metadata&&(Flags&4))continue;
        if(!Dat||!Dat->TryGetSpellInfo(Spell,Name,Did))Name=FString::Printf(TEXT("Spell %u"),Spell);
        const FString Kind=!Metadata?TEXT("Spell (type unclassified)"):(School==3||School==4)?TEXT("Debuff"):TEXT("Attack spell");
        Tile(4,Did,Name,TEXT("Magic: ")+Name+TEXT(". ")+Kind+TEXT("; frequency unknown."));
        ++MagicCount;
    }
    if(!MagicCount)Unknown(C?TEXT("No offensive spells listed"):TEXT("Unknown"));
    Heading(TEXT("Missile"),Reference+TEXT("Missile damage requires a separate confirmed source; melee types are not reused."));
    if(C&&C->MissileTypes.IsSet())
    {
        const uint32 Missile=C->MissileTypes.GetValue();
        for(const auto& D:Damage)if(Missile&D.Bit)Tile(5,Icon(D.Spell),D.Name,FString(TEXT("Missile: "))+D.Name);
        if(!Missile)Unknown(TEXT("None listed"));
    }
    else Unknown(TEXT("Unknown"));
    Heading(TEXT("Drops"),Reference+TEXT("Direct trophies and fixed drops only. Rates assume standard trophy settings, not confirmed live-server rates. Procedural loot and scripted rewards are not covered."));
    if(C)for(const auto& Drop:C->Drops)
        Tile(3,Drop.IconDid,Drop.IconDid?TEXT(""):TEXT("?"),
            Drop.Name+(Drop.BaselineChance.IsSet()?FString::Printf(TEXT(" — baseline %.2f%%"),Drop.BaselineChance.GetValue()*100):TEXT(" — rate unknown"))+TEXT(". ")+Reference);
    if(!C||C->Drops.IsEmpty()){FRow R;R.Value=C?TEXT("No direct drops listed"):TEXT("Drop information unavailable");R.bFullWidth=true;R.Color=Muted;Rows.Add(R);}
    return Rows;
}

}
