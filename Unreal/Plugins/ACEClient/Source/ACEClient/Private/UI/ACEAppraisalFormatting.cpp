#include "ACEAppraisalFormatting.h"
#include "UI/ACEUIResourceResolver.h"

FString ACEAppraisalFormatting::SlayerProperty(UACEDatSubsystem* Dat, uint32 CreatureType)
{
    // ItemExamineUI::Appraisal_ShowSpecialProperties uses SlayerCreatureType
    // (int 166) and the retail creature enum, independently of weapon profiles
    // and SlayerDamageBonus. This includes casters and partial appraisals.
    if (!Dat) return {};
    FString Name=Dat->GetUiResources()->ResolveEnumString(0x10000005,CreatureType);
    if (Name.IsEmpty()) return {};
    Name.ReplaceInline(TEXT("_"),TEXT(" "));
    return CreatureType==31 ? FString(TEXT("Bael'Zharon's Hate")) : Name+TEXT(" slayer");
}

TArray<FACEItemStatLine> ACEAppraisalFormatting::EquipmentStatLines(const FACEAppraisalInfo& Info)
{
    TArray<FACEItemStatLine> Rows;
    if(!Info.bSuccess)return Rows;
    auto Number=[](double V)
    {
        return FMath::IsNearlyEqual(V,FMath::RoundToDouble(V),.0001)?FString::Printf(TEXT("%.0f"),V):FString::Printf(TEXT("%.1f"),V);
    };
    auto Percent=[&](double V){return (V>=0?TEXT("+"):TEXT(""))+Number(V*100)+TEXT("%");};
    auto Add=[&](const TCHAR* Key,const TCHAR* Label,const FString& Value){Rows.Add({Key,Label,Value});};
    const bool Caster=(Info.ItemType&ACEItemType::Caster)!=0;
    const bool Launcher=(Info.ItemType&ACEItemType::MissileWeapon)!=0;
    if(Info.bHasWeaponProfile&&!Caster)
    {
        if(Launcher)
        {
            if(FMath::IsFinite(Info.WeaponDamageMod)&&Info.WeaponDamageMod>0)
                Add(TEXT("Multiplier"),TEXT("Damage multiplier"),TEXT("×")+Number(Info.WeaponDamageMod));
            if(Info.Damage)Add(TEXT("Damage"),TEXT("Missile damage bonus"),FString::Printf(TEXT("%+d"),Info.Damage));
        }
        else if(FMath::IsFinite(Info.DamageVariance))
        {
            Add(TEXT("Damage"),TEXT("Damage per hit"),Number(Info.Damage*(1-Info.DamageVariance))+TEXT("–")+Number(Info.Damage));
            Add(TEXT("Variance"),TEXT("Damage variance"),Number(Info.DamageVariance*100)+TEXT("%"));
        }
        if(FMath::IsFinite(Info.WeaponOffense)&&Info.WeaponOffense>0)
            Add(TEXT("Attack"),TEXT("Attack"),Percent(Info.WeaponOffense-1));
        Add(TEXT("Speed"),TEXT("Speed"),FString::FromInt(Info.WeaponTime));
    }
    if(const auto* V=Info.IntProperties.Find(204))Add(TEXT("ElementBonus"),TEXT("Elemental damage bonus"),FString::Printf(TEXT("%+d"),*V));
    if(const auto* V=Info.FloatProperties.Find(152);V&&FMath::IsFinite(*V))
    {
        Add(TEXT("Element"),TEXT("Spell damage (PvE)"),Percent(*V-1));
        Add(TEXT("ElementPvP"),TEXT("Spell damage (PvP)"),Percent((*V-1)*.5));
    }
    for(const auto& P:TArray<TPair<uint32,const TCHAR*>>{{29,TEXT("Melee defense")},{149,TEXT("Missile defense")},{150,TEXT("Magic defense")}})
        if(const auto* V=Info.FloatProperties.Find(P.Key);V&&FMath::IsFinite(*V))
            Add(P.Key==29?TEXT("Defense"):P.Key==149?TEXT("MissileDefense"):TEXT("MagicDefense"),P.Value,Percent(*V-1));
    if(const auto* V=Info.FloatProperties.Find(144);V&&FMath::IsFinite(*V))Add(TEXT("ManaConversion"),TEXT("Mana conversion"),Percent(*V));
    if(const auto* V=Info.FloatProperties.Find(147);V&&FMath::IsFinite(*V))Add(TEXT("Critical"),TEXT("Critical chance"),Number(*V*100)+TEXT("%"));
    if(const auto* V=Info.FloatProperties.Find(136);V&&FMath::IsFinite(*V))Add(TEXT("CritDamage"),TEXT("Critical damage bonus"),Percent(*V));
    if(const auto* V=Info.FloatProperties.Find(138);V&&FMath::IsFinite(*V))Add(TEXT("Slayer"),TEXT("Slayer damage"),TEXT("×")+Number(*V));
    if(const auto* AL=Info.IntProperties.Find(28))
    {
        Add(TEXT("Armor"),TEXT("Armor level"),FString::FromInt(*AL));
        const TCHAR* Names[]={TEXT("Slashing armor"),TEXT("Piercing armor"),TEXT("Bludgeoning armor"),TEXT("Cold armor"),TEXT("Fire armor"),TEXT("Acid armor"),TEXT("Nether armor"),TEXT("Electric armor")};
        const int32 Keys[]={13,14,15,16,17,18,165,19};
        for(int32 I:{0,1,2,4,3,5,7,6})if(Info.ArmorResistances.IsValidIndex(I)&&FMath::IsFinite(Info.ArmorResistances[I]))
            Rows.Add({FString::Printf(TEXT("Armor%d"),Keys[I]),Names[I],FString::FromInt(FMath::TruncToInt(*AL*FMath::Clamp(Info.ArmorResistances[I],0.f,2.f)))});
    }
    for(uint32 Key:{370u,371u,372u,373u,374u,375u,376u,377u,378u,379u})
    {
        const TCHAR* Names[]={TEXT("Damage rating"),TEXT("Damage resistance"),TEXT("Critical rating"),TEXT("Critical resistance"),TEXT("Critical damage rating"),TEXT("Critical damage resistance"),TEXT("Healing boost"),TEXT("Nether resistance"),TEXT("Life resistance"),TEXT("Maximum health rating")};
        if(const auto* V=Info.IntProperties.Find(Key);V&&*V)Add(TEXT("Rating"),Names[Key-370],FString::Printf(TEXT("%+d"),*V));
    }
    if(const int32 Set=Info.IntProperties.FindRef(265))
    {
        const FString Name=EquipmentSetName(Set);
        Add(TEXT("Set"),TEXT("Set"),Name.IsEmpty()?FString::FromInt(Set):Name);
    }
    if(Info.IntProperties.FindRef(171)>0)Add(TEXT("Tinkers"),TEXT("Tinkers"),FString::FromInt(Info.IntProperties.FindRef(171)));
    if(const auto* V=Info.IntProperties.Find(105))Add(TEXT("Work"),TEXT("Workmanship"),FString::FromInt(*V));
    if(const auto* V=Info.IntProperties.Find(107);V&&Info.IntProperties.Contains(108))
        Add(TEXT("Mana"),TEXT("Item mana"),FString::Printf(TEXT("%d / %d"),*V,Info.IntProperties.FindRef(108)));
    if(const auto* V=Info.IntProperties.Find(106))Add(TEXT("Spellcraft"),TEXT("Spellcraft"),FString::FromInt(*V));
    return Rows;
}

