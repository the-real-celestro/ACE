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

TArray<FACEItemStatLine> ACEAppraisalFormatting::EquipmentStatLines(const FACEAppraisalInfo& Info,UACEDatSubsystem* Dat)
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
                Add(TEXT("Multiplier"),TEXT("Damage modifier"),Percent(Info.WeaponDamageMod-1));
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
    // Appraisal weapon profiles and the defense/caster/armor property values
    // already contain active spells. Never apply a spell to these totals again.
    bool Known=false;
    auto Base=UnbuffedItemStats(Info,Dat,Known);
    if(!Known)Base.Reset();
    auto RawInt=[&](const TCHAR* Key,uint32 Property)
    {if(const auto* V=Info.IntProperties.Find(Property))Base.Add(Key,*V);};
    auto RawFloat=[&](const TCHAR* Key,uint32 Property)
    {if(const auto* V=Info.FloatProperties.Find(Property);V&&FMath::IsFinite(*V))Base.Add(Key,*V);};
    // These instance properties are transmitted unmodified alongside the
    // reflected weapon profile, so they work even without a known spell ID.
    RawInt(TEXT("Damage"),44);RawInt(TEXT("Speed"),49);
    RawFloat(TEXT("Variance"),22);RawFloat(TEXT("Attack"),62);RawFloat(TEXT("Multiplier"),63);
    if(!Info.IntProperties.Contains(49)&&Info.WeaponTime==0&&(Info.WeaponEnchantments&4))Base.Remove(TEXT("Speed"));
    if(!Info.IntProperties.Contains(44)&&Info.Damage==0&&(Info.WeaponEnchantments&8))Base.Remove(TEXT("Damage"));
    if(!Base.Contains(TEXT("Variance"))&&!(Info.WeaponEnchantments&16))Base.Add(TEXT("Variance"),Info.DamageVariance);
    if(!(Info.ArmorEnchantments&1)&&Info.IntProperties.Contains(28))Base.Add(TEXT("Armor"),Info.IntProperties.FindRef(28));
    const uint32 ArmorKeys[]={13,14,15,16,17,18,165,19};
    const uint32 ArmorBits[]={2,4,8,16,32,64,256,128};
    for(int32 I=0;I<8;++I)
    {
        const FString Key=FString::Printf(TEXT("Armor%d"),ArmorKeys[I]);
        if((Info.ArmorEnchantments&ArmorBits[I])&&Info.ArmorResistances.IsValidIndex(I)
            &&FMath::Abs(Info.ArmorResistances[I])>=2)Base.Remove(Key); // clamped totals cannot be inverted
        RawFloat(*Key,ArmorKeys[I]);
    }
    TMap<FString,double> Current;
    if(Info.bHasWeaponProfile)
    {
        Current.Add(TEXT("Damage"),Info.Damage);Current.Add(TEXT("Speed"),Info.WeaponTime);
        Current.Add(TEXT("Variance"),Info.DamageVariance);Current.Add(TEXT("Attack"),Info.WeaponOffense);
        Current.Add(TEXT("Multiplier"),Info.WeaponDamageMod);
    }
    for(const auto& P:TArray<TPair<const TCHAR*,uint32>>{{TEXT("Defense"),29},{TEXT("MissileDefense"),149},
        {TEXT("MagicDefense"),150},{TEXT("ManaConversion"),144},{TEXT("Element"),152}})
        if(const auto* V=Info.FloatProperties.Find(P.Value);V&&FMath::IsFinite(*V))Current.Add(P.Key,*V);
    if(const auto* AL=Info.IntProperties.Find(28))Current.Add(TEXT("Armor"),*AL);
    auto Signed=[&](double V){return FString(V>=0?TEXT("+"):TEXT(""))+Number(V);};
    for(auto& Row:Rows)
    {
        uint32 Flags=0,Bit=0;
        const FString Key=Row.Key==TEXT("ElementPvP")?TEXT("Element"):Row.Key;
        if(Key==TEXT("Damage")){Flags=Info.WeaponEnchantments;Bit=(Flags&8)?8:16;}
        else if(Key==TEXT("Variance")){Flags=Info.WeaponEnchantments;Bit=16;}
        else if(Key==TEXT("Speed")){Flags=Info.WeaponEnchantments;Bit=4;}
        else if(Key==TEXT("Attack")){Flags=Info.WeaponEnchantments;Bit=1;}
        else if(Key==TEXT("Defense")){Flags=Info.WeaponEnchantments;Bit=2;}
        else if(Key==TEXT("Multiplier")){Flags=Info.WeaponEnchantments;Bit=32;}
        else if(Key==TEXT("ManaConversion")){Flags=Info.ResistanceEnchantments;Bit=0x1000;}
        else if(Key==TEXT("Element")){Flags=Info.ResistanceEnchantments;Bit=0x2000;}
        else if(Key==TEXT("Armor")){Flags=Info.ArmorEnchantments;Bit=1;}
        for(int32 I=0;I<8;++I)if(Key==FString::Printf(TEXT("Armor%d"),ArmorKeys[I]))
        {Flags=Info.ArmorEnchantments;Bit=(Flags&ArmorBits[I])?ArmorBits[I]:1;}
        Row.bSpellModified=(Flags&Bit)!=0;
        Row.Color=!Row.bSpellModified?FLinearColor::White:(Flags&(Bit<<16))?FLinearColor::Green:FLinearColor(1.f,.2f,.15f);
        double Now=Current.FindRef(Key),Before=Base.FindRef(Key);
        bool HasDelta=Current.Contains(Key)&&Base.Contains(Key)&&FMath::IsFinite(Now)&&FMath::IsFinite(Before);
        if(Key.StartsWith(TEXT("Armor"))&&Key!=TEXT("Armor"))
        {
            int32 Index=INDEX_NONE;
            for(int32 I=0;I<8;++I)if(Key==FString::Printf(TEXT("Armor%d"),ArmorKeys[I]))Index=I;
            HasDelta=Index!=INDEX_NONE&&Info.ArmorResistances.IsValidIndex(Index)&&Base.Contains(Key)&&Base.Contains(TEXT("Armor"));
            if(HasDelta)
            {
                Now=FMath::TruncToDouble(Info.IntProperties.FindRef(28)*FMath::Clamp(double(Info.ArmorResistances[Index]),0.,2.));
                Before=FMath::TruncToDouble(Base.FindRef(TEXT("Armor"))*FMath::Clamp(Base[Key],0.,2.));
            }
        }
        if(!HasDelta)continue;
        const double Delta=Now-Before;
        const bool HitRange=Key==TEXT("Damage")&&!Launcher&&Base.Contains(TEXT("Variance"));
        const double LowDelta=HitRange?Now*(1-FMath::Clamp(double(Info.DamageVariance),0.,1.))
            -Before*(1-FMath::Clamp(Base[TEXT("Variance")],0.,1.)):Delta;
        if(FMath::IsNearlyZero(Delta,1.e-5)&&FMath::IsNearlyZero(LowDelta,1.e-5))continue;
        Row.bSpellModified=true;
        const bool Lower=Key==TEXT("Speed")||Key==TEXT("Variance");
        const bool Beneficial=Lower?Delta<0:(Delta>0||(FMath::IsNearlyZero(Delta,1.e-5)&&LowDelta>0));
        Row.Color=Beneficial?FLinearColor::Green:FLinearColor(1.f,.2f,.15f);
        FString Contribution;
        if(HitRange)
        {
            Contribution=FMath::IsNearlyEqual(LowDelta,Delta,.05)?Signed(Delta)
                :Signed(LowDelta)+TEXT("–")+(LowDelta>=0&&Delta>=0?Number(Delta):Signed(Delta));
        }
        else
        {
            const bool PercentStat=Key==TEXT("Attack")||Key==TEXT("Defense")||Key==TEXT("MissileDefense")
                ||Key==TEXT("MagicDefense")||Key==TEXT("Variance")||Key==TEXT("Multiplier")
                ||Key==TEXT("Element")||Key==TEXT("ManaConversion");
            const double Scale=Row.Key==TEXT("ElementPvP")?50:PercentStat?100:1;
            Contribution=Signed(Delta*Scale)+(PercentStat?TEXT("%"):Key==TEXT("Damage")&&!Launcher?TEXT(" max"):TEXT(""));
        }
        Row.SpellContribution=TEXT(" (")+Contribution+TEXT(" spells)");
    }
    return Rows;
}

