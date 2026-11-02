#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "Engine/GameInstance.h"
#include "ACEPluginVM.h"
#include "ACEDatSubsystem.h"
#include "UI/ACERetailObjectNames.h"
#include "ACESession.h"
#include "ACEVTRegex.h"

bool UACEPluginSubsystem::ValidatePluginChatFilters(const TArray<TSharedPtr<FJsonValue>>& Values,
    TArray<FACEPluginChatFilter>& Filters,FString& Error) const
{
    // Rules run before display without invoking Lua. Patterns use the same
    // bounded work/stack matcher as loot profiles.
    if(Values.Num()>32){Error=TEXT("At most 32 chat filters per plugin");return false;}
    TSet<FString> Ids;
    for(const auto& Value:Values)
    {
        if(!Value||Value->Type!=EJson::Object){Error=TEXT("Chat filters must be objects");return false;}
        const auto O=Value->AsObject();FACEPluginChatFilter F;
        for(const auto& Field:O->Values)
        {
            const FString Key(Field.Key);const auto& V=Field.Value;
            if(Key==TEXT("id")||Key==TEXT("contains")||Key==TEXT("prefix")||Key==TEXT("exact")||Key==TEXT("sender")||Key==TEXT("sender_type")||Key==TEXT("pattern")||Key==TEXT("action")||Key==TEXT("color"))
            {
                FString Text;
                if(!V->TryGetString(Text)||Text.Len()>(Key==TEXT("id")?64:256)){Error=TEXT("Chat filter text exceeds its limit or is not a string");return false;}
                if(Key==TEXT("id"))F.Id=Text;
                else if(Key==TEXT("contains"))F.Contains=Text;
                else if(Key==TEXT("prefix"))F.Prefix=Text;
                else if(Key==TEXT("exact"))F.Exact=Text;
                else if(Key==TEXT("sender")){F.Sender=Text;F.HasSender=true;}
                else if(Key==TEXT("sender_type"))
                {if(!TArray<FString>{TEXT("player"),TEXT("npc"),TEXT("vendor"),TEXT("server"),TEXT("unknown")}.Contains(Text)){Error=TEXT("Invalid sender_type");return false;}F.SenderType=Text;}
                else if(Key==TEXT("pattern")){if(!ACEVTRegex::Validate(Text,Error))return false;F.Pattern=Text;}
                else if(Key==TEXT("action")){if(!TArray<FString>{TEXT("hide"),TEXT("show"),TEXT("color")}.Contains(Text)){Error=TEXT("Invalid chat rule action");return false;}F.Action=Text;}
                else
                {
                    static const TMap<FString,FLinearColor> Named={{TEXT("red"),FLinearColor::Red},{TEXT("green"),FLinearColor::Green},{TEXT("blue"),FLinearColor::Blue},{TEXT("yellow"),FLinearColor::Yellow},{TEXT("white"),FLinearColor::White}};
                    if(const auto* Color=Named.Find(Text))F.Color=*Color;
                    else if(Text.StartsWith(TEXT("#"))&&Text.Len()==7){for(int32 I=1;I<Text.Len();++I)if(!FChar::IsHexDigit(Text[I])){Error=TEXT("Invalid hex color");return false;}F.Color=FLinearColor(FColor::FromHex(Text));}
                    else {Error=TEXT("Color must be red, green, blue, yellow, white or #RRGGBB");return false;}
                }
            }
            else if(Key==TEXT("case_sensitive"))
            {
                if(V->Type!=EJson::Boolean){Error=TEXT("case_sensitive must be boolean");return false;}
                F.CaseSensitive=V->AsBool();
            }
            else if(Key==TEXT("window")||Key==TEXT("chat_type")||Key==TEXT("seconds"))
            {
                double Number=0;
                if(V->Type!=EJson::Number||!V->TryGetNumber(Number)||!FMath::IsFinite(Number))
                {Error=TEXT("Invalid chat filter numeric value");return false;}
                if(Key==TEXT("seconds"))
                {
                    if(Number<=0||Number>300){Error=TEXT("Chat filter seconds must be greater than 0 and at most 300");return false;}
                    F.Expires=FPlatformTime::Seconds()+Number;
                }
                else
                {
                    if(Number!=FMath::FloorToDouble(Number)||Number<(Key==TEXT("window")?-1:0)||Number>(Key==TEXT("window")?4:63))
                    {Error=TEXT("Chat filter window must be -1..4; chat_type must be 0..63");return false;}
                    if(Key==TEXT("window"))F.Windows=Number<0?0x1f:uint8(1u<<int32(Number));
                    else F.ChatType=int32(Number);
                }
            }
            else {Error=TEXT("Unknown chat filter field: ")+Key;return false;}
        }
        if(F.Id.IsEmpty()||Ids.Contains(F.Id)||(F.Contains.IsEmpty()&&F.Prefix.IsEmpty()&&F.Exact.IsEmpty()&&F.Pattern.IsEmpty()&&F.SenderType.IsEmpty()&&!F.HasSender&&F.ChatType==INDEX_NONE))
        {Error=TEXT("Chat filters need unique IDs and at least one text, sender or chat_type predicate");return false;}
        if(F.Color.IsSet()&&!O->HasField(TEXT("action")))F.Action=TEXT("color");
        Ids.Add(F.Id);Filters.Add(MoveTemp(F));
    }
    return true;
}