FString ACEAppraisalFormatting::ItemLevelDetails(const FACEAppraisalInfo& Info)
{
    FString Text;
    const uint64 Base=Info.Int64Properties.FindRef(5), XP=Info.Int64Properties.FindRef(4);
    const int32 Maximum=Info.IntProperties.FindRef(319), Scheme=Info.IntProperties.FindRef(320);
    if (Base>0 && Maximum>0 && Scheme>=1 && Scheme<=3)
    {
        // ExperienceSystem's linear, doubling and increasing-cost schemes.
        // Binary search avoids work proportional to a custom server's cap.
        auto Threshold=[&](int32 Level,uint64& Value)
        {
            uint64 Factor=uint64(Level);
            if (Scheme==2)
            {
                if (Level>64) { Value=MAX_uint64; return false; }
                Factor=Level==64?MAX_uint64:(uint64(1)<<Level)-1;
            }
            else if (Scheme==3) Factor=uint64(Level)*(uint64(Level)+1)/2;
            if (Factor && Base>MAX_uint64/Factor) { Value=MAX_uint64; return false; }
            Value=Base*Factor; return true;
        };
        int32 Low=0, High=Maximum;
        while (Low<High)
        {
            const int32 Mid=Low+int32((int64(High)-Low+1)/2);
            uint64 Required=0;
            if (Threshold(Mid,Required) && XP>=Required) Low=Mid;
            else High=Mid-1;
        }
        uint64 Next=0; Threshold(Low<Maximum?Low+1:Maximum,Next);
        Text=FString::Printf(TEXT("Item Level: %d / %d\nItem XP: %s / %s"),Low,Maximum,
            *FText::AsNumber(XP).ToString(),*FText::AsNumber(Next).ToString());
    }
    if (Info.IntProperties.FindRef(352)==2)
        AppendItemText(Text,TEXT("This cloak has a chance to reduce an incoming attack by 200 damage."),!Text.IsEmpty());
    return Text;
}

