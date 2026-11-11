#include "ACELootQuality.h"
#include "ACEDatSubsystem.h"
#include "Mods/ACEPluginEquipment.h"

namespace
{
using namespace ACELootScoring;
struct FLootVariant { int32 Family, Skill; const TCHAR* Label; };
struct FLootTemplate { int32 WCID, Variant; };
struct FLootCohort { int32 Variant, Kind, Skill, Wield; Range Bounds[9]; };
struct FLootSpell { int32 Id, Type, Key; double Amount; int32 Family, Rank; double Legendary; };
struct FLootArmorTemplate { int32 WCID,Family;double Elements[4];int32 Mutates; };
struct FLootArmorCohort { int32 Family,Kind,Skill,Wield,Level;Range Bounds; };
#include "ACELootReference.inl"
const FLootSpell* Spell(int32 Id)
{
    int32 Lo = 0, Hi = UE_ARRAY_COUNT(LootSpells);
    while (Lo < Hi) { const int32 Mid = (Lo + Hi) / 2; if (LootSpells[Mid].Id < Id) Lo = Mid + 1; else Hi = Mid; }
    return Lo < UE_ARRAY_COUNT(LootSpells) && LootSpells[Lo].Id == Id ? &LootSpells[Lo] : nullptr;
}
double F(const FACEAppraisalInfo& A, uint32 Key, double Default = 0)
{ const auto* V = A.FloatProperties.Find(Key); return V && FMath::IsFinite(*V) ? *V : Default; }
int32 SkillValue(const FACEPlayerVitals& C, int32 Id, bool Base = false)
{ for (const auto& S : C.Skills) if (S.SkillId == Id) return Base ? S.Base : S.Current; return 0; }
bool Trained(const FACEPlayerVitals& C, int32 Id)
{ for (const auto& S : C.Skills) if (S.SkillId == Id) return S.AdvancementClass >= 2; return false; }
FString SkillName(int32 Id)
{
    switch (Id) { case 44:return TEXT("Heavy Weapons"); case 45:return TEXT("Light Weapons");
    case 46:return TEXT("Finesse Weapons");case 41:return TEXT("Two-Handed");case 47:return TEXT("Missile Weapons");
    case 34:return TEXT("War Magic");case 43:return TEXT("Void Magic");case 6:return TEXT("Melee Defense");
    case 7:return TEXT("Missile Defense");case 15:return TEXT("Magic Defense");case 14:return TEXT("Arcane Lore");
    case 16:return TEXT("Mana Conversion");default:return FString::Printf(TEXT("Skill %d"),Id); }
}
const FLootCohort* Cohort(const FACEWorldObject& Item, const FACEAppraisalInfo& A)
{
    for (const auto& T : LootTemplates)
    {
        if (T.WCID != Item.WeenieClassId) continue;
        for (const auto& C : LootCohorts)
        {
            if (C.Variant != T.Variant) continue;
            const auto& V = LootVariants[T.Variant];
            const int32 WeaponSkill = A.WeaponSkill ? A.WeaponSkill : A.IntProperties.FindRef(48);
            if (V.Family == 1 && WeaponSkill && V.Skill != WeaponSkill) continue;
            // The primary generation gate is not the later Legendary level gate.
            if (!C.Kind)
            {
                if(!A.IntProperties.FindRef(158)&&!A.IntProperties.FindRef(270)
                    &&!A.IntProperties.FindRef(273)&&!A.IntProperties.FindRef(276))return &C;
                continue;
            }
            for (uint32 K : {158u,270u,273u,276u})
                if (A.IntProperties.FindRef(K) == C.Kind && A.IntProperties.FindRef(K+2) == C.Wield
                    && (C.Kind != 2 || A.IntProperties.FindRef(K+1) == C.Skill)) return &C;
        }
    }
    return nullptr;
}
const FLootArmorTemplate* ArmorTemplate(int32 WCID)
{for(const auto& T:LootArmorTemplates)if(T.WCID==WCID)return &T;return nullptr;}
const FLootArmorCohort* ArmorCohort(const FLootArmorTemplate& T,const FACEAppraisalInfo& A)
{
    int32 Kind=A.IntProperties.FindRef(158),Skill=A.IntProperties.FindRef(159),Wield=A.IntProperties.FindRef(160),Level=0;
    for(uint32 K:{270u,273u,276u})if(A.IntProperties.FindRef(K)==7)Level=FMath::Max(Level,A.IntProperties.FindRef(K+2));
    if(Kind!=2)Skill=0;
    for(const auto& G:LootArmorCohorts)
        if(G.Family==T.Family&&G.Kind==Kind&&G.Skill==Skill&&G.Wield==Wield&&G.Level==Level)return &G;
    return nullptr;
}
// Collapse active spell categories before reversing their reflected appraisal
// effects. Inherent spells without the high-bit marker are NOT active buffs.
TArray<const FLootSpell*> ActiveItemEffects(const FACEAppraisalInfo& A, UACEDatSubsystem* Dat, bool& Known)
{
    TMap<uint32, TPair<uint32,const FLootSpell*>> Groups;
    for (int32 Entry : A.SpellBookEntries)
    {
        if (!(uint32(Entry) & 0x80000000u)) continue;
        const int32 Id = uint32(Entry) & 0x7fffffffu;
        const auto* S = Spell(Id);
        uint32 School=0,Power=0,Category=0,Flags=0;double Duration=0;
        if (!S || !Dat || !Dat->TryGetPluginSpellInfo(Id,School,Power,Category,Flags,Duration)) { Known=false;continue; }
        auto* Old = Groups.Find(Category);
        if (!Old || Power > Old->Key) Groups.Add(Category,{Power,S});
    }
    TArray<const FLootSpell*> Result;
    for (const auto& Pair : Groups) Result.Add(Pair.Value.Value);
    return Result;
}
TMap<FString,double> PermanentStats(const FACEAppraisalInfo& A, UACEDatSubsystem* Dat, bool& Known)
{
    Known = A.bSuccess;
    TMap<FString,double> Values;
    if (A.bHasWeaponProfile)
    {
        Values.Add(TEXT("Damage"), A.IntProperties.Contains(44) ? A.IntProperties.FindRef(44) : A.Damage);
        Values.Add(TEXT("Variance"), F(A,22,A.DamageVariance));
        Values.Add(TEXT("Attack"), F(A,62,A.WeaponOffense));
        Values.Add(TEXT("Multiplier"), F(A,63,A.WeaponDamageMod));
    }
    if (A.FloatProperties.Contains(29)) Values.Add(TEXT("Defense"),F(A,29));
    if (A.FloatProperties.Contains(152)) Values.Add(TEXT("Element"),F(A,152));
    if (A.IntProperties.Contains(204)) Values.Add(TEXT("ElementBonus"),A.IntProperties.FindRef(204));
    if (A.IntProperties.Contains(28)) Values.Add(TEXT("Armor"),A.IntProperties.FindRef(28));
    const int32 ResistanceIndices[]={3,4,5,7};
    for(int32 I=0;I<4;++I)if(A.ArmorResistances.IsValidIndex(ResistanceIndices[I]))
        Values.Add(FString::Printf(TEXT("Armor%d"),16+I),A.ArmorResistances[ResistanceIndices[I]]);
    if (A.FloatProperties.Contains(144)) Values.Add(TEXT("ManaConversion"),F(A,144));
    const auto Appraised=Values;
    const auto Effects = ActiveItemEffects(A,Dat,Known);
    for (const auto* S : Effects)
    {
        const int32 Type = S->Type & 0xff;
        const bool Add = (S->Type & 0x8000) != 0, Multiply = (S->Type & 0x4000) != 0;
        FString Key;
        bool AlreadyRaw=false;
        if (Type==4 && (S->Key==44 || S->Key==360)) { Key=TEXT("Damage");AlreadyRaw=A.IntProperties.Contains(44); }
        if (Type==8)
        {
            switch (S->Key)
            {
            case 22:Key=TEXT("Variance");AlreadyRaw=A.FloatProperties.Contains(22);break;
            case 62:case 168:Key=TEXT("Attack");AlreadyRaw=A.FloatProperties.Contains(62);break;
            case 29:case 169:Key=TEXT("Defense");break;
            case 63:Key=TEXT("Multiplier");AlreadyRaw=A.FloatProperties.Contains(63);break;
            case 152:case 170:Key=TEXT("Element");break;
            case 144:case 171:Key=TEXT("ManaConversion");break;
            case 16:case 17:case 18:case 19:Key=FString::Printf(TEXT("Armor%d"),S->Key);break;
            }
        }
        if (Type==4 && S->Key==28) Key=TEXT("Armor");
        if (Key.IsEmpty() || AlreadyRaw) continue;
        if (double* Value=Values.Find(Key))
        {
            if (Add) *Value-=S->Amount;
            else if (Multiply && FMath::Abs(S->Amount)>1.e-8) *Value/=S->Amount;
            else Known=false;
        }
    }
    // Old/custom servers may reflect an aura without including its spellbook
    // marker. Never manufacture an original roll from an unexplained highlight.
    if ((A.WeaponEnchantments & 0xffffu) && Effects.IsEmpty()) Known=false;
    for (const auto& P : Values) if (!FMath::IsFinite(P.Value)) Known=false;
    return Known?Values:Appraised;
}
FString EffectLabel(const FLootSpell& S, const FACEPlayerVitals& C)
{
    const int32 Type=S.Type&0xff;
    FString Label;
    if(Type==16)
    {
        Label=SkillName(S.Key);
        for(const auto& Skill:C.Skills)if(Skill.SkillId==S.Key&&!Skill.Name.IsEmpty())Label=Skill.Name;
    }
    else if(Type==1)
    { const TCHAR* Names[]={TEXT(""),TEXT("Strength"),TEXT("Endurance"),TEXT("Quickness"),TEXT("Coordination"),TEXT("Focus"),TEXT("Willpower")};if(S.Key>=1&&S.Key<=6)Label=Names[S.Key]; }
    else if(Type==4 && S.Key==360)Label=TEXT("weapon damage");
    else if(Type==8 && S.Key==168)Label=TEXT("melee attack");
    else if(Type==8 && S.Key==169)Label=TEXT("weapon melee defense");
    else if(Type==8 && S.Key==170)Label=TEXT("matching spell damage");
    else if(Type==8 && S.Key==171)Label=TEXT("item mana-conversion bonus");
    else if(S.Type&0x80)Label=TEXT("character armor");
    else if(Type==8&&S.Key>=64&&S.Key<=70)Label=TEXT("incoming elemental damage");
    if(Label.IsEmpty())return TEXT("Situational utility");
    if(S.Type&0x4000)return FString::Printf(TEXT("×%.2f %s"),S.Amount,*Label);
    const bool Percent=Type==8&&(S.Key==168||S.Key==169||S.Key==170);
    return FString::Printf(TEXT("+%s %s"),*ACELootQuality::FormatStat(S.Amount,Percent),*Label);
}
Role Relevance(const FLootSpell& S, int32 Skill, int32 Family, bool Atlatl,bool ElementalCaster,
    const FACEPlayerVitals& C, const TMap<FString,double>& Stats)
{
    const int32 Type=S.Type&0xff;
    const bool Caster=Family==3||Family==4;
    if(Type==4&&S.Key==360)return !Caster?Role::Damage:Role::Irrelevant;
    if(Type==8&&S.Key==170)return Caster&&ElementalCaster?Role::Damage:Role::Irrelevant;
    if(Type==8&&S.Key==168)return Family==1?Role::Attack:Role::Irrelevant;
    if(Type==8&&S.Key==169)return Role::Defender;
    if(Type==16)
    {
        if(S.Key==Skill&&Skill)return Role::WeaponSkill;
        if(S.Key==6)return Role::MeleeDefense;
        if(S.Key==15)return Role::MagicDefense;
        if(S.Key==7)return Role::MissileDefense;
        if((S.Key==16&&Caster&&Trained(C,16))||(S.Key==54&&Trained(C,54))||(S.Key==21&&Trained(C,21))
            ||((S.Key==49||S.Key==50||S.Key==51||S.Key==52)&&Family==1&&Trained(C,S.Key)))return Role::SupportSkill;
        return Role::Irrelevant;
    }
    if(Type==1)
    {
        if(Caster)return S.Key==5||S.Key==6?Role::PrimaryAttribute:S.Key==2||S.Key==3||S.Key==4?Role::SurvivalAttribute:Role::Irrelevant;
        const int32 Primary=Skill==46||(Skill==47&&!Atlatl)?4:1;
        if(S.Key==Primary)return Role::PrimaryAttribute;
        if(S.Key==4||(S.Key==3&&Skill==46))return Role::SecondaryAttribute;
        return S.Key==2||S.Key==3?Role::SurvivalAttribute:Role::Irrelevant;
    }
    if(Type==8&&S.Key==171)return Caster&&Stats.FindRef(TEXT("ManaConversion"))>0?Role::ManaEfficiency:Role::Irrelevant;
    if(Type==8&&S.Key>=64&&S.Key<=70)return Role::Ward;
    if((Type==4&&S.Key==28)||(S.Type&0x80))return Role::Armor;
    return Role::Irrelevant;
}
}