bool UACEPluginSubsystem::MatchesPluginChat(const FACEPluginChatFilter& F,const FString& Text,const FString& Sender,int32 Type,const FString& SenderType)
{
    if((F.Expires>0&&FPlatformTime::Seconds()>=F.Expires)||(F.ChatType!=INDEX_NONE&&F.ChatType!=Type)
        ||(!F.SenderType.IsEmpty()&&F.SenderType!=SenderType))return false;
    const auto Case=F.CaseSensitive?ESearchCase::CaseSensitive:ESearchCase::IgnoreCase;
    if((F.HasSender&&!Sender.Equals(F.Sender,Case))||(!F.Contains.IsEmpty()&&!Text.Contains(F.Contains,Case))
        ||(!F.Prefix.IsEmpty()&&!Text.StartsWith(F.Prefix,Case))||(!F.Exact.IsEmpty()&&!Text.Equals(F.Exact,Case)))return false;
    FString Error;return F.Pattern.IsEmpty()||ACEVTRegex::Test((F.CaseSensitive?FString():FString(TEXT("(?i)")))+F.Pattern,Text.Left(4096),Error);
}

FString UACEPluginSubsystem::PluginChatSenderType(const FString& Sender) const
{
    if(Sender.IsEmpty())return TEXT("server");
    const auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto Session=C?C->GetSession():nullptr;
    if(Session)
    {
        if(Sender.Equals(TEXT("You"),ESearchCase::IgnoreCase)||Sender.Equals(Session->GetPlayerName(),ESearchCase::IgnoreCase))return TEXT("player");
        for(const auto& Pair:Session->GetWorldObjects())if(Pair.Value.Name.Equals(Sender,ESearchCase::IgnoreCase))
        {if(Pair.Value.bIsPlayer)return TEXT("player");if(Pair.Value.IsVendor())return TEXT("vendor");if(Pair.Value.IsGiveOrCreatureTarget())return TEXT("npc");}
        for(const auto& Fellow:C->GetFellowship().Members)if(Fellow.Name.Equals(Sender,ESearchCase::IgnoreCase))return TEXT("player");
    }
    return TEXT("unknown");
}

FACEPluginChatPresentation UACEPluginSubsystem::PresentPluginChat(const FString& Text,const FString& Sender,int32 Type) const
{
    FACEPluginChatPresentation Result;
    const auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(C)
    {
        Result.Forced=C->CurrentChatWindows;
        if(C->CurrentChatColor.IsSet())for(int32 W=0;W<5;++W)Result.Colors.Add(W,C->CurrentChatColor.GetValue());
    }
    if(!C||C->GetSessionState()!=EACESessionState::InWorld)return Result;
    const FString SenderType=PluginChatSenderType(Sender);
    for(const auto& P:Plugins)
    {
        if(!P->Enabled||!P->Running||!P->HasPresentation()||!P->Permissions.Contains(TEXT("chat_filter"))
            ||P->Player!=C->GetPlayerGuid()||P->Server!=C->GetServerName()||P->VMSession.Pin()!=C->GetSession())continue;
        for(const auto& F:P->ChatFilters)
        {
            if(!MatchesPluginChat(F,Text,Sender,Type,SenderType))continue;
            if(F.Action==TEXT("hide"))Result.Hidden|=F.Windows;
            else if(F.Action==TEXT("show"))Result.Forced|=F.Windows;
            if(F.Color.IsSet())for(int32 W=0;W<5;++W)if(F.Windows&(1u<<W))Result.Colors.Add(W,F.Color.GetValue());
        }
    }
    // Hiding wins conflicts between plugins; rules never expose a squelched line.
    Result.Forced&=~Result.Hidden;return Result;
}