FString ACEAppraisalFormatting::RequirementEnumName(UACEDatSubsystem* Dat, uint32 Enum, int32 Value)
{
    if (Dat)
    {
        auto* Resources = NewObject<UACEUIResourceResolver>();
        Resources->Initialize(Dat);
        FString Name = Resources->ResolveEnumString(Enum, Value);
        Resources->Shutdown();
        Name.ReplaceInline(TEXT("_"),TEXT(" "));
        if (!Name.IsEmpty()) return Name;
    }
    return FString::FromInt(Value);
}
namespace ACEAppraisalFormatting::Titles
{
#include "Protocol/ACECharacterTitleNames.inl"
}

ACEAppraisalFormatting::FCreatureHeadings ACEAppraisalFormatting::CreatureHeadings(
    const FACEAppraisalInfo& Info, UACEUIResourceResolver* Resources, const FACEWorldObject* Subject)
{
    FCreatureHeadings Result;
    if (Info.CreatureType && Resources) Result.Type = Resources->ResolveEnumString(0x10000005, Info.CreatureType);
    Result.Type.ReplaceInline(TEXT("_"), TEXT(" "));
    if (!UsesCharacterExamination(Info)) return Result;
    FString Gender = Resources ? Resources->ResolveEnumString(0x10000001, Info.IntProperties.FindRef(113)) : FString();
    FString Heritage = Resources ? Resources->ResolveEnumString(0x10000002, Info.IntProperties.FindRef(188)) : FString();
    if (!Info.IntProperties.FindRef(113)) Gender.Reset();
    if (!Info.IntProperties.FindRef(188)) Heritage = Result.Type;
    Gender.ReplaceInline(TEXT("_"), TEXT(" ")); Heritage.ReplaceInline(TEXT("_"), TEXT(" "));
    Result.Heritage = (Gender + TEXT(" ") + Heritage).TrimStartAndEnd();
    Result.Profession = Info.StringProperties.FindRef(5);
    if (const int32* Title = Info.IntProperties.Find(261))
    {
        const FString Name = Titles::GetCharacterTitleName(*Title);
        if (Name != TEXT("(Unknown Title)")) Result.Profession = Name;
    }
    const int32 PK = Info.IntProperties.FindRef(134);
    const bool bPK = Subject ? (Subject->ObjectDescriptionFlags & ACEObjectDescFlag::PlayerKiller) != 0 : (PK & ACEPlayerKillerStatus::PK) != 0;
    const bool bPKLite = Subject ? (Subject->ObjectDescriptionFlags & ACEObjectDescFlag::PkLiteStatus) != 0 : (PK & ACEPlayerKillerStatus::PKLite) != 0;
    Result.PlayerKiller = bPK ? TEXT("Player Killer") : bPKLite ? TEXT("Player Killer Lite") : TEXT("Non-Player Killer");
    if (Info.IntProperties.FindRef(30) > 0) Result.Allegiance = Info.StringProperties.FindRef(47);
    return Result;
}