namespace
{
const TMap<FString,FString>& ExaminationStatKeys()
{
    static const TMap<FString,FString> Keys={{TEXT("Damage:"),TEXT("Damage")},{TEXT("Damage Bonus:"),TEXT("Damage")},
        {TEXT("Damage Modifier:"),TEXT("Multiplier")},{TEXT("Speed:"),TEXT("Speed")},
        {TEXT("Bonus to Attack Skill:"),TEXT("Attack")},{TEXT("Bonus to Melee Defense:"),TEXT("Defense")},
        {TEXT("Bonus to Missile Defense:"),TEXT("MissileDefense")},{TEXT("Bonus to Magic Defense:"),TEXT("MagicDefense")},
        {TEXT("Armor Level:"),TEXT("Armor")},{TEXT("Base Shield Level:"),TEXT("Armor")},
        {TEXT("Slashing:"),TEXT("Armor13")},{TEXT("Piercing:"),TEXT("Armor14")},{TEXT("Bludgeoning:"),TEXT("Armor15")},
        {TEXT("Cold:"),TEXT("Armor16")},{TEXT("Fire:"),TEXT("Armor17")},{TEXT("Acid:"),TEXT("Armor18")},
        {TEXT("Electric:"),TEXT("Armor19")},{TEXT("Nether:"),TEXT("Armor165")},
        {TEXT("Bonus to Mana Conversion:"),TEXT("ManaConversion")},
        {TEXT(" vs. Monsters:"),TEXT("Element")},{TEXT(" vs. Players:"),TEXT("ElementPvP")}};
    return Keys;
}
}