// This VM only evaluates the shared rule predicate. It cannot issue game actions.
void UACEPluginSubsystem::InspectLootItem(int32 Guid, TSharedPtr<FJsonObject> TestRule)
{
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const double Now=FPlatformTime::Seconds();
    if(!TestRule&&Guid&&(Guid==InspectLootGuid||(Guid==LastInspectedItem&&Now-LastInspectedAt<.35)))return;
    // A second selection callback must not flush a provisional line and start over.

    InspectLootGuid=0;InspectLootVM.Reset();InspectLootWaiting=false;InspectLootTestPolicy.Reset();
    const auto P=Find(TEXT("ucm"));FACEWorldObject Item;
    if(!P||!P->Profile||C->GetSessionState()!=EACESessionState::InWorld
        ||!Guid||!C->GetWorldObject(Guid,Item)||(Item.ItemType&ACEItemType::Creature)||Item.IsCorpse())return;
    if(TestRule)
    {
        InspectLootTestPolicy=MakeShared<FJsonObject>();InspectLootTestPolicy->Values=P->Profile->Values;
        InspectLootTestPolicy->SetArrayField(TEXT("loot_rules"),{MakeShared<FJsonValueObject>(TestRule)});
    }
    InspectLootGuid=Guid;InspectLootSession=C->GetSession();InspectLootProfile=P->Profile;
    InspectLootDeadline=FPlatformTime::Seconds()+12;
    const auto* A=Appraisals.Find(Guid);
    if((Item.ItemType&(ACEItemType::MeleeWeapon|ACEItemType::MissileWeapon|ACEItemType::Armor|ACEItemType::Caster))&&(!A||!A->bSuccess))
    {InspectLootWaiting=true;C->RequestBackgroundAppraisal(Guid);}
}
void UACEPluginSubsystem::TickLootInspection()
{
    if(!InspectLootGuid)return;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const auto P=Find(TEXT("ucm"));FACEWorldObject Item;
    auto Cancel=[this]{InspectLootGuid=0;InspectLootVM.Reset();InspectLootWaiting=false;InspectLootTestPolicy.Reset();};
    if(!P||C->GetSessionState()!=EACESessionState::InWorld||C->GetSession()!=InspectLootSession.Pin()
        ||P->Profile!=InspectLootProfile.Pin()||C->GetSelectedObject().Guid!=InspectLootGuid
        ||!C->GetWorldObject(InspectLootGuid,Item)){Cancel();return;}
    if(FPlatformTime::Seconds()>InspectLootDeadline){PrintLootInspection(Item);Cancel();return;}
    if(InspectLootWaiting)
    {
        const auto* A=Appraisals.Find(InspectLootGuid);
        if(!A)return;
        if(!A->bSuccess){PrintLootInspection(Item);Cancel();return;}
        InspectLootWaiting=false;InspectLootVM.Reset();
    }
    FString Error;
    if(!InspectLootVM)
    {
        InspectLootVM=MakeShared<FACEPluginVM>();
        if(!InspectLootVM->Load(TEXT(R"lua(
local index,scan=1,{}
return function(s,p)
 local item=s.clicked_item
 while index<=#(p.loot_rules or {}) do
  if not workavailable() then return {status='pending'} end
  local rule=p.loot_rules[index]
  local result=lootmatch(rule,item,s,scan)
  if result=='pending' then return {status='pending'} end
  if result==nil then return {status='identify'} end
  if result then return {status='match',label=rule.label or ('Rule '..index),action=rule.action or 'keep'} end
  index=index+1;scan={}
 end
 return {status='none'}
end
)lua"),Error)){Notice=TEXT("Loot check failed: ")+Error;PrintLootInspection(Item);Cancel();return;}
    }
    auto S=Snapshot();ExtendSnapshot(S);S->SetField(TEXT("clicked_item"),LootItemSnapshot(Item));
    TSharedPtr<FJsonObject> Result;
    if(!InspectLootVM->Step(S,InspectLootTestPolicy?InspectLootTestPolicy:P->Profile,Result,Error)||!Result)
    {Notice=TEXT("Loot check failed: ")+Error;PrintLootInspection(Item);Cancel();return;}
    const FString Status=Result->GetStringField(TEXT("status"));
    if(Status==TEXT("pending"))return;
    if(Status==TEXT("identify"))
    {
        InspectLootWaiting=true;C->RequestBackgroundAppraisal(InspectLootGuid);return;
    }
    PrintLootInspection(Item,Result);
    Cancel();
}

void UACEPluginSubsystem::PrintLootInspection(const FACEWorldObject& Item,const TSharedPtr<FJsonObject>& Result)
{
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const auto J=LootItemSnapshot(Item)->AsObject();
    const FString Name=J->GetStringField(TEXT("name"));
    const bool Matched=Result&&Result->GetStringField(TEXT("status"))==TEXT("match");
    FString Text=(InspectLootTestPolicy?TEXT("[Loot test] "):Matched?TEXT("[MATCH] "):TEXT("[Item] "))+Name;
    const auto* A=Appraisals.Find(Item.Guid);
    if(A&&A->bSuccess)
    {
        if(A->bHasWeaponProfile)
        {
            const double Low=A->Damage*(1.-FMath::Clamp(double(A->DamageVariance),0.,1.));
            Text+=FString::Printf(TEXT(" | Dmg %g–%d | Speed %d"),Low,A->Damage,A->WeaponTime);
            Text+=FString::Printf(TEXT(" | Attack %+.1f%% | Damage mod x%.2f"),(A->WeaponOffense-1.)*100.,A->WeaponDamageMod);
            FString DamageTypes;
            for(const auto& T:{TPair<int32,const TCHAR*>(1,TEXT("Slash")),{2,TEXT("Pierce")},{4,TEXT("Bludgeon")},{8,TEXT("Cold")},{16,TEXT("Fire")},{32,TEXT("Acid")},{64,TEXT("Electric")},{1024,TEXT("Nether")}})
                if(A->DamageType&T.Key){if(!DamageTypes.IsEmpty())DamageTypes+=TEXT("/");DamageTypes+=T.Value;}
            if(!DamageTypes.IsEmpty())Text+=TEXT(" | ")+DamageTypes;
            FString Skill;uint32 Icon=0;
            if(GetGameInstance()->GetSubsystem<UACEDatSubsystem>()->TryGetSkillInfo(A->WeaponSkill,Skill,Icon)&&!Skill.IsEmpty())Text+=TEXT(" | ")+Skill;
        }
        for(const auto& E:{TPair<int32,const TCHAR*>(29,TEXT("Melee D")),{149,TEXT("Missile D")},{150,TEXT("Magic D")},{144,TEXT("Mana C")}})
            if(const double* V=A->FloatProperties.Find(E.Key))Text+=FString::Printf(TEXT(" | %s %+.1f%%"),E.Value,(*V-1.)*100.);
        for(const auto& E:{TPair<int32,const TCHAR*>(136,TEXT("Crit multiplier")),{138,TEXT("Slayer mod")},{147,TEXT("Crit chance")}})
            if(const double* V=A->FloatProperties.Find(E.Key))Text+=FString::Printf(TEXT(" | %s %g"),E.Value,*V);
        if(const int32* Bonus=A->IntProperties.Find(204))Text+=FString::Printf(TEXT(" | Damage bonus %d"),*Bonus);
        if(const auto* AL=A->IntProperties.Find(28))Text+=FString::Printf(TEXT(" | AL %d"),*AL);
        if(const auto* Mod=A->FloatProperties.Find(152))Text+=FString::Printf(TEXT(" | Element x%.2f"),*Mod);
        if(const auto* Rating=A->IntProperties.Find(307))Text+=FString::Printf(TEXT(" | Rating %d"),*Rating);
        double Workmanship=0;if(J->TryGetNumberField(TEXT("workmanship"),Workmanship))Text+=FString::Printf(TEXT(" | Work %g"),Workmanship);
    }
    else if(Item.ItemType&(ACEItemType::MeleeWeapon|ACEItemType::MissileWeapon|ACEItemType::Armor|ACEItemType::Caster))Text+=TEXT(" | Not appraised");
    if(Item.StackSize>1)Text+=FString::Printf(TEXT(" | Count %d"),Item.StackSize);
    Text+=FString::Printf(TEXT(" | Value %d | Burden %d"),Item.Value,Item.Burden);
    if(Matched)
    {
        const FString Profile=UCMSectionName(TEXT("Loot"));
        Text+=FString::Printf(TEXT(" | Matches %s / %s (%s)"),*(Profile.IsEmpty()?TEXT("Default"):Profile),*Result->GetStringField(TEXT("label")),*Result->GetStringField(TEXT("action")));
    }
    else if(InspectLootTestPolicy)Text+=TEXT(" | Does not match this rule");
    LastInspectedItem=Item.Guid;LastInspectedAt=FPlatformTime::Seconds();
    C->ReceiveChatMessage(Text,TEXT(""),Matched?ACEChatMessageType::Broadcast:ACEChatMessageType::Appraisal,INDEX_NONE,0,Matched?FLinearColor(.15f,1.f,.15f):FLinearColor::White,Matched?Text:FString());
}
uint8 UACEPluginSubsystem::FilterPluginChatWindows(const FString& Text,const FString& Sender,int32 Type) const
{return PresentPluginChat(Text,Sender,Type).Hidden;}