FString ACEAppraisalFormatting::EquipmentSetName(int32 Id)
{
    // ItemExamineUI::Appraisal_ShowSet names for replicated EquipmentSetId.
    switch(Id)
    {
    case 4: return TEXT("Carraida's Benediction");
    case 5: return TEXT("Noble Relic");
    case 6: return TEXT("Ancient Relic");
    case 7: return TEXT("Alduressa Relic");
    case 8: return TEXT("Shou-jen");
    case 9: return TEXT("Empyrean Rings");
    case 10: return TEXT("Arm, Mind, Heart");
    case 11: return TEXT("Coat of Perfect Light");
    case 12: return TEXT("Leggings of Perfect Light");
    case 13: return TEXT("Soldier's");
    case 14: return TEXT("Adept's");
    case 15: return TEXT("Archer's");
    case 16: return TEXT("Defender's");
    case 17: return TEXT("Tinker's");
    case 18: return TEXT("Crafter's");
    case 19: return TEXT("Hearty");
    case 20: return TEXT("Dexterous");
    case 21: return TEXT("Wise");
    case 22: return TEXT("Swift");
    case 23: return TEXT("Hardened");
    case 24: return TEXT("Reinforced");
    case 25: return TEXT("Interlocking");
    case 26: return TEXT("Flame Proof");
    case 27: return TEXT("Acid Proof");
    case 28: return TEXT("Cold Proof");
    case 29: return TEXT("Lightning Proof");
    case 30: return TEXT("Dedication");
    case 31: return TEXT("Gladiatorial Clothing");
    case 32: return TEXT("Ceremonial Clothing");
    case 33: return TEXT("Protective Clothing");
    case 35: return TEXT("Sigil of Defense");
    case 36: return TEXT("Sigil of Destruction");
    case 37: return TEXT("Sigil of Fury");
    case 38: return TEXT("Sigil of Growth");
    case 39: return TEXT("Sigil of Vigor");
    case 40: return TEXT("Heroic Protector");
    case 41: return TEXT("Heroic Destroyer");
    case 49: return TEXT("Weave of Alchemy");
    case 50: return TEXT("Weave of Arcane Lore");
    case 51: return TEXT("Weave of Armor Tinkering");
    case 52: return TEXT("Weave of Assess Person");
    case 79: return TEXT("Weave of Light Weapons");
    case 77: return TEXT("Weave of Missile Weapons");
    case 55: return TEXT("Weave of Cooking");
    case 56: return TEXT("Weave of Creature Enchantment");
    case 58: return TEXT("Weave of Finesse Weapons");
    case 59: return TEXT("Weave of Deception");
    case 60: return TEXT("Weave of Fletching");
    case 61: return TEXT("Weave of Healing");
    case 62: return TEXT("Weave of Item Enchantment");
    case 63: return TEXT("Weave of Item Tinkering");
    case 64: return TEXT("Weave of Leadership");
    case 65: return TEXT("Weave of Life Magic");
    case 66: return TEXT("Weave of Loyalty");
    case 68: return TEXT("Weave of Magic Defense");
    case 69: return TEXT("Weave of Magic Item Tinkering");
    case 70: return TEXT("Weave of Mana Conversion");
    case 71: return TEXT("Weave of Melee Defense");
    case 72: return TEXT("Weave of Missile Defense");
    case 73: return TEXT("Weave of Salvaging");
    case 76: return TEXT("Weave of Heavy Weapons");
    case 78: return TEXT("Weave of Two Handed Combat");
    case 80: return TEXT("Weave of Void Magic");
    case 81: return TEXT("Weave of War Magic");
    case 82: return TEXT("Weave of Weapon Tinkering");
    case 83: return TEXT("Weave of Assess Creature");
    case 84: return TEXT("Weave of Dirty Fighting");
    case 85: return TEXT("Weave of Dual Wield");
    case 86: return TEXT("Weave of Recklessness");
    case 87: return TEXT("Weave of Shield");
    case 88: return TEXT("Weave of Sneak Attack");
    case 89: return TEXT("Shou-jen Shozoku");
    case 90: return TEXT("Weave of Summoning");
    case 91: return TEXT("Shrouded Soul");
    case 92: return TEXT("Darkened Mind");
    case 93: return TEXT("Clouded Spirit");
    case 94: return TEXT("Minor Stinging Shrouded Soul");
    case 95: return TEXT("Minor Sparking Shrouded Soul");
    case 96: return TEXT("Minor Smoldering Shrouded Soul");
    case 97: return TEXT("Minor Shivering Shrouded Soul");
    case 98: return TEXT("Minor Stinging Darkened Mind");
    case 99: return TEXT("Minor Sparking Darkened Mind");
    case 100: return TEXT("Minor Smoldering Darkened Mind");
    case 101: return TEXT("Minor Shivering Darkened Mind");
    case 102: return TEXT("Minor Stinging Clouded Spirit");
    case 103: return TEXT("Minor Sparking Clouded Spirit");
    case 104: return TEXT("Minor Smoldering Clouded Spirit");
    case 105: return TEXT("Minor Shivering Clouded Spirit");
    case 106: return TEXT("Major Stinging Shrouded Soul");
    case 107: return TEXT("Major Sparking Shrouded Soul");
    case 108: return TEXT("Major Smoldering Shrouded Soul");
    case 109: return TEXT("Major Shivering Shrouded Soul");
    case 110: return TEXT("Major Stinging Darkened Mind");
    case 111: return TEXT("Major Sparking Darkened Mind");
    case 112: return TEXT("Major Smoldering Darkened Mind");
    case 113: return TEXT("Major Shivering Darkened Mind");
    case 114: return TEXT("Major Stinging Clouded Spirit");
    case 115: return TEXT("Major Sparking Clouded Spirit");
    case 116: return TEXT("Major Smoldering Clouded Spirit");
    case 117: return TEXT("Major Shivering Clouded Spirit");
    case 118: return TEXT("Blackfire Stinging Shrouded Soul");
    case 119: return TEXT("Blackfire Sparking Shrouded Soul");
    case 120: return TEXT("Blackfire Smoldering Shrouded Soul");
    case 121: return TEXT("Blackfire Shivering Shrouded Soul");
    case 122: return TEXT("Blackfire Stinging Darkened Mind");
    case 123: return TEXT("Blackfire Sparking Darkened Mind");
    case 124: return TEXT("Blackfire Smoldering Darkened Mind");
    case 125: return TEXT("Blackfire Shivering Darkened Mind");
    case 126: return TEXT("Blackfire Stinging Clouded Spirit");
    case 127: return TEXT("Blackfire Sparking Clouded Spirit");
    case 128: return TEXT("Blackfire Smoldering Clouded Spirit");
    case 129: return TEXT("Blackfire Shivering Clouded Spirit");
    case 130: return TEXT("Shimmering Shadows");
    default: return FString();
    }
}

