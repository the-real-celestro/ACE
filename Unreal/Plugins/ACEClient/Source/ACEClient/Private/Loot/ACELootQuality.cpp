#include "ACELootQuality.h"
#include "ACEDatSubsystem.h"
#include "Mods/ACEPluginEquipment.h"
#include "UI/ACEAppraisalFormatting.h"

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
struct FLootEssenceTemplate { int32 WCID,Pet,Level,SkillRequired,Specialized,UserLevel,Mastery,Damage; };
#include "ACEEssenceReference.inl"
const FLootEssenceTemplate* EssenceTemplate(int32 WCID)
{
    int32 Lo=0,Hi=UE_ARRAY_COUNT(LootEssences);
    while(Lo<Hi){const int32 Mid=(Lo+Hi)/2;if(LootEssences[Mid].WCID<WCID)Lo=Mid+1;else Hi=Mid;}
    return Lo<UE_ARRAY_COUNT(LootEssences)&&LootEssences[Lo].WCID==WCID?&LootEssences[Lo]:nullptr;
}
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
    case 33:return TEXT("Life Magic");case 34:return TEXT("War Magic");case 43:return TEXT("Void Magic");case 6:return TEXT("Melee Defense");
    case 7:return TEXT("Missile Defense");case 15:return TEXT("Magic Defense");case 14:return TEXT("Arcane Lore");
    case 16:return TEXT("Mana Conversion");case 54:return TEXT("Summoning");case 39:return TEXT("Cooking");
    default:return FString::Printf(TEXT("Skill %d"),Id); }
}
void Check(FACEQualityReview& R,const FString& Label,const FString& Have,bool Met,bool Known=true)
{R.Checks.Add({Label,Have,Met,Known});}
void WieldChecks(const FACEAppraisalInfo& A,const FACEPlayerVitals& C,FACEQualityReview& R)
{
    const TCHAR* Attributes[]={TEXT(""),TEXT("Strength"),TEXT("Endurance"),TEXT("Quickness"),TEXT("Coordination"),TEXT("Focus"),TEXT("Willpower")};
    auto Training=[](int32 V)->FString{return V>=3?TEXT("Specialized"):V==2?TEXT("Trained"):TEXT("Untrained");};
    for(uint32 Key:{158u,270u,273u,276u})
    {
        const int32 Kind=A.IntProperties.FindRef(Key),Id=A.IntProperties.FindRef(Key+1),Required=A.IntProperties.FindRef(Key+2);
        if(!Kind)continue;
        // Use the shared wield validator for the truth of each row, including
        // custom boolean/equality/Int64 gates, rather than duplicating its rules.
        auto Single=A;for(uint32 Other:{158u,270u,273u,276u})if(Other!=Key)Single.IntProperties.Remove(Other);
        bool Server=false;const FString Failure=ACEPluginEquipment::WieldFailure(Single,C,&Server);
        FString Label,Have;bool Known=!Server&&C.bValid;int64 Actual=0;
        const auto* Skill=C.Skills.FindByPredicate([&](const auto& S){return S.SkillId==Id;});
        switch(Kind)
        {
        case 1:case 2:Label=SkillName(Id);Actual=Skill?(Kind==1?Skill->Current:Skill->Base):0;break;
        case 8:Label=Training(Required)+TEXT(" ")+SkillName(Id);Have=Training(Skill?Skill->AdvancementClass:0);break;
        case 3:case 4:Label=Id>=1&&Id<=6?Attributes[Id]:TEXT("Attribute");Actual=Kind==3?C.GetAttributeCurrent(Id):C.GetAttributeBase(Id);break;
        case 5:case 6:Label=Id==1?TEXT("Health"):Id==3?TEXT("Stamina"):Id==5?TEXT("Mana"):TEXT("Vital");Actual=Kind==5?(Id==1?C.MaxHealth:Id==3?C.MaxStamina:Id==5?C.MaxMana:0):C.GetVitalBase(Id);break;
        case 7:Label=TEXT("Level");Actual=C.Level;break;
        case 9:Label=FString::Printf(TEXT("Property %d"),Id);Actual=C.StatQualityInts.FindRef(Id);break;
        case 10:Label=FString::Printf(TEXT("Property %d: %s"),Id,Required?TEXT("Yes"):TEXT("No"));Have=C.QualityBools.FindRef(Id)?TEXT("Yes"):TEXT("No");break;
        case 11:Label=TEXT("Creature type");Actual=C.StatQualityInts.FindRef(2);break;
        case 12:Label=TEXT("Heritage");Actual=C.HeritageGroup;break;
        case 13:Label=FString::Printf(TEXT("Counter %d"),Id);Actual=C.QualityInt64s.FindRef(Id);break;
        default:Label=FString::Printf(TEXT("Requirement %d"),Kind);Known=false;break;
        }
        if(Have.IsEmpty())
        {
            Label+=FString::Printf(TEXT(" %d"),Required);
            if(Kind==2||Kind==4||Kind==6)Label+=TEXT(" (base)");
            Have=FString::Printf(TEXT("%lld"),static_cast<long long>(Actual));
        }
        Check(R,Label,Known?Have:FString(TEXT("Server check")),Failure.IsEmpty()&&!Server,Known);
    }
}
FACEQualityReview EssenceReview(const FACEWorldObject& Item,const FACEAppraisalInfo& A,
    const FACEPlayerVitals& C,const FLootEssenceTemplate& T)
{
    FACEQualityReview R;R.bEquipment=true;R.Class=FString::Printf(TEXT("Summoning · level %d pet"),T.Level);
    R.Reason=TEXT("Identify to reveal essence ratings");if(!A.bSuccess)return R;
    R.bIdentified=true;R.bOriginalKnown=true;
    const TCHAR* Types[]={TEXT("Cold"),TEXT("Fire"),TEXT("Acid"),TEXT("Electric")};
    const int32 Type=T.Damage==8?0:T.Damage==16?1:T.Damage==32?2:T.Damage==64?3:-1;
    if(Type>=0)R.DamageType=Types[Type];
    // Assessment omission means no generated bonus, not an unknown positive
    // roll. Show all six dimensions so the combination's denominator is clear.
    int Ratings[6]={};
    const TCHAR* Labels[]={TEXT("Damage"),TEXT("Damage resist"),TEXT("Critical chance"),TEXT("Critical resist"),TEXT("Critical damage"),TEXT("Crit. damage resist")};
    for(int32 I=0;I<6;++I)
    {
        Ratings[I]=A.IntProperties.FindRef(370+I);
        const FString Key=FString::Printf(TEXT("Essence%d"),370+I);
        R.CurrentStats.Add({Key,Labels[I],FString::FromInt(Ratings[I])});
        auto& S=R.Stats.AddDefaulted_GetRef();S.Key=Key;S.Label=Labels[I];S.Bounds={0,20};
        S.Original=S.Permanent=S.WithCantrip=Ratings[I];S.bOriginalKnown=true;S.bPotential=false;
    }
    const auto Combo=Essence(Ratings);
    // Known random-loot identities establish the generation rule. If a server
    // changes a rating beyond that rule, retain the facts but withhold a grade.
    R.bRated=Combo.Valid&&(!A.IntProperties.Contains(266)||A.IntProperties.FindRef(266)==T.Pet);
    R.Points=R.OriginalPoints=Combo.Points();R.Grade=Band(R.Points);
    R.Reason=R.bRated?TEXT("Six-roll combination · ACE baseline"):TEXT("Outside verified essence generation");
    R.CurrentStats.Add({TEXT("EssenceTotal"),TEXT("Combined rating"),FString::Printf(TEXT("%lld / 120"),
        static_cast<long long>(int64(Ratings[0])+Ratings[1]+Ratings[2]+Ratings[3]+Ratings[4]+Ratings[5]))});
    const int32 MaxUses=A.IntProperties.Contains(92)?A.IntProperties.FindRef(92):Item.MaxStructure;
    const int32 Uses=A.IntProperties.Contains(91)?A.IntProperties.FindRef(91):Item.Structure;
    if(MaxUses>0)R.CurrentStats.Add({TEXT("Uses"),TEXT("Uses"),FString::Printf(TEXT("%d / %d"),Uses,MaxUses)});
    R.CurrentStats.Add({TEXT("Value"),TEXT("Value"),FText::AsNumber(A.IntProperties.Contains(19)?A.IntProperties.FindRef(19):Item.Value).ToString()});
    if(const auto* Work=A.IntProperties.Find(105))R.CurrentStats.Add({TEXT("Work"),TEXT("Workmanship"),FString::FromInt(*Work)});
    auto Gate=[&](int32 Key,int32 Fallback){return A.IntProperties.Contains(Key)?A.IntProperties.FindRef(Key):Fallback;};
    const int32 Skill=Gate(366,54),Required=Gate(367,T.SkillRequired),Specialized=Gate(368,T.Specialized),Level=Gate(369,T.UserLevel);
    const auto* Have=C.Skills.FindByPredicate([&](const auto& S){return S.SkillId==Skill;});
    const int32 Value=Have?Have->Current:0;
    R.Requirements=FString::Printf(TEXT("Summoning %d / %d · level %d / %d"),Value,Required,C.Level,Level);
    R.bCannotWield=!Have||Have->AdvancementClass<2||Value<Required||C.Level<Level;
    Check(R,FString::Printf(TEXT("%s %d (trained)"),*SkillName(Skill),Required),FString::FromInt(Value),Have&&Have->AdvancementClass>=2&&Value>=Required,C.bValid);
    Check(R,FString::Printf(TEXT("Level %d"),Level),FString::FromInt(C.Level),C.Level>=Level,C.bValid);
    if(Specialized)
    {
        const auto* Spec=C.Skills.FindByPredicate([&](const auto& S){return S.SkillId==Specialized;});
        R.bCannotWield|=!Spec||Spec->AdvancementClass<3;
        R.Requirements+=TEXT(" · specialized");
        Check(R,TEXT("Specialized ")+SkillName(Specialized),Spec&&Spec->AdvancementClass>=3?TEXT("Specialized"):TEXT("Not specialized"),Spec&&Spec->AdvancementClass>=3,C.bValid);
    }
    const int32 Mastery=Gate(362,T.Mastery),Lore=A.IntProperties.FindRef(109);
    const TCHAR* Masteries[]={TEXT(""),TEXT("Primalist"),TEXT("Necromancer"),TEXT("Naturalist")};
    if(Mastery>0&&Mastery<UE_ARRAY_COUNT(Masteries))
    {
        R.Activation=Masteries[Mastery];const int32 Current=C.StatQualityInts.FindRef(362);R.bActivationBlocked=Current!=Mastery;
        Check(R,Masteries[Mastery],Current>0&&Current<UE_ARRAY_COUNT(Masteries)?Masteries[Current]:TEXT("None"),Current==Mastery,C.bValid);
    }
    if(Lore>0)
    {
        if(!R.Activation.IsEmpty())R.Activation+=TEXT(" · ");
        R.Activation+=FString::Printf(TEXT("Arcane Lore %d / %d"),SkillValue(C,14),Lore);
        R.bActivationBlocked|=SkillValue(C,14)<Lore;
        Check(R,FString::Printf(TEXT("Arcane Lore %d"),Lore),FString::FromInt(SkillValue(C,14)),SkillValue(C,14)>=Lore,C.bValid);
    }
    if(MaxUses>0&&Uses<=0){R.bActivationBlocked=true;if(!R.Activation.IsEmpty())R.Activation+=TEXT(" · ");R.Activation+=TEXT("Empty");Check(R,TEXT("Charges > 0"),FString::FromInt(Uses),false);}
    return R;
}
const FLootCohort* Cohort(const FACEWorldObject& Item, const FACEAppraisalInfo& A)
{
    int32 WeaponSkill=A.WeaponSkill?A.WeaponSkill:A.IntProperties.FindRef(48);
    // The generation gate is authoritative when older appraisal profiles keep
    // a legacy Sword/Dagger skill alongside a modern Heavy/Light/Finesse gate.
    for(uint32 K:{158u,270u,273u,276u})if(A.IntProperties.FindRef(K)==2)
    {const int32 S=A.IntProperties.FindRef(K+1);if(S==44||S==45||S==46||S==41||S==47){WeaponSkill=S;break;}}
    for (const auto& T : LootTemplates)
    {
        if (T.WCID != Item.WeenieClassId) continue;
        for (const auto& C : LootCohorts)
        {
            if (C.Variant != T.Variant) continue;
            const auto& V = LootVariants[T.Variant];
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
bool ComparableWeaponCohort(const FACEWorldObject& Item,const FACEAppraisalInfo& A,FLootCohort& Out)
{
    if(A.IntProperties.FindRef(105)<=0)return false;
    int32 Skill=A.WeaponSkill?A.WeaponSkill:A.IntProperties.FindRef(48),Kind=0,Wield=0;
    for(uint32 K:{158u,270u,273u,276u})if(A.IntProperties.FindRef(K)==2)
    {const int32 S=A.IntProperties.FindRef(K+1);if(S==44||S==45||S==46||S==41||S==47||S==34||S==43){Skill=S;Kind=2;Wield=A.IntProperties.FindRef(K+2);break;}}
    const int32 Family=(Item.ItemType&ACEItemType::Caster)?(Skill==34||Skill==43?3:4):(Item.ItemType&ACEItemType::MissileWeapon)?2:1;
    const TCHAR* Types[]={TEXT(""),TEXT("Unarmed"),TEXT("Sword"),TEXT("Axe"),TEXT("Mace"),TEXT("Spear"),TEXT("Dagger"),TEXT("Staff"),TEXT("Bow"),TEXT("Crossbow"),TEXT("Atlatl")};
    const int32 Type=A.IntProperties.FindRef(353);const FString Shape=Type>0&&Type<UE_ARRAY_COUNT(Types)?Types[Type]:TEXT("");
    bool Found=false;
    for(const auto& G:LootCohorts)
    {
        const auto& V=LootVariants[G.Variant];
        if(V.Family!=Family||(Family!=4&&V.Skill!=Skill)||G.Kind!=Kind||G.Wield!=Wield||(!Shape.IsEmpty()&&!FString(V.Label).Contains(Shape)))continue;
        if(!Found){Out=G;Found=true;continue;}
        for(int32 I=0;I<9;++I)if(G.Bounds[I].Low>=0)
        {if(Out.Bounds[I].Low<0)Out.Bounds[I]=G.Bounds[I];else {Out.Bounds[I].Low=FMath::Min(Out.Bounds[I].Low,G.Bounds[I].Low);Out.Bounds[I].High=FMath::Max(Out.Bounds[I].High,G.Bounds[I].High);}}
    }
    return Found;
}
const FLootArmorCohort* ArmorCohort(const FLootArmorTemplate& T,const FACEAppraisalInfo& A)
{
    int32 Kind=A.IntProperties.FindRef(158),Skill=A.IntProperties.FindRef(159),Wield=A.IntProperties.FindRef(160),Level=0;
    for(uint32 K:{270u,273u,276u})if(A.IntProperties.FindRef(K)==7)Level=FMath::Max(Level,A.IntProperties.FindRef(K+2));
    if(Kind!=2)Skill=0;
    for(const auto& G:LootArmorCohorts)
        if(G.Family==T.Family&&G.Kind==Kind&&G.Skill==Skill&&G.Wield==Wield&&G.Level==Level)return &G;
    return nullptr;
}
bool EstimateOriginal(const FACEWorldObject& Item,const FACEAppraisalInfo& A,const TMap<FString,double>& Current,
    const FLootCohort* Weapon,const FLootArmorTemplate* Armor,bool Covenant,FACEQualityOriginal& Out)
{
    if(Covenant)return false;
    TArray<TinkerAxis> Axes;TArray<FString> Keys;
    auto Add=[&](const TCHAR* Key,Range Bounds,double Step,double Multiply,double Preference)
    {
        const auto* Value=Current.Find(Key);if(!Value||Bounds.Low<0)return false;
        Keys.Add(Key);Axes.Add({*Value,Bounds,Step,Multiply,Preference});return true;
    };
    int32 Family=0;const FLootArmorCohort* AG=nullptr;
    if(Weapon)
    {
        Family=LootVariants[Weapon->Variant].Family;const auto& B=Weapon->Bounds;
        if(Family==1)
        {
            if(!Add(TEXT("Damage"),B[0],1,1,.55)||!Add(TEXT("Variance"),B[1],0,.8,.25)
                ||!Add(TEXT("Attack"),B[2],.01,1,.08)||!Add(TEXT("Defense"),B[3],.01,1,.12))return false;
        }
        else if(Family==2)
        {
            if(!Add(TEXT("Multiplier"),B[4],.04,1,.8)||!Add(TEXT("Defense"),B[3],.01,1,.2))return false;
            if(B[5].Low>=0&&!Add(TEXT("ElementBonus"),B[5],0,1,1))return false;
        }
        else if(Family==3)
        {
            if(!Add(TEXT("Element"),B[6],.01,1,.7)||!Add(TEXT("Defense"),B[3],.01,1,.2))return false;
            if(Current.Contains(TEXT("ManaConversion")))Add(TEXT("ManaConversion"),B[8],.01,1,.1);
        }
        else if(Family==4)
        {if(!Add(TEXT("Defense"),B[3],.01,1,.7)||!Add(TEXT("ManaConversion"),B[8],.01,1,.3))return false;}
        else return false;
    }
    else
    {
        AG=Armor?ArmorCohort(*Armor,A):nullptr;if(!AG)return false;
        if(!Add(TEXT("Armor"),AG->Bounds,20,1,.7))return false;
        const int32 Bits[]={2,8,1,4};
        for(int32 I=0;I<4;++I)if(Armor->Mutates&Bits[I])
        {
            const FString Key=FString::Printf(TEXT("Armor%d"),16+I);
            if(!Add(*Key,{Armor->Elements[I],FMath::Min(2.,Armor->Elements[I]+.9)},.4,1,.075))return false;
        }
    }
    auto Score=[&](const double* V)
    {
        if(Family==1)return Melee(MeanHit(V[0],V[1]),V[2],V[3],Weapon->Bounds[7],Weapon->Bounds[2],Weapon->Bounds[3]).Total();
        if(Family==2)
        {
            const auto& B=Weapon->Bounds;const double W1=B[4].Variable()?60:0,W2=B[5].Variable()?25:0,W3=B[3].Variable()?15:0;
            return W1+W2+W3>0?100*(W1*Position(V[0],B[4])+W3*Position(V[1],B[3])+(Axes.Num()>2?W2*Position(V[2],B[5]):0))/(W1+W2+W3):0;
        }
        if(Family==3)return 85*Position(V[0],Weapon->Bounds[6])+15*Position(V[1],Weapon->Bounds[3]);
        if(Family==4)return 80*Position(V[0],Weapon->Bounds[3])+20*Position(V[1],Weapon->Bounds[8]);
        double Protection=0;for(int32 I=1;I<Axes.Num();++I)Protection+=Position(V[I],Axes[I].Bounds);
        return Axes.Num()>1?12*Position(V[0],AG->Bounds)+8*Protection/(Axes.Num()-1):20*Position(V[0],AG->Bounds);
    };
    bool Imbued=false;for(uint32 K=179;K<=183;++K)Imbued|=A.IntProperties.FindRef(K)!=0;
    const auto Guess=InferTinkers(Axes.GetData(),Axes.Num(),A.IntProperties.FindRef(171),Imbued?1:0,Score);
    if(!Guess.Valid)return false;
    Out.WCID=Item.WeenieClassId;Out.Setup=Item.SetupId;Out.Icon=Item.IconId;Out.Name=Item.Name;Out.Stats=Current;
    Out.bEstimated=true;Out.EstimateLow=Guess.Low;Out.EstimateHigh=Guess.High;
    for(int32 I=0;I<Keys.Num();++I)Out.Stats.Add(Keys[I],Guess.Base[I]);
    return true;
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
        Values.Add(TEXT("Speed"), A.IntProperties.Contains(49) ? A.IntProperties.FindRef(49) : A.WeaponTime);
    }
    if (A.FloatProperties.Contains(29)) Values.Add(TEXT("Defense"),F(A,29));
    if (A.FloatProperties.Contains(152)) Values.Add(TEXT("Element"),F(A,152));
    if (A.IntProperties.Contains(204)) Values.Add(TEXT("ElementBonus"),A.IntProperties.FindRef(204));
    if (A.IntProperties.Contains(28)) Values.Add(TEXT("Armor"),A.IntProperties.FindRef(28));
    const int32 ResistanceKeys[]={13,14,15,16,17,18,165,19};
    for(int32 I=0;I<8;++I)if(A.ArmorResistances.IsValidIndex(I))
        Values.Add(FString::Printf(TEXT("Armor%d"),ResistanceKeys[I]),F(A,ResistanceKeys[I],A.ArmorResistances[I]));
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
        if (Type==4 && (S->Key==49 || S->Key==361)) { Key=TEXT("Speed");AlreadyRaw=A.IntProperties.Contains(49); }
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
            case 13:case 14:case 15:case 16:case 17:case 18:case 19:case 165:
                Key=FString::Printf(TEXT("Armor%d"),S->Key);AlreadyRaw=A.FloatProperties.Contains(S->Key);break;
            }
        }
        if (Type==4 && S->Key==28) Key=TEXT("Armor");
        if (Key.IsEmpty() || AlreadyRaw) continue;
        if(Key.StartsWith(TEXT("Armor"))&&Key!=TEXT("Armor")&&FMath::Abs(Appraised.FindRef(Key))>=2)
        {
            // A clamped resistance cannot be inverted by subtracting a bane.
            // Keep the reliable dimensions and leave this original unknown.
            Values.Remove(Key);continue;
        }
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
struct FCovenantProjection
{
    bool Known=false;double Armor=0,Impen=0,BaselineImpen=0;
    double Resistance[7]={},Baseline[7]={};bool BaselineKnown[7]={};
};
FString ProtectionKey(const FLootSpell& S)
{
    if(!(S.Type&0x8000)||S.Amount<=0)return {};
    if((S.Type&0xff)==4&&S.Key==28)return TEXT("Armor");
    return (S.Type&0xff)==8&&S.Key>=13&&S.Key<=19?FString::Printf(TEXT("Armor%d"),S.Key):FString();
}
FCovenantProjection CovenantProjection(const FACEAppraisalInfo& A,const TMap<FString,double>& Permanent,UACEDatSubsystem* Dat)
{
    FCovenantProjection Out;if(!Dat||!Permanent.Contains(TEXT("Armor")))return Out;
    TMap<uint32,TPair<uint32,const FLootSpell*>> Owned,Act;
    auto Collect=[&](int32 Id,auto& Categories)
    {
        const auto* S=Spell(Id);if(!S||ProtectionKey(*S).IsEmpty())return;
        uint32 School=0,Power=0,Category=0,Flags=0;double Duration=0;
        if(!Dat->TryGetPluginSpellInfo(Id,School,Power,Category,Flags,Duration))return;
        const auto* Old=Categories.Find(Category);
        if(!Old||Power>Old->Key||(Power==Old->Key&&S->Amount>Old->Value->Amount))Categories.Add(Category,{Power,S});
    };
    for(int32 Entry:A.SpellBookEntries.IsEmpty()?A.SpellIds:A.SpellBookEntries)
        if(uint32(Entry)&0x80000000u)Collect(uint32(Entry)&0x7fffffffu,Act);else Collect(Entry,Owned);
    TMap<FString,double> Full,Baseline,Active;TSet<FString> Uncovered;
    for(const auto& P:Act)
    {
        const auto& S=*P.Value.Value;const FString Key=ProtectionKey(S);Active.FindOrAdd(Key)+=S.Amount;
        const auto* Own=Owned.Find(P.Key);
        if(!Own||Own->Value->Amount+1.e-5<S.Amount)Uncovered.Add(Key);
    }
    for(const auto& P:Owned)
    {
        const auto& S=*P.Value.Value;const FString Key=ProtectionKey(S);Full.FindOrAdd(Key)+=S.Amount;
        const auto* Applied=Act.Find(P.Key);
        Baseline.FindOrAdd(Key)+=S.Family?Applied?FMath::Min(S.Amount,Applied->Value->Amount):0:S.Amount;
    }
    if((A.ArmorEnchantments&1)&&!Active.Contains(TEXT("Armor")))return Out;
    Out.Armor=Permanent.FindRef(TEXT("Armor"));Out.Impen=Full.FindRef(TEXT("Armor"));Out.BaselineImpen=Baseline.FindRef(TEXT("Armor"));
    for(int32 I=0;I<7;++I)
    {
        const int32 Property=13+I,ProfileIndex=I==6?7:I;
        const FString Key=FString::Printf(TEXT("Armor%d"),Property);
        if((A.ArmorEnchantments&(2u<<I))&&!Active.Contains(Key)&&!A.FloatProperties.Contains(Property))return Out;
        double Low=0,High=0;
        if(const auto* V=Permanent.Find(Key))Low=High=*V;
        else
        {
            if(!A.ArmorResistances.IsValidIndex(ProfileIndex)||A.ArmorResistances[ProfileIndex]<2-1.e-5
                ||!Active.Contains(Key)||Uncovered.Contains(Key))return Out;
            Low=FMath::Max(I<3?1.3:.6,2-Active.FindRef(Key));
            High=A.IntProperties.FindRef(171)>0?2:1.6;
        }
        if(!FMath::IsFinite(Low)||!FMath::IsFinite(High)||Low>High+1.e-5)return Out;
        if(A.IntProperties.FindRef(171)==0&&(!Range{I<3?1.3:.6,1.6}.Contains(Low)||!Range{I<3?1.3:.6,1.6}.Contains(High)))return Out;
        const double FullLow=FMath::Clamp(Low+Full.FindRef(Key),0.,2.),FullHigh=FMath::Clamp(High+Full.FindRef(Key),0.,2.);
        if(!FMath::IsNearlyEqual(FullLow,FullHigh,1.e-5))return Out;
        Out.Resistance[I]=FullLow;
        const double Before=FMath::Clamp(Low+Baseline.FindRef(Key),0.,2.),After=FMath::Clamp(High+Baseline.FindRef(Key),0.,2.);
        Out.BaselineKnown[I]=FMath::IsNearlyEqual(Before,After,1.e-5);Out.Baseline[I]=Before;
    }
    Out.Known=true;return Out;
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
    else if(Type==4 && S.Key==28)Label=TEXT("item armor level");
    else if(Type==8 && S.Key>=13 && S.Key<=19)
    {
        const TCHAR* Types[]={TEXT("slashing"),TEXT("piercing"),TEXT("bludgeoning"),TEXT("cold"),TEXT("fire"),TEXT("acid"),TEXT("electric")};
        Label=FString::Printf(TEXT("item %s armor multiplier"),Types[S.Key-13]);
    }
    else if(S.Type&0x80)Label=TEXT("character armor");
    else if(Type==8&&S.Key>=64&&S.Key<=70)Label=TEXT("incoming elemental damage");
    if(Label.IsEmpty())return TEXT("Situational utility");
    if(S.Type&0x4000)return FString::Printf(TEXT("×%.2f %s"),S.Amount,*Label);
    const bool Percent=Type==8&&(S.Key==168||S.Key==169||S.Key==170);
    return FString::Printf(TEXT("+%s %s"),*ACELootQuality::FormatStat(S.Amount,Percent),*Label);
}
Role Relevance(const FLootSpell& S, int32 Skill, int32 Family, bool Atlatl,bool ElementalCaster,
    const FACEPlayerVitals& C, const TMap<FString,double>& Stats,bool Wearable=false,UACEDatSubsystem* Dat=nullptr)
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
        // A wearable can support any trained offensive skill. Choosing only the
        // highest current weapon skill discarded valid secondary templates.
        if(Wearable&&Trained(C,S.Key)&&(S.Key==44||S.Key==45||S.Key==46||S.Key==41||S.Key==47||S.Key==34||S.Key==43||S.Key==54))return Role::WeaponSkill;
        if(S.Key==14)return Role::SupportSkill; // Arcane Lore unlocks item activation.
        if((S.Key==31||S.Key==32||S.Key==33)&&Trained(C,S.Key))return Role::SupportSkill;
        if(S.Key==48&&Trained(C,48)&&(Wearable||Skill!=41))return Role::Shield;
        if(S.Key==6)return Role::MeleeDefense;
        if(S.Key==15)return Role::MagicDefense;
        if(S.Key==7)return Role::MissileDefense;
        if((S.Key==16&&Caster&&Trained(C,16))||(S.Key==54&&Trained(C,54))||(S.Key==21&&Trained(C,21))
            ||((S.Key==49||S.Key==50||S.Key==51||S.Key==52)&&Family==1&&(S.Key!=49||Wearable||Skill!=41)&&Trained(C,S.Key)))return Role::SupportSkill;
        if(S.Key==20)return Trained(C,20)&&Trained(C,51)?Role::SupportSkill:Role::Utility;
        if(S.Key==18||S.Key==28||S.Key==29||S.Key==30||S.Key==37||S.Key==38||S.Key==39||S.Key==40)return Role::Crafting;
        if(S.Key==22||S.Key==24)return Role::Movement;
        if(S.Key==35||S.Key==36)return Role::Social;
        if(S.Key==19||S.Key==27)return Role::Appraisal;
        if(S.Key==23)return Role::Utility;
        return Role::Irrelevant;
    }
    if(Type==1&&S.Key>=1&&S.Key<=6)
    {
        // Every attribute has a floor. Primary weapon use and supporting skill
        // formulas raise relevance; they never erase the other attributes.
        const bool StrengthBuild=Wearable?(Trained(C,44)||Trained(C,45)||Trained(C,41)):(!Caster&&Skill!=46&&!(Skill==47&&!Atlatl));
        const bool CoordinationBuild=Wearable?(Trained(C,46)||Trained(C,47)):(Skill==46||(Skill==47&&!Atlatl));
        const bool MagicBuild=Wearable?(Trained(C,34)||Trained(C,43)):Caster;
        TArray<uint32> Attributes;
        if(Dat&&Trained(C,54))Dat->TryGetSkillAttributes(54,Attributes);
        if((S.Key==1&&StrengthBuild)||(S.Key==4&&CoordinationBuild)||((S.Key==5||S.Key==6)&&MagicBuild)||Attributes.Contains(S.Key))return Role::PrimaryAttribute;
        if(S.Key==2)return Role::SurvivalAttribute;
        for(int32 Support:{6,7,14,15,16,21,31,32,33,48})if(Dat&&Trained(C,Support))
        {
            if(Dat->TryGetSkillAttributes(Support,Attributes)&&Attributes.Contains(S.Key))return Role::SecondaryAttribute;
        }
        return Role::BaselineAttribute;
    }
    if(Type==2&&S.Amount>0&&!(S.Type&0x4000))return S.Key==1?Role::HealthVital:S.Key==3?Role::StaminaVital:S.Key==5?Role::ManaVital:Role::Irrelevant;
    if(Type==4&&S.Amount>0&&(S.Key==307||S.Key==308||S.Key==313||S.Key==314||S.Key==315||S.Key==316||S.Key==323))return Role::NativeRating;
    if(Type==8&&S.Key>=3&&S.Key<=5)return Role::Regeneration;
    if(Type==8&&S.Key==171)return Caster&&Stats.FindRef(TEXT("ManaConversion"))>0?Role::ManaEfficiency:Role::Irrelevant;
    if(Type==8&&S.Key>=64&&S.Key<=70)return Role::Ward;
    if(Type==8&&S.Key>=13&&S.Key<=19&&Stats.Contains(FString::Printf(TEXT("Armor%d"),S.Key)))return Role::Armor;
    if((Type==4&&S.Key==28)||(S.Type&0x80))return Role::Armor;
    return Role::Irrelevant;
}
double UtilityFactor(const FLootSpell& S,Role EffectRole,bool Underclothes,
    const FACEPlayerVitals& C,UACEDatSubsystem* Dat,const TMap<int32,const FLootSpell*>& Best)
{
    double Factor=WearableRelevanceFactor(EffectRole,Underclothes&&(S.Type&0xff)==4&&S.Key==28,(S.Type&0x80)!=0);
    if(EffectRole==Role::Crafting)
    {
        double Highest=0,Next=0;TArray<uint32> Attributes;
        if(Dat&&Dat->TryGetSkillAttributes(S.Key,Attributes))for(uint32 Attribute:Attributes)
        {
            double Rank=0;for(const auto& P:Best)if((P.Value->Type&0xff)==1&&P.Value->Key==int32(Attribute))Rank=FMath::Max(Rank,SpellRankFactor(P.Value->Rank));
            if(Rank>Highest){Next=Highest;Highest=Rank;}else Next=FMath::Max(Next,Rank);
        }
        const double Support=Attributes.Num()==1?Highest:.8*Highest+.2*Next;
        Factor=(Trained(C,S.Key)?1.:.55)*(.40+.60*Support);
    }
    if((S.Type&0xff)==16&&(EffectRole==Role::Utility||EffectRole==Role::Social||EffectRole==Role::Appraisal)&&!Trained(C,S.Key))Factor*=.5;
    if(EffectRole==Role::Regeneration&&S.Key!=3)Factor=.65;
    if((S.Type&255)==1&&Dat)for(const auto& P:Best)
    {
        const auto& Craft=*P.Value;
        if((Craft.Type&255)!=16||!Trained(C,Craft.Key)||!(Craft.Key==18||Craft.Key==28||Craft.Key==29||Craft.Key==30||Craft.Key==37||Craft.Key==38||Craft.Key==39||Craft.Key==40))continue;
        TArray<uint32> Attributes;
        if(Dat->TryGetSkillAttributes(Craft.Key,Attributes)&&Attributes.Contains(S.Key))Factor=FMath::Max(Factor,1.);
    }
    return Factor;
}
void GradeSpellList(const FACEWorldObject& Item,const FACEAppraisalInfo& A,const FACEPlayerVitals& C,
    UACEDatSubsystem* Dat,int32 Skill,int32 Family,bool Underclothes,bool Covenant,
    const TMap<FString,double>& Stats,const TMap<int32,const FLootSpell*>& Best,FACEQualityReview& R)
{
    const bool Weapon=(Item.ItemType&(ACEItemType::MeleeWeapon|ACEItemType::MissileWeapon|ACEItemType::Caster))!=0;
    TMap<FString,double> OrdinaryProtection;
    const auto& Book=A.SpellBookEntries.IsEmpty()?A.SpellIds:A.SpellBookEntries;
    for(int32 Entry:Book)
    {
        if(uint32(Entry)&0x80000000u)continue;
        if(const auto* S=Spell(Entry);S&&!S->Family)
        {const FString Key=ProtectionKey(*S);if(!Key.IsEmpty())OrdinaryProtection.FindOrAdd(Key)=FMath::Max(OrdinaryProtection.FindRef(Key),S->Amount);}
    }
    for(int32 Entry:Book)
    {
        if(uint32(Entry)&0x80000000u)continue;
        const auto* S=Spell(Entry);if(!S)continue;
        FString Name;uint32 Icon=0;if(!Dat||!Dat->TryGetSpellInfo(Entry,Name,Icon))Name=FString::Printf(TEXT("Spell %d"),Entry);
        const auto Role=Relevance(*S,Skill,Family,Item.AmmoType==4,(A.DamageType|A.IntProperties.FindRef(45))!=0,C,Stats,!Weapon,Dat);
        double Use=UtilityFactor(*S,Role,Underclothes,C,Dat,Best);
        if(Weapon&&(Role==ACELootScoring::Role::Damage||Role==ACELootScoring::Role::Attack))Use=1;
        const bool Bane=(S->Type&0xff)==8&&S->Key>=13&&S->Key<=19;
        if(Bane)
        {
            Use=0;
            if(Covenant)
            {
                const FString Key=FString::Printf(TEXT("Armor%d"),S->Key);
                const auto* Native=Stats.Find(Key);
                if(Native||!A.IntProperties.FindRef(171))
                {
                    const double Before=Native?*Native:S->Key<=15?1.3:.6;
                    const double Room=FMath::Max(0.,2-Before-(S->Family?OrdinaryProtection.FindRef(Key):0));
                    Use=S->Family?FMath::Clamp(Room/FMath::Max(S->Amount,1.e-8),0.,1.):Room>0?FMath::Clamp(S->Amount/Room,0.,1.):0;
                }
                else {R.SpellGrades.Add(Name,Grade::Common);continue;}
            }
        }
        if(S->Family)
        {
            if(const auto* Stronger=Best.Find(S->Family);Stronger&&(*Stronger)->Id!=S->Id)Use=0;
            R.SpellGrades.Add(Name,SpellUsefulness(S->Rank,Use));
        }
        else if(Covenant&&!ProtectionKey(*S).IsEmpty())
            R.SpellGrades.Add(Name,Bane?Band(85*Use):Band(85*FMath::Clamp(S->Amount/240.,0.,1.)));
        else if(Use>0)R.SpellGrades.Add(Name,Grade::Common);
    }
}
}