FLinearColor ACELootQuality::Color(ACELootScoring::Grade Grade)
{
    const FColor Colors[]={FColor(157,157,157),FColor(255,255,255),FColor(30,255,0),FColor(0,112,221),FColor(163,53,238),FColor(255,128,0)};
    return FLinearColor::FromSRGBColor(Colors[static_cast<int32>(Grade)]);
}
FString ACELootQuality::Name(ACELootScoring::Grade Grade) { return UTF8_TO_TCHAR(ACELootScoring::Name(Grade)); }
FString ACELootQuality::FormatStat(double V,bool Percent)
{
    const double Display=V*(Percent?100:1);
    FString Value=FMath::IsNearlyEqual(Display,FMath::RoundToDouble(Display),.0001)?FString::Printf(TEXT("%.0f"),Display):
        Percent||FMath::IsNearlyEqual(Display*10,FMath::RoundToDouble(Display*10),.0001)?FString::Printf(TEXT("%.1f"),Display):FString::Printf(TEXT("%.2f"),Display);
    return Percent?Value+TEXT("%"):Value;
}

#if WITH_DEV_AUTOMATION_TESTS
int32 ACELootQuality::ValidateWeaponBrackets(UACEDatSubsystem* Dat,TArray<FString>& Errors)
{
    int32 Count=0;FACEPlayerVitals Character;Character.bValid=true;Character.Level=275;
    for(int32 Id:{44,45,46,41,47,34,43,14})
    {auto& S=Character.Skills.AddDefaulted_GetRef();S.SkillId=Id;S.Base=S.Current=1000;S.AdvancementClass=3;}
    for(const auto& G:LootCohorts)
    {
        const auto& V=LootVariants[G.Variant];
        if(V.Family==4||(V.Family==3&&!G.Bounds[6].Variable()))continue;
        FACEWorldObject Item;for(const auto& T:LootTemplates)if(T.Variant==G.Variant){Item.WeenieClassId=T.WCID;break;}
        Item.Guid=1;Item.ItemType=V.Family==1?ACEItemType::MeleeWeapon:V.Family==2?ACEItemType::MissileWeapon:ACEItemType::Caster;
        FACEAppraisalInfo A;A.bSuccess=true;A.bHasWeaponProfile=V.Family!=3;A.WeaponSkill=V.Skill;
        A.IntProperties={{158,G.Kind},{159,G.Skill},{160,G.Wield},{171,0}};
        for(int32 End=0;End<2;++End)
        {
            auto Value=[&](int32 I){return End?G.Bounds[I].High:G.Bounds[I].Low;};
            A.Damage=Value(0);A.DamageVariance=End?G.Bounds[1].Low:G.Bounds[1].High;A.WeaponOffense=Value(2);A.WeaponDamageMod=Value(4);
            A.FloatProperties.Add(29,Value(3));
            if(G.Bounds[6].Low>=0)A.FloatProperties.Add(152,Value(6));
            if(G.Bounds[5].Low>=0)A.IntProperties.Add(204,Value(5));
            const auto R=Evaluate(Item,A,Character,{},Dat);
            if(!R.bRated||!FMath::IsFinite(R.Points)||!FMath::IsNearlyEqual(R.OriginalPoints,End?100.:0.,.002))
                Errors.Add(FString::Printf(TEXT("%s wield %d kind %d endpoint %d: %.4f (%s)"),V.Label,G.Wield,G.Kind,End,R.OriginalPoints,*R.Reason));
            ++Count;
        }
    }
    return Count;
}
#endif

