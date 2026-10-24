#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "Engine/GameInstance.h"
#include "ACEPluginVM.h"
#include "ACEDatSubsystem.h"
#include "UI/ACERetailObjectNames.h"

bool UACEPluginSubsystem::ValidatePluginChatFilters(const TArray<TSharedPtr<FJsonValue>>& Values,
    TArray<FACEPluginChatFilter>& Filters,FString& Error) const
{
    // Literal matching only on the receive path: no Lua calls or regex backtracking
    // while a large command response is being delivered to the UI.
    if(Values.Num()>32){Error=TEXT("At most 32 chat filters per plugin");return false;}
    TSet<FString> Ids;
    for(const auto& Value:Values)
    {
        if(!Value||Value->Type!=EJson::Object){Error=TEXT("Chat filters must be objects");return false;}
        const auto O=Value->AsObject();FACEPluginChatFilter F;
        for(const auto& Field:O->Values)
        {
            const FString Key(Field.Key);const auto& V=Field.Value;
            if(Key==TEXT("id")||Key==TEXT("contains")||Key==TEXT("prefix")||Key==TEXT("exact")||Key==TEXT("sender"))
            {
                FString Text;
                if(!V->TryGetString(Text)||Text.Len()>(Key==TEXT("id")?64:256)){Error=TEXT("Chat filter text exceeds its limit or is not a string");return false;}
                if(Key==TEXT("id"))F.Id=Text;
                else if(Key==TEXT("contains"))F.Contains=Text;
                else if(Key==TEXT("prefix"))F.Prefix=Text;
                else if(Key==TEXT("exact"))F.Exact=Text;
                else {F.Sender=Text;F.HasSender=true;}
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
        if(F.Id.IsEmpty()||Ids.Contains(F.Id)||(F.Contains.IsEmpty()&&F.Prefix.IsEmpty()&&F.Exact.IsEmpty()))
        {Error=TEXT("Chat filters need unique IDs and at least one nonempty contains, prefix or exact match");return false;}
        Ids.Add(F.Id);Filters.Add(MoveTemp(F));
    }
    return true;
}

uint8 UACEPluginSubsystem::FilterPluginChatWindows(const FString& Text,const FString& Sender,int32 Type) const
{
    const auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(!C||C->GetSessionState()!=EACESessionState::InWorld)return 0;
    const double Now=FPlatformTime::Seconds();uint8 Hidden=0;
    for(const auto& P:Plugins)
    {
        if(!P->Enabled||!P->Running||!P->HasPresentation()||!P->Permissions.Contains(TEXT("chat_filter"))
            ||P->Player!=C->GetPlayerGuid()||P->Server!=C->GetServerName()||P->VMSession.Pin()!=C->GetSession())continue;
        for(const auto& F:P->ChatFilters)
        {
            if((F.Expires>0&&Now>=F.Expires)||(F.ChatType!=INDEX_NONE&&F.ChatType!=Type)||(Hidden&F.Windows)==F.Windows)continue;
            const auto Case=F.CaseSensitive?ESearchCase::CaseSensitive:ESearchCase::IgnoreCase;
            if((F.HasSender&&!Sender.Equals(F.Sender,Case))||(!F.Contains.IsEmpty()&&!Text.Contains(F.Contains,Case))
                ||(!F.Prefix.IsEmpty()&&!Text.StartsWith(F.Prefix,Case))||(!F.Exact.IsEmpty()&&!Text.Equals(F.Exact,Case)))continue;
            Hidden|=F.Windows;
            if(Hidden==0x1f)return Hidden;
        }
    }
    return Hidden;
}

// This VM only evaluates the shared rule predicate. It cannot issue game actions.
void UACEPluginSubsystem::InspectLootItem(int32 Guid, TSharedPtr<FJsonObject> TestRule)
{
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    FACEWorldObject Previous;
    if(InspectLootGuid&&C->GetWorldObject(InspectLootGuid,Previous))PrintLootInspection(Previous);
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
    FString Text=(InspectLootTestPolicy?TEXT("[Loot test] "):TEXT("[Item] "))+Name;
    const auto* A=Appraisals.Find(Item.Guid);
    if(A&&A->bSuccess)
    {
        if(A->bHasWeaponProfile)
        {
            const double Low=A->Damage*(1.-FMath::Clamp(double(A->DamageVariance),0.,1.));
            Text+=FString::Printf(TEXT(" | Dmg %g–%d | Speed %d"),Low,A->Damage,A->WeaponTime);
            FString Skill;uint32 Icon=0;
            if(GetGameInstance()->GetSubsystem<UACEDatSubsystem>()->TryGetSkillInfo(A->WeaponSkill,Skill,Icon)&&!Skill.IsEmpty())Text+=TEXT(" | ")+Skill;
        }
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
    C->ReceiveChatMessage(Text,TEXT(""),Matched?ACEChatMessageType::Broadcast:ACEChatMessageType::Appraisal,INDEX_NONE,Matched?Text:FString());
}