TMap<FString,double> ACEAppraisalFormatting::UnbuffedItemStats(const FACEAppraisalInfo& Info,UACEDatSubsystem* Dat,bool& Known)
{
    return PermanentStats(Info,Dat,Known);
}
bool ACELootQuality::CanUseWithin(const FACEAppraisalInfo& A,const FACEPlayerVitals& C,int32 SkillGap,int32 LevelGap,int32 AttributeGap,int32 LoreGap,const FACEWorldObject* Item)
{
    if(!A.bSuccess||!C.bValid)return false;
    auto Candidate=A;
    for(uint32 K:{158u,270u,273u,276u})
    {
        const int32 Kind=Candidate.IntProperties.FindRef(K);
        const int32 Gap=(Kind==1||Kind==2)?SkillGap:(Kind==3||Kind==4)?AttributeGap:Kind==7?LevelGap:0;
        if(Gap>0)Candidate.IntProperties.FindOrAdd(K+2)=FMath::Max(0,Candidate.IntProperties.FindRef(K+2)-Gap);
    }
    bool Server=false;
    if(!ACEPluginEquipment::WieldFailure(Candidate,C,&Server).IsEmpty()||Server)return false;
    if(SkillValue(C,14)<FMath::Max(0,A.IntProperties.FindRef(109)-LoreGap))return false;
    if(A.IntProperties.FindRef(110)>0||A.BoolProperties.FindRef(94)||(A.IntProperties.Contains(107)&&A.IntProperties.FindRef(107)<=0))return false;
    if(A.IntProperties.FindRef(115)>0&&(!A.IntProperties.FindRef(176)||SkillValue(C,A.IntProperties.FindRef(176))+SkillGap<A.IntProperties.FindRef(115)))return false;
    if(Item)if(const auto* T=EssenceTemplate(Item->WeenieClassId))
    {
        auto Gate=[&](int32 K,int32 Fallback){return A.IntProperties.Contains(K)?A.IntProperties.FindRef(K):Fallback;};
        const int32 Skill=Gate(366,54),Specialized=Gate(368,T->Specialized),Mastery=Gate(362,T->Mastery);
        const auto* Have=C.Skills.FindByPredicate([&](const auto& S){return S.SkillId==Skill;});
        const auto* Spec=C.Skills.FindByPredicate([&](const auto& S){return S.SkillId==Specialized;});
        if(!Have||Have->AdvancementClass<2||Have->Current+SkillGap<Gate(367,T->SkillRequired)||C.Level+LevelGap<Gate(369,T->UserLevel))return false;
        if(Specialized&&(!Spec||Spec->AdvancementClass<3))return false;
        if(Mastery&&C.StatQualityInts.FindRef(362)!=Mastery)return false;
        if((A.IntProperties.Contains(92)?A.IntProperties.FindRef(92):Item->MaxStructure)>0&&(A.IntProperties.Contains(91)?A.IntProperties.FindRef(91):Item->Structure)<=0)return false;
    }
    return true;
}
int32 ACELootQuality::AttackSkill(const FACEWorldObject& Item,const FACEAppraisalInfo& A)
{
    if(const auto* G=Cohort(Item,A))return LootVariants[G->Variant].Skill;
    for(uint32 K:{158u,270u,273u,276u})if((A.IntProperties.FindRef(K)==1||A.IntProperties.FindRef(K)==2)&&TArray<int32>{44,45,46,41,47,34,43}.Contains(A.IntProperties.FindRef(K+1)))return A.IntProperties.FindRef(K+1);
    return A.WeaponSkill;
}
bool ACELootQuality::IsSummoningEssence(const FACEWorldObject& Item)
{return EssenceTemplate(Item.WeenieClassId)!=nullptr;}

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
    if(const auto* Essence=EssenceTemplate(Item.WeenieClassId))return EssenceReview(Item,A,Character,*Essence);
    FACEQualityReview R;
    const bool Weapon=(Item.ItemType&(ACEItemType::MeleeWeapon|ACEItemType::MissileWeapon|ACEItemType::Caster))!=0;
    const bool Wearable=!Weapon&&(Item.ValidLocations!=0)&&!(Item.ItemType&ACEItemType::Container);
    const uint32 Coverage=A.IntProperties.Contains(4)?uint32(A.IntProperties.FindRef(4)):
        Item.ClothingPriority?uint32(Item.ClothingPriority):ACEInferClothingPriority(Item.ValidLocations,Item.ItemType);
    const bool Underclothes=Wearable&&(Item.ItemType&ACEItemType::Clothing)&&(Coverage&0x7eu)&&!(Coverage&0x3f00u);
    R.bEquipment=Weapon||Wearable;if(!R.bEquipment)return R;
    R.Class=Weapon?TEXT("Weapon"):TEXT("Wearable");R.Reason=TEXT("Identify to reveal quality");
    if(!A.bSuccess)return R;
    R.bIdentified=true;
    auto Display=A;Display.ItemType=Item.ItemType;
    R.CurrentStats=ACEAppraisalFormatting::EquipmentStatLines(Display,Dat);
    if(!R.CurrentStats.ContainsByPredicate([](const auto& S){return S.Key==TEXT("Value");}))
        R.CurrentStats.Add({TEXT("Value"),TEXT("Value"),Item.Value?FText::AsNumber(Item.Value).ToString():FString(TEXT("—"))});
    if(!R.CurrentStats.ContainsByPredicate([](const auto& S){return S.Key==TEXT("Work");}))R.CurrentStats.Add({TEXT("Work"),TEXT("Workmanship"),TEXT("—")});
    R.Properties=ACEAppraisalFormatting::EquipmentProperties(A,Dat);
    for(int32 Entry:A.SpellBookEntries.IsEmpty()?A.SpellIds:A.SpellBookEntries)
    {
        if(uint32(Entry)&0x80000000u)continue;
        FString Name;uint32 Icon=0;
        if(!Dat||!Dat->TryGetSpellInfo(Entry,Name,Icon))Name=FString::Printf(TEXT("Spell %d"),Entry);
        R.SpellNames.AddUnique(Name);
    }
    R.Tinkers=A.IntProperties.FindRef(171);
    const auto* ArmorDefinition=Weapon?nullptr:ArmorTemplate(Item.WeenieClassId);
    const bool Covenant=ArmorDefinition&&(ArmorDefinition->Family==3||ArmorDefinition->Family==4)
        &&(!A.IntProperties.Contains(36)||A.IntProperties.FindRef(36)>=9999);
    bool CovenantRated=false;
    bool RawKnown=false;auto Permanent=PermanentStats(A,Dat,RawKnown);
    const bool Match=Original&&Original->WCID==Item.WeenieClassId&&Original->Setup==Item.SetupId
        &&Original->Icon==Item.IconId&&Original->Name==Item.Name;
    if(Covenant&&Match&&R.Tinkers==0)
        for(const auto& P:Original->Stats)if(P.Key.StartsWith(TEXT("Armor"))&&!Permanent.Contains(P.Key))Permanent.Add(P.Key,P.Value);
    R.bEstimated=Match&&Original->bEstimated;
    R.bOriginalKnown=(Match&&!R.bEstimated)||(RawKnown&&R.Tinkers==0);
    // A fresh, reliably unbuffed zero-tinker appraisal is stronger evidence
    // than an older cache entry, which may predate a partial stat arrival.
    const auto& Base=R.Tinkers==0&&RawKnown?Permanent:Match?Original->Stats:Permanent;
    const auto* Group=Cohort(Item,A);FLootCohort Comparable{};
    const bool ApproximateBracket=Weapon&&!Group&&ComparableWeaponCohort(Item,A,Comparable);
    if(ApproximateBracket){Group=&Comparable;R.bEstimated=true;}
    const auto* Variant=Group?&LootVariants[Group->Variant]:nullptr;
    if(!Match&&RawKnown&&!R.bOriginalKnown&&R.Tinkers>0)
    {
        FACEQualityOriginal Guess;
        if(EstimateOriginal(Item,A,Permanent,Group,ArmorDefinition,Covenant,Guess))
            return Evaluate(Item,A,Character,Active,Dat,&Guess);
    }
    int32 Family=Variant?Variant->Family:(Item.ItemType&ACEItemType::Caster)?3:(Item.ItemType&ACEItemType::MissileWeapon)?2:1;
    int32 RelevantSkill=Variant?Variant->Skill:A.WeaponSkill?A.WeaponSkill:A.IntProperties.FindRef(48);
    if(!RelevantSkill&&A.IntProperties.FindRef(158)==2)RelevantSkill=A.IntProperties.FindRef(159);
    if(!Weapon)
    {
        int32 Best=0;
        for(int32 Id:{44,45,46,41,47,34,43})if(Trained(Character,Id)&&SkillValue(Character,Id)>Best)
        { Best=SkillValue(Character,Id);RelevantSkill=Id; }
        Family=RelevantSkill==34||RelevantSkill==43?3:RelevantSkill==47?2:1;
    }
    R.Class=Variant?Variant->Label:Wearable?TEXT("Wearable / combat spell package"):
        RelevantSkill?SkillName(RelevantSkill):(Item.ItemType&ACEItemType::Caster)?TEXT("Caster"):
        (Item.ItemType&ACEItemType::MissileWeapon)?TEXT("Missile weapon"):TEXT("Melee weapon");
    if(ApproximateBracket)R.Class=SkillName(RelevantSkill);
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
    WieldChecks(A,Character,R);
    if(R.Requirements.IsEmpty())R.Requirements=ServerCheck?TEXT("Wield requirements: server check needed"):TEXT("Wield requirements met");
    for(uint32 K:{158u,270u,273u,276u})
        if(A.IntProperties.FindRef(K)==2)R.Requirements+=FString::Printf(TEXT("\n%s %d / %d base"),*SkillName(A.IntProperties.FindRef(K+1)),
            SkillValue(Character,A.IntProperties.FindRef(K+1),true),A.IntProperties.FindRef(K+2));
    const int32 Lore=A.IntProperties.FindRef(109),HaveLore=SkillValue(Character,14);
    R.Activation=Lore>0?FString::Printf(TEXT("Arcane Lore %d / %d"),HaveLore,Lore):TEXT("No Arcane Lore requirement");
    R.bActivationBlocked=HaveLore<Lore;
    if(Lore>0)Check(R,FString::Printf(TEXT("Arcane Lore %d"),Lore),FString::FromInt(HaveLore),HaveLore>=Lore,Character.bValid);
    if(A.IntProperties.FindRef(115)>0)
    {
        const int32 Skill=A.IntProperties.FindRef(176),Required=A.IntProperties.FindRef(115);
        R.bActivationBlocked|=Skill==0||SkillValue(Character,Skill)<Required;
        R.Activation+=FString::Printf(TEXT(" · %s %d / %d"),*SkillName(Skill),SkillValue(Character,Skill),Required);
        Check(R,FString::Printf(TEXT("%s %d (activation)"),*SkillName(Skill),Required),FString::FromInt(SkillValue(Character,Skill)),Skill!=0&&SkillValue(Character,Skill)>=Required,Character.bValid&&Skill!=0);
    }
    if(A.IntProperties.Contains(107)&&A.IntProperties.FindRef(107)<=0)
    { R.bActivationBlocked=true;R.Activation+=TEXT(" · Out of item mana");Check(R,TEXT("Item mana > 0"),TEXT("0"),false); }
    if(A.IntProperties.FindRef(110)>0||A.BoolProperties.FindRef(94))
    { R.bActivationBlocked=true;R.Activation+=TEXT(" · Restricted activation: server check needed");Check(R,TEXT("Restricted activation"),TEXT("Server check"),false,false); }
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
    double WearableBanePoints=0;
    for(const auto& P:BestFamilies)
    {
        const auto& S=*P.Value;FACEQualitySpell Q;Q.SpellId=S.Id;Q.Family=S.Family;Q.Rank=S.Rank;
        uint32 Icon=0;if(!Dat||!Dat->TryGetSpellInfo(S.Id,Q.Name,Icon))Q.Name=FString::Printf(TEXT("Cantrip %d"),S.Id);
        Q.Effect=EffectLabel(S,Character);
        const auto EffectRole=Relevance(S,RelevantSkill,Family,Item.AmmoType==4,(A.DamageType|A.IntProperties.FindRef(45))!=0,Character,Permanent,!Weapon,Dat);
        const bool Multiply=(S.Type&0x4000)!=0;
        const bool ItemBane=(S.Type&0xff)==8&&S.Key>=13&&S.Key<=19;
        const bool CovenantSpell=Covenant&&(ItemBane||((S.Type&0xff)==4&&S.Key==28));
        const bool UnderclothesImpen=Underclothes&&(S.Type&0xff)==4&&S.Key==28;
        Q.Points=CantripPoints(EffectRole,Multiply?S.Amount-1:S.Amount,Multiply?S.Legendary-1:S.Legendary,Family==3||Family==4);
        if(EffectRole==Role::Crafting||EffectRole==Role::Utility||EffectRole==Role::Social||EffectRole==Role::Appraisal)Q.Points*=UtilityFactor(S,EffectRole,Underclothes,Character,Dat,BestFamilies);
        if(ItemBane&&!Covenant)
        {
            const FString Key=FString::Printf(TEXT("Armor%d"),S.Key);
            Q.Points=!Weapon&&RawKnown&&Permanent.Contains(Key)?ArmorBanePoints(Permanent[Key],S.Amount):0;
            WearableBanePoints+=Q.Points;
        }
        if(CovenantSpell)Q.Points=0; // Its actual protection is scored once below.
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
        if(Q.Points<=0)Q.Status=CovenantSpell?TEXT("Included in Covenant protection"):TEXT("Utility · no combat score");
        R.CantripPoints+=Q.Points;
        if(Q.Points>0&&!ItemBane)
        {
            const double RelevanceFactor=UtilityFactor(S,EffectRole,Underclothes,Character,Dat,BestFamilies);
            Q.WearableBenefit=SpellRankFactor(S.Rank)*RelevanceFactor;
            WearableBenefits.Add(Q.WearableBenefit);
        }
        if((S.Type&0xff)==4&&S.Key==360&&!((Family==3)||(Family==4)))NativeBonuses.Add(TEXT("Damage"),S.Amount);
        if((S.Type&0xff)==8&&S.Key==168&&Family==1)NativeBonuses.Add(TEXT("Attack"),S.Amount);
        if((S.Type&0xff)==8&&S.Key==169)NativeBonuses.Add(TEXT("Defense"),S.Amount);
        if((S.Type&0xff)==8&&S.Key==170&&(Family==3||Family==4))NativeBonuses.Add(TEXT("Element"),S.Amount);
        if((S.Type&0xff)==4&&S.Key==28)NativeBonuses.Add(TEXT("Armor"),S.Amount);
        if((S.Type&0xff)==8&&S.Key>=13&&S.Key<=19)NativeBonuses.Add(FString::Printf(TEXT("Armor%d"),S.Key),S.Amount);
        if((S.Type&0xff)==8&&S.Key==171&&Multiply)NativeMultipliers.Add(TEXT("ManaConversion"),S.Amount);
        if(Q.bActive||A.SpellBookEntries.Contains(int32(0x80000000u|uint32(S.Id))))
        {
            if((S.Type&0xff)==4&&S.Key==360)ActiveKeys.Add(TEXT("Damage"));
            if((S.Type&0xff)==8&&S.Key==168)ActiveKeys.Add(TEXT("Attack"));
            if((S.Type&0xff)==8&&S.Key==169)ActiveKeys.Add(TEXT("Defense"));
            if((S.Type&0xff)==8&&S.Key==170)ActiveKeys.Add(TEXT("Element"));
            if((S.Type&0xff)==8&&S.Key==171)ActiveKeys.Add(TEXT("ManaConversion"));
            if((S.Type&0xff)==4&&S.Key==28)ActiveKeys.Add(TEXT("Armor"));
            if((S.Type&0xff)==8&&S.Key>=13&&S.Key<=19)ActiveKeys.Add(FString::Printf(TEXT("Armor%d"),S.Key));
        }
        R.Spells.Add(MoveTemp(Q));
    }
    R.Spells.Sort([](const auto& X,const auto& Y){return X.Points==Y.Points?X.SpellId<Y.SpellId:X.Points>Y.Points;});
    auto Stat=[&](const TCHAR* Key,const TCHAR* Label,Range Bounds,bool Percent=false,bool Lower=false)
    {
        if(!Permanent.Contains(Key))return;
        FACEQualityStat S;S.Key=Key;S.Label=Label;S.Bounds=Bounds;S.Original=Base.FindRef(Key);S.Permanent=Permanent.FindRef(Key);
        const auto* Multiplier=NativeMultipliers.Find(Key);
        S.WithCantrip=RawKnown?S.Permanent*(Multiplier?*Multiplier:1)+NativeBonuses.FindRef(Key):S.Permanent;
        S.bOriginalKnown=R.bOriginalKnown&&Base.Contains(Key);S.bOriginalEstimated=Match&&Original->bEstimated&&Base.Contains(Key);S.bPermanentKnown=RawKnown;S.bPercent=Percent;S.bLowerBetter=Lower;
        S.bPotential=!ActiveKeys.Contains(Key);R.Stats.Add(MoveTemp(S));
    };
    bool Valid=Group&&(R.bOriginalKnown||R.bEstimated)&&RawKnown;
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
        else if(Variant->Family==4)
        {
            Stat(TEXT("Defense"),TEXT("Weapon melee-defense bonus"),{B[3].Low-1,B[3].High-1},true);
            if(!R.Stats.IsEmpty()){auto& S=R.Stats.Last();S.Original-=1;S.Permanent-=1;S.WithCantrip-=1;}
            Valid&=Base.Contains(TEXT("Defense"))&&Base.Contains(TEXT("ManaConversion"))&&B[3].Contains(Base.FindRef(TEXT("Defense")))&&B[8].Contains(Base.FindRef(TEXT("ManaConversion")));
            R.OriginalPoints=80*Position(Base.FindRef(TEXT("Defense")),B[3])+20*Position(Base.FindRef(TEXT("ManaConversion")),B[8]);
        }
        else Valid=false;
        Stat(TEXT("ManaConversion"),TEXT("Item mana-conversion bonus"),B[8],true);
    }
    else if(Weapon)
    {
        // Fixed and custom weapons still retain observed modification stages.
        // Empty bounds deliberately provide neither roll limits nor a grade.
        Stat(TEXT("Damage"),TEXT("Maximum damage"),{});
        Stat(TEXT("Variance"),TEXT("Damage variance"),{},true,true);
        for(const auto& P:TArray<TPair<const TCHAR*,const TCHAR*>>{
            {TEXT("Attack"),TEXT("Melee attack bonus")},{TEXT("Defense"),TEXT("Weapon melee-defense bonus")},
            {TEXT("Element"),TEXT("Matching spell-damage bonus")}})
        {
            const int32 Before=R.Stats.Num();Stat(P.Key,P.Value,{},true);
            if(R.Stats.Num()>Before){auto& S=R.Stats.Last();S.Original-=1;S.Permanent-=1;S.WithCantrip-=1;}
        }
        Stat(TEXT("Multiplier"),TEXT("Launcher damage multiplier"),{});
        Stat(TEXT("ElementBonus"),TEXT("Elemental damage bonus"),{});
        Stat(TEXT("ManaConversion"),TEXT("Item mana-conversion bonus"),{},true);
    }
    if(!Weapon)
    {
        // Wearables are evaluated for their spell package, explicitly labelled.
        // An unverified AL/set/rating baseline must not be invented from item level.
        WearableBenefits.Sort(TGreater<double>());
        const double Weights[]={1,.20,.10,.05};double Package=0;
        for(int32 I=0;I<WearableBenefits.Num();++I)Package+=WearableBenefits[I]*(I<4?Weights[I]:.05*FMath::Pow(.5,I-3));
        const auto* Template=ArmorDefinition;
        const bool HasProtection=!Underclothes&&((Item.ItemType&ACEItemType::Armor)||Template||Permanent.FindRef(TEXT("Armor"))>0);
        const double SpellScale=Covenant?20:HasProtection?68:85;
        R.OriginalPoints=0;R.CantripPoints=SpellScale*Package;Valid=!HasProtection||!R.Spells.IsEmpty();
        R.Reason=TEXT("Combat spell quality · protection baseline unverified");
        const auto* ArmorGroup=Template?ArmorCohort(*Template,A):nullptr;
        bool ArmorKnown=!Covenant&&!Underclothes&&ArmorGroup&&RawKnown&&(R.bOriginalKnown||R.bEstimated)&&Base.Contains(TEXT("Armor"))
            &&ArmorGroup->Bounds.Contains(Base.FindRef(TEXT("Armor")));
        const int32 ElementBits[]={2,8,1,4};
        if(ArmorKnown)for(int32 I=0;I<4;++I)
        {
            if(!(Template->Mutates&ElementBits[I]))continue;
            const FString Key=FString::Printf(TEXT("Armor%d"),16+I);
            const Range Bounds{Template->Elements[I],FMath::Min(2.,Template->Elements[I]+.9)};
            if(!Base.Contains(Key)||!Bounds.Contains(Base.FindRef(Key)))ArmorKnown=false;
        }
        if(Permanent.Contains(TEXT("Armor"))&&(Permanent[TEXT("Armor")]>0||NativeBonuses.FindRef(TEXT("Armor"))>0))
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
            R.CantripPoints=68*Package;Valid=true;
            R.Class=(Item.ValidLocations&0x200000)?TEXT("Shield"):TEXT("Armor");
            R.Reason=TEXT("Original protection + calibrated spell package · ACE baseline");
        }
        else if(HasProtection)
        {R.Class=TEXT("Armor / combat spell package");R.Reason=TEXT("Spell-only lower bound · original protection unverified");}
        else R.Class=(Item.ItemType&ACEItemType::Jewelry)?TEXT("Jewelry / combat spell package"):TEXT("Clothing / combat spell package");
        FCovenantProjection SelfProtection;
        if(Covenant)
        {
            R.Class=Template->Family==4?TEXT("Covenant Shield"):TEXT("Covenant Armor");
            R.Properties.AddUnique(TEXT("Unenchantable"));
            SelfProtection=CovenantProjection(A,Permanent,Dat);
            CovenantRated=RawKnown&&ArmorGroup&&SelfProtection.Known&&ArmorGroup->Bounds.Contains(SelfProtection.Armor);
            Valid=CovenantRated;R.OriginalPoints=0;
            R.Reason=TEXT("Covenant protection unverified");
            if(CovenantRated)
            {
                const auto Quality=ACELootScoring::Covenant(SelfProtection.Armor,ArmorGroup->Bounds,SelfProtection.Impen,SelfProtection.Resistance);
                R.OriginalPoints=Quality.ArmorRoll;R.PropertyPoints+=Quality.Protection;
                R.Reason=TEXT("20% native AL + 60% seven-type self protection + 20% useful cantrips");
                FACEQualitySpell Q;Q.Name=TEXT("Covenant protection");Q.Points=Quality.Protection;
                Q.Effect=TEXT("Earned protection and carried Impenetrability/banes, capped by damage type");
                Q.Status=TEXT("Self-spell potential; no outside buffs or future tinkers assumed");R.Spells.Add(MoveTemp(Q));
            }
        }
        // Physical banes can appear too; their display must not be lost just
        // because only the elemental multipliers have random generation ranges.
        for(int32 Key=13;Key<=19;++Key)
        {
            const FString Name=FString::Printf(TEXT("Armor%d"),Key);
            if(!R.Stats.ContainsByPredicate([&](const auto& S){return S.Key==Name;}))
                Stat(*Name,TEXT("Armor resistance"),Covenant?Range{Key<16?1.3:.6,1.6}:Range{});
        }
        Stat(TEXT("Armor165"),TEXT("Nether armor resistance"),{});
        if(RawKnown&&A.IntProperties.Contains(28))
        {
            const double AL=A.IntProperties.FindRef(28);
            const double ExtraAL=ActiveKeys.Contains(TEXT("Armor"))?0:NativeBonuses.FindRef(TEXT("Armor"));
            const int32 Keys[]={13,14,15,16,17,18,165,19};
            for(int32 I=0;I<8;++I)
            {
                if(!A.ArmorResistances.IsValidIndex(I))continue;
                const FString Key=FString::Printf(TEXT("Armor%d"),Keys[I]);
                auto* S=R.Stats.FindByPredicate([&](const auto& V){return V.Key==Key;});if(!S)continue;
                const double Resistance=A.ArmorResistances[I];
                const double Extra=S->bPotential?NativeBonuses.FindRef(Key):0;
                const double Before=FMath::TruncToDouble(AL*FMath::Clamp(Resistance,0.,2.));
                const double After=FMath::TruncToDouble((AL+ExtraAL)*FMath::Clamp(Resistance+Extra,0.,2.));
                if(After>Before)S->CantripPreview=FString::Printf(TEXT("  With cantrip: %.0f (+%.0f)"),After,After-Before);
            }
        }
        if(CovenantRated)
        {
            if(auto* AL=R.Stats.FindByPredicate([](const auto& S){return S.Key==TEXT("Armor");}))
            {
                AL->Original=AL->Permanent;AL->bOriginalKnown=true; // Covenant cannot be steel-tinkered.
                const double Extra=SelfProtection.Impen-SelfProtection.BaselineImpen;
                if(Extra>1.e-5)AL->CantripPreview=FString::Printf(TEXT("  With item spells: %.0f (+%.0f cantrip)"),SelfProtection.Armor+SelfProtection.Impen,Extra);
                else AL->WithCantrip=AL->Permanent;
            }
            for(int32 I=0;I<7;++I)
            {
                const FString Key=FString::Printf(TEXT("Armor%d"),13+I);
                auto* S=R.Stats.FindByPredicate([&](const auto& V){return V.Key==Key;});
                if(!S)
                {auto& New=R.Stats.AddDefaulted_GetRef();New.Key=Key;New.bPermanentKnown=false;New.bOriginalKnown=false;S=&New;}
                S->CantripPreview.Reset();
                if(!SelfProtection.BaselineKnown[I])continue;
                const double Before=FMath::TruncToDouble((SelfProtection.Armor+SelfProtection.BaselineImpen)*SelfProtection.Baseline[I]);
                const double After=FMath::TruncToDouble((SelfProtection.Armor+SelfProtection.Impen)*SelfProtection.Resistance[I]);
                if(After>Before)S->CantripPreview=FString::Printf(TEXT("  With item spells: %.0f (+%.0f cantrip)"),After,After-Before);
            }
            if(auto* Nether=R.Stats.FindByPredicate([](const auto& S){return S.Key==TEXT("Armor165");});Nether&&Nether->bPermanentKnown)
            {
                Nether->CantripPreview.Reset();const double Resistance=FMath::Clamp(Nether->Permanent,0.,2.);
                const double Before=FMath::TruncToDouble((SelfProtection.Armor+SelfProtection.BaselineImpen)*Resistance);
                const double After=FMath::TruncToDouble((SelfProtection.Armor+SelfProtection.Impen)*Resistance);
                if(After>Before)Nether->CantripPreview=FString::Printf(TEXT("  With item spells: %.0f (+%.0f cantrip)"),After,After-Before);
            }
        }
        R.Spells.Sort([](const auto& X,const auto& Y){return X.WearableBenefit==Y.WearableBenefit?X.SpellId<Y.SpellId:X.WearableBenefit>Y.WearableBenefit;});
        for(int32 I=0;I<R.Spells.Num();++I)
        {
            auto& S=R.Spells[I];
            if(S.WearableBenefit>0)S.Points=SpellScale*S.WearableBenefit*(I<4?Weights[I]:.05*FMath::Pow(.5,I-3));
        }
        R.CantripPoints+=WearableBanePoints;
        // Native gear ratings are separate from spell skills and cannot be
        // manufactured from level or the item's display rarity.
        for(uint32 Key:{370u,371u,372u,373u,374u,375u,376u,377u,378u,379u})
        {
            const int32 Value=A.IntProperties.FindRef(Key);if(Value<=0)continue;
            const double Credit=NativeGearPoints(Key,Value,HasProtection?2:3);R.PropertyPoints+=Credit;
            const TCHAR* Labels[]={TEXT("Damage rating"),TEXT("Damage resistance rating"),TEXT("Critical chance rating"),TEXT("Critical resistance rating"),TEXT("Critical damage rating"),TEXT("Critical damage resistance rating"),TEXT("Healing boost rating"),TEXT("Nether resistance rating"),TEXT("Life resistance rating"),TEXT("Maximum health rating")};
            FACEQualitySpell Q;Q.Name=Labels[Key-370];Q.Effect=FString::Printf(TEXT("+%d native rating"),Value);
            Q.Status=TEXT("Permanent gear property");Q.Points=Credit;R.Spells.Add(MoveTemp(Q));Valid=true;
        }
        const int32 Set=A.IntProperties.FindRef(265);
        if(Set)
        {
            const FString SetName=ACEAppraisalFormatting::EquipmentSetName(Set);
            TArray<uint32> SetSpells;double Potential=0;TMap<FString,double> SetTargets;
            const bool KnownSet=Dat&&Dat->TryGetEquipmentSetSpells(Set,MAX_uint32,SetSpells);
            for(uint32 Id:SetSpells)if(const auto* S=Spell(Id))
            {
                const auto Role=Relevance(*S,RelevantSkill,Family,false,false,Character,Permanent,true,Dat);
                const double Amount=(S->Type&0x4000)?FMath::Abs(S->Amount-1):S->Amount;
                const FString Target=FString::Printf(TEXT("%d:%d"),S->Type&255,S->Key);
                SetTargets.FindOrAdd(Target)=FMath::Max(SetTargets.FindRef(Target),EquipmentSetPoints(Role,Amount));
            }
            TArray<double> SetBenefits;SetTargets.GenerateValueArray(SetBenefits);SetBenefits.Sort(TGreater<double>());
            for(int32 I=0;I<SetBenefits.Num();++I)Potential+=SetBenefits[I]*(I==0?1:.30*FMath::Pow(.5,I-1));
            Potential=FMath::Min(36.,Potential);
            bool SetActive=false;TArray<FString> ActiveEffects;
            for(const auto& E:Active)if(E.SpellSetId==Set)
            {
                SetActive=true;
                const FLootSpell Observed{E.SpellId,E.StatModType,E.StatModKey,E.StatModValue,0,0,0};
                if(FMath::IsFinite(Observed.Amount)&&Relevance(Observed,RelevantSkill,Family,false,false,Character,Permanent,true,Dat)!=Role::Irrelevant)
                    ActiveEffects.AddUnique(EffectLabel(Observed,Character));
            }
            FACEQualitySpell Q;Q.Name=(SetName.IsEmpty()?TEXT("Equipment"):SetName)+FString(TEXT(" set"));
            Q.Effect=KnownSet?TEXT("Build-relevant potential from the highest DAT set tier"):TEXT("Set effects unverified");
            Q.Status=SetActive?TEXT("Set bonus active; piece quality includes set potential"):TEXT("Set potential; requires equipped pieces");
            Q.Points=Potential;Q.bActive=SetActive;R.PropertyPoints+=Q.Points;R.Spells.Add(MoveTemp(Q));
            Valid|=Potential>0;
            if(auto* Line=R.CurrentStats.FindByPredicate([](const auto& S){return S.Key==TEXT("Set");}))
                Line->Value+=SetActive?TEXT(" · active"):TEXT(" · set piece");
            if(!ActiveEffects.IsEmpty())
            {
                auto& Line=R.CurrentStats.AddDefaulted_GetRef();Line.Key=TEXT("SetBonus");Line.Label=TEXT("Set bonus");
                Line.Value=FString::Join(ActiveEffects,TEXT(" · "));Line.bSpellModified=true;Line.Color=FLinearColor::Green;
            }
            if(!Covenant&&HasProtection&&!ArmorKnown&&Potential>0)
            {R.Class=TEXT("Armor / set package");R.Reason=TEXT("Relevant set and spell quality · fixed or unverified protection baseline");}
        }
        if(HasProtection&&!ArmorKnown&&!Covenant&&R.CantripPoints<=0&&R.PropertyPoints<=0)
        {Valid=false;R.Reason=TEXT("Protection baseline unverified; no useful combat spell package");}
    }
    else R.Reason=!Group?TEXT("No verified generation bracket for this item"):
        !R.bOriginalKnown?TEXT("Original roll unknown · identify before tinkering to preserve it"):
        !RawKnown?TEXT("Active effects could not be separated reliably"):
        !Valid?TEXT("Stats outside the verified ACE baseline"):TEXT("Original roll + relevant cantrips · ACE baseline");
    bool FinitePermanent=true;for(const auto& P:Permanent)FinitePermanent&=FMath::IsFinite(P.Value);
    if(Weapon&&!Valid&&Group&&R.Tinkers>0&&RawKnown&&FinitePermanent)
    {
        // Custom histories or incomplete spell inversion can prevent an exact
        // reconstruction. Still give owned tinkered gear a clearly estimated
        // current-stat comparison; never invent a saved original or additions.
        const auto& B=Group->Bounds;const int32 K=LootVariants[Group->Variant].Family;
        if(K==1&&Permanent.Contains(TEXT("Damage"))&&Permanent.Contains(TEXT("Variance"))&&Permanent.Contains(TEXT("Attack"))&&Permanent.Contains(TEXT("Defense")))
        {R.OriginalPoints=Melee(MeanHit(Permanent[TEXT("Damage")],Permanent[TEXT("Variance")]),Permanent[TEXT("Attack")],Permanent[TEXT("Defense")],B[7],B[2],B[3]).Total();Valid=true;}
        else if((K==3||K==4)&&Permanent.Contains(TEXT("Defense"))&&(K==3?Permanent.Contains(TEXT("Element")):Permanent.Contains(TEXT("ManaConversion"))))
        {R.OriginalPoints=K==3?85*Position(Permanent[TEXT("Element")],B[6])+15*Position(Permanent[TEXT("Defense")],B[3]):80*Position(Permanent[TEXT("Defense")],B[3])+20*Position(Permanent[TEXT("ManaConversion")],B[8]);Valid=true;}
        else if(K==2&&Permanent.Contains(TEXT("Multiplier"))&&Permanent.Contains(TEXT("Defense")))
        {const double W1=B[4].Variable()?60:0,W2=B[5].Variable()?25:0,W3=B[3].Variable()?15:0;if(W1+W2+W3>0){R.OriginalPoints=100*(W1*Position(Permanent[TEXT("Multiplier")],B[4])+W2*Position(Permanent.FindRef(TEXT("ElementBonus")),B[5])+W3*Position(Permanent[TEXT("Defense")],B[3]))/(W1+W2+W3);Valid=true;}}
        if(Valid){R.bEstimated=true;R.Reason=TEXT("Estimated from current stats; original history unavailable");}
    }
    if(Covenant&&!CovenantRated)Valid=false;
    R.bRated=Valid;R.Points=Overall(R.OriginalPoints,R.CantripPoints)+R.PropertyPoints;R.Grade=Band(R.Points);
    if(Match&&Original->bEstimated)
    {
        R.EstimateLow=Original->EstimateLow+R.CantripPoints+R.PropertyPoints;
        R.EstimateHigh=Original->EstimateHigh+R.CantripPoints+R.PropertyPoints;
        R.Reason=TEXT("Estimated from current stats, wield bracket and tinker count");
    }
    else if(ApproximateBracket){R.EstimateLow=R.EstimateHigh=R.Points;R.Reason=TEXT("Estimated from comparable class and wield ranges");}
    GradeSpellList(Item,A,Character,Dat,RelevantSkill,Family,Underclothes,Covenant,Permanent,BestFamilies,R);
    return R;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/ACELootRatingAudit.inl"
#endif