TArray<FString> ACEAppraisalFormatting::EquipmentProperties(const FACEAppraisalInfo& Info, UACEDatSubsystem* Dat)
{
    uint32 Imbued = 0;
    for (uint32 Id : {179u,303u,304u,305u,306u}) Imbued |= Info.IntProperties.FindRef(Id);
    TArray<FString> Properties;
    if (Info.IntProperties.FindRef(33)) Properties.Add(TEXT("Bonded"));
    if (Info.IntProperties.FindRef(114)) Properties.Add(TEXT("Attuned"));
    if (const int32* Slayer=Info.IntProperties.Find(166))
    {
        const FString Name=SlayerProperty(Dat,uint32(*Slayer));
        if (!Name.IsEmpty()) Properties.Add(Name);
    }
    for (const auto& E : {TPair<uint32,const TCHAR*>(1,TEXT("Critical Strike")),{2,TEXT("Crippling Blow")},{4,TEXT("Armor Rending")},
        {8,TEXT("Slash Rending")},{16,TEXT("Pierce Rending")},{32,TEXT("Bludgeon Rending")},{64,TEXT("Acid Rending")},
        {128,TEXT("Cold Rending")},{256,TEXT("Lightning Rending")},{512,TEXT("Fire Rending")},{16384,TEXT("Nether Rending")},
        {1024,TEXT("+1 Melee Defense")},{2048,TEXT("+1 Missile Defense")},{4096,TEXT("+1 Magic Defense")},{0x80000000u,TEXT("Phantasmal")}})
        if (Imbued & E.Key) Properties.Add(E.Value);
    // ItemExamineUI::Appraisal_ShowSpecialProperties reads ResistMagic (int 36),
    // including fields supplied by a partially successful appraisal.
    if (Info.IntProperties.FindRef(36) >= 9999) Properties.Add(TEXT("Unenchantable"));
    if (Info.BoolProperties.FindRef(91)) Properties.Add(TEXT("Retained"));
    for (const auto& Property : {TPair<uint32,const TCHAR*>(136,TEXT("Crushing Blow")),
        {147,TEXT("Biting Strike")},{155,TEXT("Armor Cleaving")}})
        if (Info.FloatProperties.Contains(Property.Key)) Properties.Add(Property.Value);
    // Retail ItemExamineUI::Appraisal_ShowSpecialProperties checks presence of
    // ResistanceModifier (float 157) and ResistanceModifierType (int 263).
    // This is independent of imbued/rending flags and the weapon's damage type.
    if (Info.FloatProperties.Contains(157))
    {
        if (const int32* ResistanceType = Info.IntProperties.Find(263))
        {
            TArray<FString> Names;
            for (const auto& E : {TPair<uint32,const TCHAR*>(1,TEXT("Slashing")),{2,TEXT("Piercing")},{4,TEXT("Bludgeoning")},
                {8,TEXT("Cold")},{16,TEXT("Fire")},{32,TEXT("Acid")},{64,TEXT("Electrical")},{1024,TEXT("Nether")},{0x10000000u,TEXT("Prismatic")}})
                if (uint32(*ResistanceType) & E.Key) Names.Add(E.Value);
            Properties.Add(TEXT("Resistance Cleaving: ") + FString::Join(Names,TEXT("/")));
        }
    }
    if (Info.BoolProperties.FindRef(99)) Properties.Add(TEXT("Ivoryable"));
    if (Info.BoolProperties.FindRef(100)) Properties.Add(TEXT("Dyeable"));
    return Properties;
}
