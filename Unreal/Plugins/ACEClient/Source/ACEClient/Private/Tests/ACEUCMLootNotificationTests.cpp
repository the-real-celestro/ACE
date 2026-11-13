#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACETestDatDirectory.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMLootNotificationTest,"ACE.Plugins.LootNotifications",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMLootNotificationTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
    Context.SetCurrentWorld(World);Context.OwningGameInstance=GI;GI->OnWorldChanged(nullptr,World);GI->Init();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    auto Parse=[](const TCHAR* Text){TSharedPtr<FJsonObject> J;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J);return J;};
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();auto P=Host->Find(TEXT("ucm"));
    if(!TestTrue(TEXT("Bundled UCM exists"),P.IsValid()))return false;
    P->Enabled=true;P->Running=false;
    P->Profile=Parse(TEXT(R"({"loot_rules":[{"name":"Ring","label":"Fine rings","action":"salvage","min_workmanship":7},{"label":"Fallback","action":"keep"}]})"));
    auto* Client=GI->GetSubsystem<UACEClientSubsystem>();
    auto& Session=*Client->Session;Session.State=EACESessionState::InWorld;Session.PlayerGuid=1;
    Session.PlayerVitals.bValid=true;Session.PlayerVitals.Health=100;
    FACEWorldObject Ring;Ring.Guid=701;Ring.WeenieClassId=701;Ring.ItemType=ACEItemType::Jewelry;Ring.Name=TEXT("Ring");
    Session.WorldObjects.Add(Ring.Guid,Ring);Session.SelectedObject.Guid=Ring.Guid;
    Client->ChatHistory.Reset();
    Host->TickLootNotifications();
    TestEqual(TEXT("Missing ID cannot fall through to a later rule"),Client->ChatHistory.Num(),0);
    TestTrue(TEXT("Read-only selection never requests an appraisal"),Host->AppraisalRequests.IsEmpty()&&Session.CachedC2SPackets.IsEmpty());
    FACEAppraisalInfo Info;Info.ObjectGuid=Ring.Guid;Info.bSuccess=true;Info.IntProperties.Add(105,8);Info.IntProperties.Add(170,1);
    Host->ObserveAppraisal(Info);Host->TickLootNotifications();
    if(TestEqual(TEXT("Assessment prints one match while UCM is stopped"),Client->ChatHistory.Num(),1))
    {
        TestTrue(TEXT("Message includes item, first rule and action"),Client->ChatHistory.Last().Text.Contains(TEXT("Ring matches rule \"Fine rings\" (salvage)")));
        TestEqual(TEXT("Item ID matches use the plugin chat filter"),Client->ChatHistory.Last().FilterType,ACEChatMessageType::Plugin);
    }
    const auto Serial=Host->LootMatchSerial;
    for(int32 I=0;I<20;++I)Host->TickLootNotifications();
    TestEqual(TEXT("Idle frames do not rebuild a snapshot or rerun rules"),Host->LootMatchSerial,Serial);
    Host->ObserveAppraisal(Info);Host->TickLootNotifications();
    TestEqual(TEXT("Repeated assessment suppresses duplicate chat"),Client->ChatHistory.Num(),1);
    Session.SelectedObject.Guid=0;Host->TickLootNotifications();Session.SelectedObject.Guid=Ring.Guid;Host->TickLootNotifications();
    TestEqual(TEXT("Explicit reselection can test the item again"),Client->ChatHistory.Num(),2);
    Ring.Guid=702;Session.WorldObjects.Add(Ring.Guid,Ring);Info.ObjectGuid=Ring.Guid;
    Host->ObserveAppraisal(Info);Host->TickLootNotifications();
    TestEqual(TEXT("Successfully assessing an unselected item also reports its rule"),Client->ChatHistory.Num(),3);
    Host->QueueLootMatch(Ring.Guid);
    P->Profile->SetBoolField(TEXT("loot_notify_match"),false);Host->TickLootNotifications();
    TestFalse(TEXT("Disabling notifications releases their VM"),Host->LootMatchVM.IsValid());
    TestTrue(TEXT("Disabling notifications discards already queued matches"),Host->LootMatchQueue.IsEmpty());
    Host->ObserveAppraisal(Info);Host->TickLootNotifications();
    TestEqual(TEXT("Disabled selected/assessed notifications stay silent"),Client->ChatHistory.Num(),3);
    P->Profile=Parse(TEXT(R"({"loot_rules":[{"label":"Ignore rings","action":"skip","name":"Ring"},{"label":"Later keep","action":"keep"}]})"));
    Host->TickLootNotifications();
    if(TestEqual(TEXT("New profile re-evaluates current selection"),Client->ChatHistory.Num(),4))
        TestTrue(TEXT("Skip is a real first-match result"),Client->ChatHistory.Last().Text.Contains(TEXT("Ignore rings\" (skip)")));
    // Large imported profiles must cooperate across callbacks and remain
    // bounded. No full inventory, routes or meta libraries enter this VM.
    TArray<TSharedPtr<FJsonValue>> Rules;
    for(int32 I=0;I<600;++I)Rules.Add(MakeShared<FJsonValueObject>(Parse(TEXT(R"({"name":"Not this item","action":"keep"})"))));
    Rules.Add(MakeShared<FJsonValueObject>(Parse(TEXT(R"({"label":"Last rule","action":"keep"})"))));
    P->Profile=MakeShared<FJsonObject>();P->Profile->SetArrayField(TEXT("loot_rules"),Rules);
    Host->TickLootNotifications();TestTrue(TEXT("Large scan yields with its cursor intact"),Host->LootMatchSnapshot.IsValid());
    TestEqual(TEXT("Incomplete scans do not print a result"),Client->ChatHistory.Num(),4);
    for(int32 I=0;I<10&&Host->LootMatchSnapshot;++I)Host->TickLootNotifications();
    TestFalse(TEXT("Cooperative scan completes"),Host->LootMatchSnapshot.IsValid());
    if(TestEqual(TEXT("Cooperative scan prints once"),Client->ChatHistory.Num(),5))
        TestTrue(TEXT("Cooperative scan reaches the final rule"),Client->ChatHistory.Last().Text.Contains(TEXT("Last rule")));
    for(int32 I=1000;I<2000;++I)Host->QueueLootMatch(I);
    TestTrue(TEXT("Assessment burst queue is bounded"),Host->LootMatchQueue.Num()<=32);
    Session.State=EACESessionState::Disconnected;Host->TickLootNotifications();
    TestTrue(TEXT("Logout releases jobs, history and VM"),Host->LootMatchQueue.IsEmpty()&&Host->LootMatchReported.IsEmpty()&&!Host->LootMatchVM);
    Session.State=EACESessionState::InWorld;

    // Exercise the actual appraisal broadcast: the background marker must still
    // be present when UCM observes the reply, then be consumed by the client.
    {
    P->Running=true;P->Profile=Parse(TEXT(R"({"salvage_inventory":true,"loot_rules":[{"label":"Inventory salvage","action":"salvage"}]})"));
    Session.SelectedObject.Guid=0;Host->TickLootNotifications();
    FACEWorldObject Bag;Bag.Guid=800;Bag.ItemType=ACEItemType::Container;Bag.ContainerId=Session.PlayerGuid;Session.WorldObjects.Add(Bag.Guid,Bag);
    for(int Location=0;Location<3;++Location)
    {
        FACEWorldObject Owned=Ring;Owned.Guid=810+Location;Owned.ContainerId=Location==0?Session.PlayerGuid:Location==1?Bag.Guid:0;
        if(Location==2){Owned.WielderId=Session.PlayerGuid;Owned.CurrentWieldedLocation=1;}
        Session.WorldObjects.Add(Owned.Guid,Owned);Info.ObjectGuid=Owned.Guid;
        const int32 Before=Client->ChatHistory.Num();
        Client->RequestBackgroundAppraisal(Owned.Guid);Session.OnAppraisal.Broadcast(Info);Host->TickLootNotifications();
        TestEqual(TEXT("Automatic appraisal of owned gear never announces a match"),Client->ChatHistory.Num(),Before);
        TestTrue(TEXT("Silent assessment remains available to salvage rules"),Host->Appraisals.Contains(Owned.Guid)&&Host->Appraisals[Owned.Guid].bSuccess);
        TestFalse(TEXT("Client consumes background marker after UCM observes it"),Client->BackgroundAppraisals.Contains(Owned.Guid));
        Session.SelectedObject.Guid=Owned.Guid;Host->TickLootNotifications();
        TestEqual(TEXT("Manual selection can still test a silently assessed possession"),Client->ChatHistory.Num(),Before+1);
        Session.SelectedObject.Guid=0;Host->TickLootNotifications();
    }
    FACEWorldObject Owned=Ring;Owned.Guid=820;Owned.ContainerId=Bag.Guid;Session.WorldObjects.Add(Owned.Guid,Owned);Info.ObjectGuid=Owned.Guid;
    int32 Before=Client->ChatHistory.Num();
    Client->RequestBackgroundAppraisal(Owned.Guid);Client->SendIdentifyObject(Owned.Guid);Session.OnAppraisal.Broadcast(Info);Host->TickLootNotifications();
    TestEqual(TEXT("Explicit assessment supersedes a background scan and still reports"),Client->ChatHistory.Num(),Before+1);
    Before=Client->ChatHistory.Num();Info.ObjectGuid=Ring.Guid;Host->LootMatchReported.Remove(Ring.Guid);
    Client->RequestBackgroundAppraisal(Ring.Guid);Session.OnAppraisal.Broadcast(Info);Host->TickLootNotifications();
    TestEqual(TEXT("Background assessment of external loot can still report matches"),Client->ChatHistory.Num(),Before+1);
    P->Running=false;
    }

    FString Source,Error;FFileHelper::LoadFileToString(Source,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")));
    auto Snapshot=[&](){return Parse(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"stamina":100,"max_stamina":100,"mana":100,"max_mana":100,"ready":true,"busy":false,"action_serial":0,"action_error":0,"container":700,"container_is_corpse":true,"position":{"cell":2103705613,"x":25,"y":97,"z":12},"spells":[],"enchantments":[],"targets":[],"inventory":[],"contents":[{"id":701,"wcid":701,"name":"Ring","count":2,"identified":true,"type":8}]})"));};
    auto Profile=[&](){return Parse(TEXT(R"({"buffing":false,"recovery":false,"combat_enabled":false,"looting":true,"loot_rules":[{"label":"Jewelry","action":"keep","name":"Ring"}]})"));};
    auto Step=[&](FACEPluginVM& VM,const auto& S,const auto& Profile){TSharedPtr<FJsonObject> I;TestTrue(*Error,VM.Step(S,Profile,I,Error));return I?I:MakeShared<FJsonObject>();};
    for(bool Enabled:{true,false})
    {
        FACEPluginVM VM;if(!TestTrue(TEXT("Pickup policy loads"),VM.Load(Source,Error)))return false;
        auto S=Snapshot(),ProfileData=Profile();ProfileData->SetBoolField(TEXT("loot_notify_pickup"),Enabled);
        auto I=Step(VM,S,ProfileData);
        TestEqual(TEXT("Rule sends the normal loot action"),I->GetStringField(TEXT("action")),FString(TEXT("loot")));
        TestFalse(TEXT("Sending pickup is not a success notification"),I->HasField(TEXT("loot_notification")));
        S->SetNumberField(TEXT("time"),101);I=Step(VM,S,ProfileData);
        TestFalse(TEXT("Unconfirmed pickup remains silent"),I->HasField(TEXT("loot_notification")));
        S->SetArrayField(TEXT("inventory"),S->GetArrayField(TEXT("contents")));S->SetArrayField(TEXT("contents"),{});S->SetNumberField(TEXT("container"),0);
        I=Step(VM,S,ProfileData);TestEqual(TEXT("Confirmed receipt honors pickup toggle"),I->HasField(TEXT("loot_notification")),Enabled);
        if(Enabled)
        {
            TestTrue(TEXT("Receipt includes quantity and matched rule"),I->GetStringField(TEXT("loot_notification")).Contains(TEXT("2 x Ring (rule \"Jewelry\", keep)")));
            auto Notice=MakeShared<FJsonObject>();Notice->SetStringField(TEXT("loot_notification"),I->GetStringField(TEXT("loot_notification")));
            P->Profile=ProfileData;P->Profile->SetBoolField(TEXT("loot_notify_match"),false);
            const int32 Before=Client->ChatHistory.Num();Host->Execute(*P,Notice);
            TestEqual(TEXT("Policy receipt is printed in local chat"),Client->ChatHistory.Num(),Before+1);
            TestEqual(TEXT("Pickup receipts also use the plugin chat filter"),Client->ChatHistory.Last().FilterType,ACEChatMessageType::Plugin);
            P->Profile->SetBoolField(TEXT("loot_notify_pickup"),false);Host->Execute(*P,Notice);
            TestEqual(TEXT("Native dispatcher also honors pickup toggle"),Client->ChatHistory.Num(),Before+1);
        }
        I=Step(VM,S,ProfileData);TestFalse(TEXT("Confirmed pickup cannot notify twice"),I->HasField(TEXT("loot_notification")));
    }
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),ProfileData=Profile();Step(VM,S,ProfileData);
        S->SetNumberField(TEXT("time"),111);auto I=Step(VM,S,ProfileData);
        TestFalse(TEXT("Timed-out transfer never claims pickup success"),I->HasField(TEXT("loot_notification")));
    }
    {
        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),ProfileData=Profile();
        auto Owned=Parse(TEXT(R"({"id":702,"wcid":701,"name":"Ring","count":3,"identified":true,"type":8})"));
        S->SetArrayField(TEXT("inventory"),{MakeShared<FJsonValueObject>(Owned)});Step(VM,S,ProfileData);
        S->SetNumberField(TEXT("container"),0);S->SetArrayField(TEXT("contents"),{});
        TestFalse(TEXT("Corpse closing and a preexisting stack do not confirm pickup"),Step(VM,S,ProfileData)->HasField(TEXT("loot_notification")));
        Owned->SetNumberField(TEXT("count"),5);auto I=Step(VM,S,ProfileData);
        TestTrue(TEXT("Confirmed stack merge reports just the received amount"),I->HasField(TEXT("loot_notification"))&&I->GetStringField(TEXT("loot_notification")).Contains(TEXT("2 x Ring")));
        TestFalse(TEXT("Merged pickup reports only once"),Step(VM,S,ProfileData)->HasField(TEXT("loot_notification")));
    }
    {
        // A universal comparison spans multiple frames. Native notification
        // jobs must refresh the budgeted score snapshot instead of waiting on
        // the first pending JSON object forever.
        if(!TestTrue(TEXT("DAT opens for recipient notification"),GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(ACETestDatDirectory())))return false;
        P->Running=false;
        P->Profile=Parse(TEXT(R"({"loot_rules":[{"label":"Trade jewelry","action":"keep","min_quality":3,"item_categories":["jewelry"],"rating_mode":"universal"}]})"));
        Ring.WeenieClassId=623;Ring.ValidLocations=0x100;Session.WorldObjects.Add(Ring.Guid,Ring);Session.SelectedObject.Guid=Ring.Guid;
        Info.ObjectGuid=Ring.Guid;Info.IntProperties={{171,0}};Info.SpellBookEntries={6075};Host->ObserveAppraisal(Info);
        const int32 Before=Client->ChatHistory.Num();++GFrameCounter;Host->TickLootNotifications();
        TestEqual(TEXT("Incomplete universal score never reports an early result"),Client->ChatHistory.Num(),Before);
        for(int32 Frame=0;Frame<64&&Client->ChatHistory.Num()==Before;++Frame){++GFrameCounter;Host->TickLootNotifications();}
        if(TestEqual(TEXT("Native universal notification finishes across frames"),Client->ChatHistory.Num(),Before+1))
            TestTrue(TEXT("Native match identifies the recipient"),Client->ChatHistory.Last().Text.Contains(TEXT("for War mage")));

        // TestItem can preview a recipient rule before it has been saved.
        const auto Draft=P->Profile->GetArrayField(TEXT("loot_rules"))[0]->AsObject();
        P->Profile=Parse(TEXT(R"({"loot_rules":[]})"));Host->InspectLootItem(Ring.Guid,Draft);
        for(int32 Frame=0;Frame<64&&Host->InspectLootGuid;++Frame){++GFrameCounter;Host->TickLootInspection();}
        TestEqual(TEXT("Draft recipient inspection completes"),Host->InspectLootGuid,0);
        TestTrue(TEXT("Draft recipient rule uses its own scoring context"),Client->ChatHistory.Last().Text.Contains(TEXT("Matches Trade jewelry · for War mage")));

        FACEPluginVM VM;VM.Load(Source,Error);auto S=Snapshot(),ProfileData=Profile();
        const auto Goal=ProfileData->GetArrayField(TEXT("loot_rules"))[0]->AsObject();
        Goal->SetStringField(TEXT("rating_mode"),TEXT("templates"));Goal->SetNumberField(TEXT("min_quality"),3);
        Goal->SetArrayField(TEXT("rating_targets"),{MakeShared<FJsonValueString>(TEXT("war"))});
        const auto Loot=S->GetArrayField(TEXT("contents"))[0]->AsObject();
        Loot->SetNumberField(TEXT("quality_grade"),0);Loot->SetBoolField(TEXT("usable_now"),false);
        Loot->SetObjectField(TEXT("quality_targets"),Parse(TEXT(R"({"war":{"grade":3,"score":85,"name":"Friend's mage"}})")));
        auto I=Step(VM,S,ProfileData);
        TestTrue(TEXT("Pickup explains the target despite personal Poor quality"),I->GetStringField(TEXT("status")).Contains(TEXT("for Friend's mage")));
        S->SetNumberField(TEXT("time"),101);S->SetArrayField(TEXT("inventory"),S->GetArrayField(TEXT("contents")));S->SetArrayField(TEXT("contents"),{});S->SetNumberField(TEXT("container"),0);
        I=Step(VM,S,ProfileData);
        TestTrue(TEXT("Confirmed recipient pickup names the friend"),I->HasField(TEXT("loot_notification"))&&I->GetStringField(TEXT("loot_notification")).Contains(TEXT("for Friend's mage")));
    }
    TestTrue(TEXT("All notifications remain local, with no server packets"),Session.CachedC2SPackets.IsEmpty());
    return true;
}
#endif