void ACEAppraisalFormatting::ItemSpellColors(const FACEAppraisalInfo& Info,UACEDatSubsystem* Dat,const FString& Text,TArray<FLinearColor>& Colors)
{
    const auto Rows=EquipmentStatLines(Info,Dat);
    int32 Start=0;
    while(Start<Text.Len())
    {
        int32 End=Text.Find(TEXT("\n"),ESearchCase::CaseSensitive,ESearchDir::FromStart,Start);
        if(End==INDEX_NONE)End=Text.Len();
        const FString Line=Text.Mid(Start,End-Start);
        for(const auto& Pair:ExaminationStatKeys())if(Line.StartsWith(Pair.Key))
        {
            const auto* Row=Rows.FindByPredicate([&](const FACEItemStatLine& R){return R.Key==Pair.Value;});
            if(Row&&Row->bSpellModified)for(int32 I=Start;I<End;++I)Colors[I]=Row->Color;
            break;
        }
        Start=End+1;
    }
}

FString ACEAppraisalFormatting::ItemSpellContributions(const FACEAppraisalInfo& Info,UACEDatSubsystem* Dat,const FString& Text)
{
    const auto Rows=EquipmentStatLines(Info,Dat);
    if(!Rows.ContainsByPredicate([](const FACEItemStatLine& R){return !R.SpellContribution.IsEmpty();}))return Text;
    TArray<FString> Lines;Text.ParseIntoArrayLines(Lines,false);
    for(auto& Line:Lines)for(const auto& Pair:ExaminationStatKeys())if(Line.StartsWith(Pair.Key))
    {
        const auto* Row=Rows.FindByPredicate([&](const FACEItemStatLine& R){return R.Key==Pair.Value;});
        if(Row&&!Row->SpellContribution.IsEmpty())
        {
            const bool Period=Line.RemoveFromEnd(TEXT("."));
            Line+=Row->SpellContribution;if(Period)Line+=TEXT(".");
        }
        break;
    }
    FString Result=FString::Join(Lines,TEXT("\n"));
    if(Text.EndsWith(TEXT("\n"))&&!Result.EndsWith(TEXT("\n")))Result+=TEXT("\n");
    return Result;
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
