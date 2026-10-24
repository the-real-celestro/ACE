#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "Engine/GameInstance.h"
#include "ACEPluginVM.h"

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
void UACEPluginSubsystem::InspectLootItem(int32 Guid)
{
    InspectLootGuid=0;InspectLootVM.Reset();InspectLootWaiting=false;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const auto P=Find(TEXT("ucm"));FACEWorldObject Item;
    if(!P||!P->Enabled||!P->Profile||C->GetSessionState()!=EACESessionState::InWorld
        ||!Guid||!C->GetWorldObject(Guid,Item)||(Item.ItemType&ACEItemType::Creature)||Item.IsCorpse())return;
    const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;
    if(!P->Profile->TryGetArrayField(TEXT("loot_rules"),Rules)||Rules->IsEmpty())return;
    InspectLootGuid=Guid;InspectLootSession=C->GetSession();InspectLootProfile=P->Profile;
    InspectLootDeadline=FPlatformTime::Seconds()+12;
}
void UACEPluginSubsystem::TickLootInspection()
{
    if(!InspectLootGuid)return;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const auto P=Find(TEXT("ucm"));FACEWorldObject Item;
    auto Cancel=[this]{InspectLootGuid=0;InspectLootVM.Reset();InspectLootWaiting=false;};
    if(!P||!P->Enabled||C->GetSessionState()!=EACESessionState::InWorld||C->GetSession()!=InspectLootSession.Pin()
        ||P->Profile!=InspectLootProfile.Pin()||C->GetSelectedObject().Guid!=InspectLootGuid
        ||FPlatformTime::Seconds()>InspectLootDeadline||!C->GetWorldObject(InspectLootGuid,Item)){Cancel();return;}
    if(InspectLootWaiting)
    {
        const auto* A=Appraisals.Find(InspectLootGuid);
        if(!A)return;
        if(!A->bSuccess){Cancel();return;}
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
)lua"),Error)){Cancel();return;}
    }
    auto S=Snapshot();ExtendSnapshot(S);S->SetField(TEXT("clicked_item"),LootItemSnapshot(Item));
    TSharedPtr<FJsonObject> Result;
    if(!InspectLootVM->Step(S,P->Profile,Result,Error)||!Result){Cancel();return;}
    const FString Status=Result->GetStringField(TEXT("status"));
    if(Status==TEXT("pending"))return;
    if(Status==TEXT("identify"))
    {
        InspectLootWaiting=true;C->RequestBackgroundAppraisal(InspectLootGuid);return;
    }
    if(Status==TEXT("match"))
    {
        const FString Name=S->GetObjectField(TEXT("clicked_item"))->GetStringField(TEXT("name"));
        const FString Profile=UCMSectionName(TEXT("Loot"));
        const FString Text=FString::Printf(TEXT("[Loot] %s matches %s / %s (%s)."),*Name,
            *(Profile.IsEmpty()?TEXT("Default"):Profile),*Result->GetStringField(TEXT("label")),*Result->GetStringField(TEXT("action")));
        C->ReceiveChatMessage(Text,TEXT(""),ACEChatMessageType::System,INDEX_NONE,Name);
    }
    Cancel();
}
