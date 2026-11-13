#include "ACELootTargets.h"

FString FACELootTemplate::Signature()const
{
    auto Sorted=Skills;Sorted.Sort();FString Key=FString::FromInt(Primary)+TEXT(":");
    for(int32 Skill:Sorted)Key+=FString::FromInt(Skill)+TEXT(",");return Key;
}
FACEPlayerVitals FACELootTemplate::Character()const
{
    FACEPlayerVitals C;C.bValid=true;C.Level=275;
    for(int32 SkillId:Skills){auto& S=C.Skills.AddDefaulted_GetRef();S.SkillId=SkillId;S.AdvancementClass=SkillId==Primary?3:2;S.Base=S.Current=SkillId==Primary?500:300;}
    return C;
}
namespace
{
bool ReadTemplate(const TSharedPtr<FJsonObject>& J,FACELootTemplate& T)
{
    double Primary=0;const TArray<TSharedPtr<FJsonValue>>* Skills=nullptr;
    if(!J||!J->TryGetStringField(TEXT("id"),T.Id)||!T.Id.StartsWith(TEXT("custom:"))||T.Id.Len()>64||T.Id.Len()<=7
        ||!J->TryGetStringField(TEXT("name"),T.Name)||T.Name.TrimStartAndEnd().IsEmpty()||T.Name.Len()>64
        ||!J->TryGetNumberField(TEXT("primary"),Primary)||!FMath::IsFinite(Primary)||Primary<1||Primary>54||Primary!=FMath::FloorToDouble(Primary)
        ||!J->TryGetArrayField(TEXT("skills"),Skills)||Skills->IsEmpty()||Skills->Num()>54)return false;
    T.Name=T.Name.TrimStartAndEnd();T.Primary=int32(Primary);T.Skills.Empty();
    for(const auto& Value:*Skills)
    {
        double Id=0;if(!Value->TryGetNumber(Id)||!FMath::IsFinite(Id)||Id<1||Id>54||Id!=FMath::FloorToDouble(Id)||T.Skills.Contains(int32(Id)))return false;
        T.Skills.Add(int32(Id));
    }
    return T.Skills.Contains(T.Primary);
}
}
const TArray<FACELootTemplate>& ACELootTargets::Presets()
{
    static const TArray<FACELootTemplate> Values=[]
    {
        TArray<FACELootTemplate> Out;
        auto Add=[&](const TCHAR* Id,const TCHAR* Name,int32 Main)
        {
            FACELootTemplate T;T.Id=Id;T.Name=Name;T.Primary=Main;
            T.Skills={6,7,14,15,16,21,31,32,33};T.Skills.AddUnique(Main);
            if(Main==44||Main==45||Main==46){T.Skills.Append({48,49,50,51,52});}
            if(Main==41)T.Skills.Append({50,51,52});
            if(Main==34||Main==43||Main==54)T.Skills.AddUnique(48);
            Out.Add(MoveTemp(T));
        };
        Add(TEXT("heavy"),TEXT("Heavy melee"),44);Add(TEXT("light"),TEXT("Light melee"),45);
        Add(TEXT("finesse"),TEXT("Finesse melee"),46);Add(TEXT("twohand"),TEXT("Two-handed melee"),41);
        Add(TEXT("missile"),TEXT("Archer / missile"),47);Add(TEXT("war"),TEXT("War mage"),34);
        Add(TEXT("void"),TEXT("Void mage"),43);Add(TEXT("summoning"),TEXT("Summoner"),54);
        Add(TEXT("sneak"),TEXT("Sneak attack / Deception"),46);Out.Last().Skills.AddUnique(20);
        Add(TEXT("weapon_tinkering"),TEXT("Weapon tinkerer"),28);Add(TEXT("armor_tinkering"),TEXT("Armor tinkerer"),29);
        Add(TEXT("magic_tinkering"),TEXT("Magic item tinkerer"),30);Add(TEXT("item_tinkering"),TEXT("Item tinkerer"),18);
        Add(TEXT("alchemy"),TEXT("Alchemist"),38);Add(TEXT("cooking"),TEXT("Cook"),39);
        Add(TEXT("fletching"),TEXT("Fletcher"),37);Add(TEXT("salvaging"),TEXT("Salvager"),40);
        return Out;
    }();return Values;
}
bool ACELootTargets::Resolve(const FString& Id,const TSharedPtr<FJsonObject>& Profile,FACELootTemplate& Out)
{
    for(const auto& T:Presets())if(T.Id==Id){Out=T;return true;}
    const TArray<TSharedPtr<FJsonValue>>* Templates=nullptr;
    if(Profile&&Profile->TryGetArrayField(TEXT("loot_rating_templates"),Templates))for(const auto& V:*Templates)
        if(V->Type==EJson::Object){FACELootTemplate T;if(ReadTemplate(V->AsObject(),T)&&T.Id==Id){Out=MoveTemp(T);return true;}}
    return false;
}
TSharedPtr<FJsonObject> ACELootTargets::ToJson(const FACELootTemplate& T)
{
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),T.Id);J->SetStringField(TEXT("name"),T.Name);J->SetNumberField(TEXT("primary"),T.Primary);
    TArray<TSharedPtr<FJsonValue>> Skills;for(int32 Id:T.Skills)Skills.Add(MakeShared<FJsonValueNumber>(Id));J->SetArrayField(TEXT("skills"),Skills);return J;
}
bool ACELootTargets::Validate(const TSharedPtr<FJsonObject>& Profile)
{
    if(!Profile)return false;
    const TArray<TSharedPtr<FJsonValue>>* Templates=nullptr;TSet<FString> Ids,Names;
    if(Profile->HasField(TEXT("loot_rating_templates")))
    {
        if(!Profile->TryGetArrayField(TEXT("loot_rating_templates"),Templates)||Templates->Num()>16)return false;
        for(const auto& V:*Templates)
        {
            FACELootTemplate T;if(V->Type!=EJson::Object||!ReadTemplate(V->AsObject(),T)||Ids.Contains(T.Id)||Names.Contains(T.Name.ToLower()))return false;
            Ids.Add(T.Id);Names.Add(T.Name.ToLower());
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;
    if(Profile->TryGetArrayField(TEXT("loot_rules"),Rules))for(const auto& V:*Rules)
    {
        if(V->Type!=EJson::Object)return false;const auto R=V->AsObject();FString Mode=TEXT("current");
        if(R->HasField(TEXT("rating_mode"))&&(!R->TryGetStringField(TEXT("rating_mode"),Mode)||!(Mode==TEXT("current")||Mode==TEXT("universal")||Mode==TEXT("templates"))))return false;
        if(R->HasField(TEXT("rating_targets")))
        {
            const TArray<TSharedPtr<FJsonValue>>* Targets=nullptr;
            if(!R->TryGetArrayField(TEXT("rating_targets"),Targets)||Targets->Num()>Presets().Num()+16)return false;TSet<FString> Seen;
            for(const auto& Target:*Targets){FString Id;FACELootTemplate T;if(!Target->TryGetString(Id)||Seen.Contains(Id)||!Resolve(Id,Profile,T))return false;Seen.Add(Id);}
        }
        if(Mode==TEXT("templates"))
        {
            const TArray<TSharedPtr<FJsonValue>>* Targets=nullptr;
            if(!R->TryGetArrayField(TEXT("rating_targets"),Targets)||Targets->IsEmpty())return false;
        }
    }
    return true;
}