bool ACELootQuality::CaptureOriginal(const FACEWorldObject& Item,const FACEAppraisalInfo& A,
    const TArray<FACEActiveEnchantment>&,UACEDatSubsystem* Dat,FACEQualityOriginal& Out)
{
    if(!A.bSuccess||A.IntProperties.FindRef(171)!=0)return false;
    bool Known=false;auto Stats=PermanentStats(A,Dat,Known);if(!Known||Stats.IsEmpty())return false;
    Out.WCID=Item.WeenieClassId;Out.Setup=Item.SetupId;Out.Icon=Item.IconId;Out.Name=Item.Name;Out.Stats=MoveTemp(Stats);return true;
}

FACEQualityReview ACELootQuality::Evaluate(const FACEWorldObject& Item,const FACEAppraisalInfo& A,
    const FACEPlayerVitals& Character,const TArray<FACEActiveEnchantment>& Active,
    UACEDatSubsystem* Dat,const FACEQualityOriginal* Original)
{
    FACEQualityReview R;
    const bool Weapon=(Item.ItemType&(ACEItemType::MeleeWeapon|ACEItemType::MissileWeapon|ACEItemType::Caster))!=0;
    const bool Wearable=!Weapon&&(Item.ValidLocations!=0)&&!(Item.ItemType&ACEItemType::Container);
    R.bEquipment=Weapon||Wearable;if(!R.bEquipment)return R;
    R.Class=Weapon?TEXT("Weapon"):TEXT("Wearable");R.Reason=TEXT("Identify to reveal quality");
    if(!A.bSuccess)return R;
    R.Tinkers=A.IntProperties.FindRef(171);
    bool RawKnown=false;const auto Permanent=PermanentStats(A,Dat,RawKnown);
    const bool Match=Original&&Original->WCID==Item.WeenieClassId&&Original->Setup==Item.SetupId
        &&Original->Icon==Item.IconId&&Original->Name==Item.Name;
    R.bOriginalKnown=Match||(RawKnown&&R.Tinkers==0);
    const auto& Base=Match?Original->Stats:Permanent;
    const auto* Group=Cohort(Item,A);const auto* Variant=Group?&LootVariants[Group->Variant]:nullptr;
    int32 Family=Variant?Variant->Family:(Item.ItemType&ACEItemType::Caster)?3:(Item.ItemType&ACEItemType::MissileWeapon)?2:1;
    int32 RelevantSkill=Variant?Variant->Skill:A.WeaponSkill;
    if(!Weapon)
    {
        int32 Best=0;
        for(int32 Id:{44,45,46,41,47,34,43})if(Trained(Character,Id)&&SkillValue(Character,Id)>Best)
        { Best=SkillValue(Character,Id);RelevantSkill=Id; }
        Family=RelevantSkill==34||RelevantSkill==43?3:RelevantSkill==47?2:1;
    }
    R.Class=Variant?Variant->Label:Wearable?TEXT("Wearable / combat spell package"):TEXT("Uncatalogued weapon");
    R.ClassMark=RelevantSkill==44?TEXT("H"):RelevantSkill==45?TEXT("L"):RelevantSkill==46?TEXT("F"):RelevantSkill==41?TEXT("2H"):TEXT("");
    if(!Weapon)R.ClassMark.Empty();
    switch(A.DamageType?A.DamageType:A.IntProperties.FindRef(45))
    {case 1:R.DamageType=TEXT("Slashing");break;case 2:R.DamageType=TEXT("Piercing");break;case 4:R.DamageType=TEXT("Bludgeoning");break;
    case 8:R.DamageType=TEXT("Cold");break;case 16:R.DamageType=TEXT("Fire");break;case 32:R.DamageType=TEXT("Acid");break;
    case 64:R.DamageType=TEXT("Electric");break;case 1024:R.DamageType=TEXT("Nether");break;default:R.DamageType=TEXT("Mixed / other");}
    if(!Weapon)R.DamageType.Empty();
    bool ServerCheck=false;
    const FString Failure=ACEPluginEquipment::WieldFailure(A,Character,&ServerCheck);
    R.bCannotWield=!Failure.IsEmpty();R.Requirements=Failure;
    if(R.Requirements.IsEmpty())R.Requirements=ServerCheck?TEXT("Wield requirements: server check needed"):TEXT("Wield requirements met");
    for(uint32 K:{158u,270u,273u,276u})
        if(A.IntProperties.FindRef(K)==2)R.Requirements+=FString::Printf(TEXT("\n%s %d / %d base"),*SkillName(A.IntProperties.FindRef(K+1)),
            SkillValue(Character,A.IntProperties.FindRef(K+1),true),A.IntProperties.FindRef(K+2));
    const int32 Lore=A.IntProperties.FindRef(109),HaveLore=SkillValue(Character,14);
    R.Activation=Lore>0?FString::Printf(TEXT("Arcane Lore %d / %d"),HaveLore,Lore):TEXT("No Arcane Lore requirement");
    R.bActivationBlocked=HaveLore<Lore;
    if(A.IntProperties.FindRef(115)>0)
    {
        const int32 Skill=A.IntProperties.FindRef(176),Required=A.IntProperties.FindRef(115);
        R.bActivationBlocked|=Skill==0||SkillValue(Character,Skill)<Required;
        R.Activation+=FString::Printf(TEXT(" · %s %d / %d"),*SkillName(Skill),SkillValue(Character,Skill),Required);
    }
    if(A.IntProperties.Contains(107)&&A.IntProperties.FindRef(107)<=0)
    { R.bActivationBlocked=true;R.Activation+=TEXT(" · Out of item mana"); }
    if(A.IntProperties.FindRef(110)>0||A.BoolProperties.FindRef(94))
    { R.bActivationBlocked=true;R.Activation+=TEXT(" · Restricted activation: server check needed"); }
    TMap<int32,const FLootSpell*> BestFamilies;
    const auto& Book=A.SpellBookEntries.IsEmpty()?A.SpellIds:A.SpellBookEntries;
    for(int32 Entry:Book)
    {
        if(uint32(Entry)&0x80000000u)continue;
        const auto* S=Spell(Entry);if(!S||!S->Family)continue;
        const auto* Old=BestFamilies.Find(S->Family);
        if(!Old||(*Old)->Rank<S->Rank)BestFamilies.Add(S->Family,S);
    }
    TMap<FString,double> NativeBonuses;
    TMap<FString,double> NativeMultipliers;
    TSet<FString> ActiveKeys;
    TArray<double> WearableBenefits;
    for(const auto& P:BestFamilies)
    {
        const auto& S=*P.Value;FACEQualitySpell Q;Q.SpellId=S.Id;Q.Family=S.Family;Q.Rank=S.Rank;
        uint32 Icon=0;if(!Dat||!Dat->TryGetSpellInfo(S.Id,Q.Name,Icon))Q.Name=FString::Printf(TEXT("Cantrip %d"),S.Id);
        Q.Effect=EffectLabel(S,Character);
        const auto EffectRole=Relevance(S,RelevantSkill,Family,Item.AmmoType==4,(A.DamageType|A.IntProperties.FindRef(45))!=0,Character,Permanent);
        const bool Multiply=(S.Type&0x4000)!=0;
        Q.Points=CantripPoints(EffectRole,Multiply?S.Amount-1:S.Amount,Multiply?S.Legendary-1:S.Legendary,Family==3||Family==4);
        Q.Status=R.bCannotWield||R.bActivationBlocked?TEXT("Potential · requirements unmet"):TEXT("Potential when equipped");
        for(const auto& E:Active)if(E.SpellId==S.Id&&E.CasterGuid==Item.Guid)
        { Q.bActive=true;Q.Status=TEXT("Active");break; }
        if(!Q.bActive&&A.SpellBookEntries.Contains(int32(0x80000000u|uint32(S.Id))))
            Q.Status=TEXT("Active at last appraisal");
        if(!Q.bActive)for(const auto& E:Active)
        {
            const auto* Other=Spell(E.SpellId);
            if(Other&&Other->Family==S.Family&&Other->Rank>=S.Rank)
            { Q.Status=TEXT("Already covered by current gear");break; }
        }
        if(Q.Points<=0)Q.Status=TEXT("Utility · no combat score");
        R.CantripPoints+=Q.Points;
        if(Q.Points>0)
        {
            const double Rank[]={0,.25,.55,.94,1};
            const double RelevanceFactor=EffectRole==Role::PrimaryAttribute||EffectRole==Role::WeaponSkill
                ||EffectRole==Role::MeleeDefense||EffectRole==Role::MagicDefense||EffectRole==Role::Defender?1:
                EffectRole==Role::SecondaryAttribute?.8:.45;
            Q.WearableBenefit=Rank[FMath::Clamp(S.Rank,0,4)]*RelevanceFactor;
            WearableBenefits.Add(Q.WearableBenefit);
        }
        if((S.Type&0xff)==4&&S.Key==360&&!((Family==3)||(Family==4)))NativeBonuses.Add(TEXT("Damage"),S.Amount);
        if((S.Type&0xff)==8&&S.Key==168&&Family==1)NativeBonuses.Add(TEXT("Attack"),S.Amount);
        if((S.Type&0xff)==8&&S.Key==169)NativeBonuses.Add(TEXT("Defense"),S.Amount);
        if((S.Type&0xff)==8&&S.Key==170&&(Family==3||Family==4))NativeBonuses.Add(TEXT("Element"),S.Amount);
        if((S.Type&0xff)==4&&S.Key==28)NativeBonuses.Add(TEXT("Armor"),S.Amount);
        if((S.Type&0xff)==8&&S.Key>=16&&S.Key<=19)NativeBonuses.Add(FString::Printf(TEXT("Armor%d"),S.Key),S.Amount);
        if((S.Type&0xff)==8&&S.Key==171&&Multiply)NativeMultipliers.Add(TEXT("ManaConversion"),S.Amount);
        if(Q.bActive)
        {
            if((S.Type&0xff)==4&&S.Key==360)ActiveKeys.Add(TEXT("Damage"));
            if((S.Type&0xff)==8&&S.Key==168)ActiveKeys.Add(TEXT("Attack"));
            if((S.Type&0xff)==8&&S.Key==169)ActiveKeys.Add(TEXT("Defense"));
            if((S.Type&0xff)==8&&S.Key==170)ActiveKeys.Add(TEXT("Element"));
            if((S.Type&0xff)==8&&S.Key==171)ActiveKeys.Add(TEXT("ManaConversion"));
        }
        R.Spells.Add(MoveTemp(Q));
    }
    R.Spells.Sort([](const auto& X,const auto& Y){return X.Points==Y.Points?X.SpellId<Y.SpellId:X.Points>Y.Points;});
    auto Stat=[&](const TCHAR* Key,const TCHAR* Label,Range Bounds,bool Percent=false,bool Lower=false)
    {
        if(!Permanent.Contains(Key))return;
        FACEQualityStat S;S.Label=Label;S.Bounds=Bounds;S.Original=Base.FindRef(Key);S.Permanent=Permanent.FindRef(Key);
        const auto* Multiplier=NativeMultipliers.Find(Key);
        S.WithCantrip=RawKnown?S.Permanent*(Multiplier?*Multiplier:1)+NativeBonuses.FindRef(Key):S.Permanent;
        S.bOriginalKnown=R.bOriginalKnown;S.bPermanentKnown=RawKnown;S.bPercent=Percent;S.bLowerBetter=Lower;
        S.bPotential=!ActiveKeys.Contains(Key);R.Stats.Add(MoveTemp(S));
    };
    bool Valid=Group&&R.bOriginalKnown&&RawKnown;
    if(Group)
    {
        const auto& B=Group->Bounds;
        if(Variant->Family==1)
        {
            Stat(TEXT("Damage"),TEXT("Maximum damage"),B[0]);Stat(TEXT("Variance"),TEXT("Damage variance"),B[1],true,true);
            Stat(TEXT("Attack"),TEXT("Melee attack bonus"),{B[2].Low-1,B[2].High-1},true);
            Stat(TEXT("Defense"),TEXT("Weapon melee-defense bonus"),{B[3].Low-1,B[3].High-1},true);
            for(int32 I=2;I<R.Stats.Num();++I){R.Stats[I].Original-=1;R.Stats[I].Permanent-=1;R.Stats[I].WithCantrip-=1;}
            for(const auto* Key:{TEXT("Damage"),TEXT("Variance"),TEXT("Attack"),TEXT("Defense")})Valid&=Base.Contains(Key);
            const double Damage=Base.FindRef(TEXT("Damage")),Variance=Base.FindRef(TEXT("Variance"));
            Valid&=B[0].Contains(Damage)&&B[1].Contains(Variance)&&B[2].Contains(Base.FindRef(TEXT("Attack")))&&B[3].Contains(Base.FindRef(TEXT("Defense")));
            R.Components=Melee(MeanHit(Damage,Variance),Base.FindRef(TEXT("Attack")),Base.FindRef(TEXT("Defense")),B[7],B[2],B[3]);
            R.OriginalPoints=R.Components.Total();
        }
        else if(Variant->Family==2)
        {
            Stat(TEXT("Multiplier"),TEXT("Launcher damage multiplier"),B[4]);Stat(TEXT("ElementBonus"),TEXT("Elemental damage bonus"),B[5]);
            Stat(TEXT("Defense"),TEXT("Weapon melee-defense bonus"),{B[3].Low-1,B[3].High-1},true);
            if(!R.Stats.IsEmpty()&&R.Stats.Last().Label.Contains(TEXT("defense")))
            {auto& S=R.Stats.Last();S.Original-=1;S.Permanent-=1;S.WithCantrip-=1;}
            const double W1=B[4].Variable()?60:0,W2=B[5].Variable()?25:0,WD=B[3].Variable()?15:0;
            const double Sum=W1+W2+WD;
            Valid&=Sum>0&&Base.Contains(TEXT("Defense"));
            for(const auto& P:TArray<TPair<FString,int32>>{{TEXT("Multiplier"),4},{TEXT("ElementBonus"),5},{TEXT("Defense"),3}})
                if(B[P.Value].Low>=0)Valid&=Base.Contains(P.Key)&&B[P.Value].Contains(Base.FindRef(P.Key));
            if(Sum>0)R.OriginalPoints=100*(W1*Position(Base.FindRef(TEXT("Multiplier")),B[4])+W2*Position(Base.FindRef(TEXT("ElementBonus")),B[5])+WD*Position(Base.FindRef(TEXT("Defense")),B[3]))/Sum;
            Stat(TEXT("Damage"),TEXT("Flat damage bonus per missile"),{0,0});
        }
        else if(Variant->Family==3)
        {
            Stat(TEXT("Element"),TEXT("Matching spell-damage bonus"),{B[6].Low-1,B[6].High-1},true);
            Stat(TEXT("Defense"),TEXT("Weapon melee-defense bonus"),{B[3].Low-1,B[3].High-1},true);
            for(auto& S:R.Stats){S.Original-=1;S.Permanent-=1;S.WithCantrip-=1;}
            Valid&=Base.Contains(TEXT("Element"))&&Base.Contains(TEXT("Defense"))&&B[6].Contains(Base.FindRef(TEXT("Element")))&&B[3].Contains(Base.FindRef(TEXT("Defense")))&&B[6].Variable();
            R.OriginalPoints=85*Position(Base.FindRef(TEXT("Element")),B[6])+15*Position(Base.FindRef(TEXT("Defense")),B[3]);
        }
        else Valid=false;
        Stat(TEXT("ManaConversion"),TEXT("Item mana-conversion bonus"),B[8],true);
    }
    if(!Weapon)
    {
        // Wearables are evaluated for their spell package, explicitly labelled.
        // An unverified AL/set/rating baseline must not be invented from item level.
        WearableBenefits.Sort(TGreater<double>());
        const double Weights[]={1,.20,.10,.05};double Package=0;
        for(int32 I=0;I<WearableBenefits.Num();++I)Package+=WearableBenefits[I]*(I<4?Weights[I]:.05*FMath::Pow(.5,I-3));
        const auto* Template=ArmorTemplate(Item.WeenieClassId);
        const bool HasProtection=(Item.ItemType&ACEItemType::Armor)||Template||Permanent.FindRef(TEXT("Armor"))>0;
        const double SpellScale=HasProtection?80:100;
        R.OriginalPoints=0;R.CantripPoints=SpellScale*Package;Valid=!R.Spells.IsEmpty();
        R.Reason=TEXT("Combat spell quality · protection baseline unverified");
        const auto* ArmorGroup=Template?ArmorCohort(*Template,A):nullptr;
        bool ArmorKnown=ArmorGroup&&RawKnown&&R.bOriginalKnown&&Base.Contains(TEXT("Armor"))
            &&ArmorGroup->Bounds.Contains(Base.FindRef(TEXT("Armor")));
        const int32 ElementBits[]={2,8,1,4};
        if(ArmorKnown)for(int32 I=0;I<4;++I)
        {
            if(!(Template->Mutates&ElementBits[I]))continue;
            const FString Key=FString::Printf(TEXT("Armor%d"),16+I);
            const Range Bounds{Template->Elements[I],FMath::Min(2.,Template->Elements[I]+.9)};
            if(!Base.Contains(Key)||!Bounds.Contains(Base.FindRef(Key)))ArmorKnown=false;
        }
        if(Permanent.FindRef(TEXT("Armor"))>0)
            Stat(TEXT("Armor"),TEXT("Armor level"),ArmorGroup?ArmorGroup->Bounds:Range{});
        if(ArmorKnown)
        {
            double Protection=0;int32 Count=0;
            const int32 Bits[]={2,8,1,4};const TCHAR* Labels[]={TEXT("Cold armor multiplier"),TEXT("Fire armor multiplier"),TEXT("Acid armor multiplier"),TEXT("Electric armor multiplier")};
            for(int32 I=0;I<4;++I)
            {
                const FString Key=FString::Printf(TEXT("Armor%d"),16+I);
                const double Start=Template->Elements[I];const Range Bounds{Start,FMath::Min(2.,Start+.9)};
                const bool Variable=(Template->Mutates&Bits[I])&&Bounds.Variable();
                if(Variable&&Base.Contains(Key)&&Bounds.Contains(Base.FindRef(Key)))
                {Protection+=Position(Base.FindRef(Key),Bounds);++Count;}
                Stat(*Key,Labels[I],Variable?Bounds:Range{Start,Start});
            }
            const double AL=Position(Base.FindRef(TEXT("Armor")),ArmorGroup->Bounds);
            R.OriginalPoints=Count?12*AL+8*Protection/Count:20*AL;
            R.CantripPoints=80*Package;Valid=true;
            R.Class=(Item.ValidLocations&0x200000)?TEXT("Shield"):TEXT("Armor");
            R.Reason=TEXT("20% original protection + 80% useful spell package · ACE baseline");
        }
        else if(HasProtection)
        {R.Class=TEXT("Armor / combat spell package");R.Reason=TEXT("Spell-only lower bound · original protection unverified");}
        else R.Class=(Item.ItemType&ACEItemType::Jewelry)?TEXT("Jewelry / combat spell package"):TEXT("Clothing / combat spell package");
        R.Spells.Sort([](const auto& X,const auto& Y){return X.WearableBenefit==Y.WearableBenefit?X.SpellId<Y.SpellId:X.WearableBenefit>Y.WearableBenefit;});
        for(int32 I=0;I<R.Spells.Num();++I)
            R.Spells[I].Points=SpellScale*R.Spells[I].WearableBenefit*(I<4?Weights[I]:.05*FMath::Pow(.5,I-3));
        // Native gear ratings are separate from spell skills and cannot be
        // manufactured from level or the item's display rarity.
        for(uint32 Key:{370u,371u,372u,373u,374u,375u,376u,377u,378u,379u})
        {
            const int32 Value=A.IntProperties.FindRef(Key);if(Value<=0)continue;
            R.PropertyPoints+=2*Value;
            const TCHAR* Labels[]={TEXT("Damage rating"),TEXT("Damage resistance rating"),TEXT("Critical chance rating"),TEXT("Critical resistance rating"),TEXT("Critical damage rating"),TEXT("Critical damage resistance rating"),TEXT("Healing boost rating"),TEXT("Nether resistance rating"),TEXT("Life resistance rating"),TEXT("Maximum health rating")};
            FACEQualitySpell Q;Q.Name=Labels[Key-370];Q.Effect=FString::Printf(TEXT("+%d native rating"),Value);
            Q.Status=TEXT("Permanent gear property");Q.Points=2*Value;R.Spells.Add(MoveTemp(Q));Valid=true;
        }
        const int32 Set=A.IntProperties.FindRef(265);
        FString SetName;bool UsefulSet=false;
        switch(Set)
        {
        case 13:SetName=TEXT("Soldier's");UsefulSet=Family==1;break;
        case 14:SetName=TEXT("Adept's");UsefulSet=Family==3||Family==4;break;
        case 15:SetName=TEXT("Archer's");UsefulSet=Family==2;break;
        case 16:SetName=TEXT("Defender's");UsefulSet=true;break;
        case 17:SetName=TEXT("Tinker's");break;case 18:SetName=TEXT("Crafter's");break;
        case 19:SetName=TEXT("Hearty");UsefulSet=Family==1&&RelevantSkill!=46;break;
        case 20:SetName=TEXT("Dexterous");UsefulSet=RelevantSkill==46||Family==2;break;
        case 21:SetName=TEXT("Wise");UsefulSet=Family==3||Family==4;break;
        case 22:SetName=TEXT("Swift");UsefulSet=true;break;
        default:if(Set>=23&&Set<=29){SetName=TEXT("Protection");UsefulSet=true;}break;
        }
        if(Set)
        {
            FACEQualitySpell Q;Q.Name=(SetName.IsEmpty()?TEXT("Equipment"):SetName)+FString(TEXT(" set"));
            Q.Effect=TEXT("Set effects depend on the pieces equipped");Q.Status=TEXT("Build potential; not an active cantrip");
            Q.Points=UsefulSet?4:0;R.PropertyPoints+=Q.Points;R.Spells.Add(MoveTemp(Q));
        }
    }
    else R.Reason=!Group?TEXT("No verified generation bracket for this item"):
        !R.bOriginalKnown?TEXT("Original roll unknown · identify before tinkering to preserve it"):
        !RawKnown?TEXT("Active effects could not be separated reliably"):
        !Valid?TEXT("Stats outside the verified ACE baseline"):TEXT("Original roll + relevant cantrips · ACE baseline");
    R.bRated=Valid;R.Points=Overall(R.OriginalPoints,R.CantripPoints)+R.PropertyPoints;R.Grade=Band(R.Points);
    return R;
}
