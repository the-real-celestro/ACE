#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Protocol/ACEBinaryWriter.h"
#include "Protocol/ACEObjectCreateParser.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"
#include "Mods/ACEVTProfile.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPluginLootIconsTest,"ACE.Plugins.LootIconProperties",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEPluginLootIconsTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
    Context.SetCurrentWorld(World);Context.OwningGameInstance=GI;GI->OnWorldChanged(nullptr,World);GI->Init();
    const FString Root=FPaths::ProjectSavedDir()/TEXT("Automation/LootIconProperties")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);IFileManager::Get().DeleteDirectory(*Root,false,true);};
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();Host->StorageRoot=Root;Host->Settings=MakeShared<FJsonObject>();
    auto Session=GI->GetSubsystem<UACEClientSubsystem>()->GetSession();
    if(!TestTrue(TEXT("Native session exists"),Session.IsValid()))return false;
    Session->State=EACESessionState::InWorld;Session->PlayerGuid=0x50000001;
    Session->PlayerVitals.bValid=true;Session->PlayerVitals.Health=Session->PlayerVitals.MaxHealth=100;
    Session->PlayerPosition.CellId=0x7d640001;Session->PlayerPosition.Location=FVector(84,84,12);
    FACEWorldObject Self;Self.Guid=Session->PlayerGuid;Self.ItemsCapacity=100;Session->WorldObjects.Add(Self.Guid,Self);
    FACEWorldObject Corpse;Corpse.Guid=9;Corpse.ItemType=ACEItemType::Container;Corpse.ObjectDescriptionFlags=ACEObjectDescFlag::Corpse;
    Session->WorldObjects.Add(Corpse.Guid,Corpse);Session->OpenExternalContainerGuid=Corpse.Guid;
    // An empty ViewContents list is sufficient here: the native snapshot still
    // serializes the objects assigned to this container, without a live server.
    Session->ContainerContents.Add(Corpse.Guid,{});

    // Classic LootSnob's actual level tests use IconOverlay, not ItemMaxLevel.
    // Round trip UTL so this covers import, native snapshot and UCM decisions.
    auto Document=MakeShared<FJsonObject>();Document->SetStringField(TEXT("format"),TEXT("utl"));
    Document->SetArrayField(TEXT("extras"),{});TArray<TSharedPtr<FJsonValue>> Rules;
    auto Requirement=[](int32 Type,const FString& Body)
    {
        auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("type"),Type);R->SetStringField(TEXT("body"),Body);
        return MakeShared<FJsonValueObject>(R);
    };
    for(const TCHAR* Name:{TEXT("Aetheria"),TEXT("Cloak")})for(int32 Level:{5,4,3,0})
    {
        auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("label"),FString::Printf(TEXT("%s %d+"),Name,Level));
        R->SetNumberField(TEXT("action_id"),1);R->SetStringField(TEXT("expression"),TEXT(""));
        TArray<TSharedPtr<FJsonValue>> Requirements={Requirement(1,FString(Name)+TEXT("\n1\n"))};
        if(FString(Name)==TEXT("Cloak"))Requirements.Add(Requirement(7,TEXT("3\n")));
        if(Level)Requirements.Add(Requirement(Level==5?12:3,FString::Printf(TEXT("%d\n218103849\n"),27699+Level)));
        if(Level<4)Requirements.Add(Requirement(9999,TEXT("True\n")));
        R->SetArrayField(TEXT("requirements"),Requirements);Rules.Add(MakeShared<FJsonValueObject>(R));
    }
    Document->SetArrayField(TEXT("rules"),Rules);FString Error;TArray<FString> Issues;
    auto Imported=ACEVTProfile::Read(ACEVTProfile::WriteLoot(Document,Error),TEXT("utl"),Error);
    if(!TestTrue(TEXT("Classic level-rule fixture imports"),Imported.IsValid()))return false;
    auto Profile=ACEVTProfile::Convert(Imported,Issues);
    TestEqual(TEXT("All level conditions supported"),Issues.Num(),0);
    Profile->SetBoolField(TEXT("looting"),true);Profile->SetBoolField(TEXT("buffing"),false);
    Profile->SetBoolField(TEXT("recovery"),false);Profile->SetStringField(TEXT("combat"),TEXT("off"));
    FString Script;
    if(!TestTrue(TEXT("Load shipped UCM"),FFileHelper::LoadFileToString(Script,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")))))return false;

    auto MakeItem=[&](const FString& Name,int32 Type,uint32 Icon,uint32 Overlay,uint32 Underlay,uint32 Wcid)
    {
        FACEBinaryWriter W;
        auto Packed=[&](uint32 Value){if(Value<0x8000)W.WriteUInt16(uint16(Value));else{W.WriteUInt16(uint16((Value>>16)|0x8000));W.WriteUInt16(uint16(Value));}};
        // PublicWeenieDesc carries packed icon indices. The parser reconstructs
        // full texture DIDs for rendering before the plugin snapshot sees them.
        W.WriteUInt32(10);W.WriteUInt32(0x4000|(Overlay?0x40000000:0));W.WriteString16L(Name);
        Packed(Wcid);Packed(Icon);W.WriteUInt32(Type);W.WriteUInt32(Underlay?0x04000000:0);W.Align();
        if(Underlay)W.WriteUInt32(1);
        W.WriteUInt32(Corpse.Guid);if(Overlay)Packed(Overlay);if(Underlay)Packed(Underlay);W.Align();
        FACEBinaryReader Reader(W.GetData());FACEDecodedObject Decoded;
        TestTrue(TEXT("Decode public item description"),FACEObjectCreateParser::ParseGameDataOnly(Reader,Decoded));
        FACEWorldObject Item;Item.Guid=Decoded.Guid;Item.WeenieClassId=Decoded.WeenieClassId;Item.Name=Decoded.Name;
        Item.ItemType=Decoded.ItemType;Item.ContainerId=Decoded.ContainerId;Item.IconId=Decoded.IconId;
        Item.IconOverlayId=Decoded.IconOverlayId;Item.IconUnderlayId=Decoded.IconUnderlayId;
        Session->WorldObjects.Add(Item.Guid,Item);return Item;
    };
    auto Decision=[&](const TSharedPtr<FJsonObject>& State)
    {
        FACEPluginVM VM;TestTrue(TEXT("UCM loads for decision"),VM.Load(Script,Error));TSharedPtr<FJsonObject> Intent;
        TestTrue(TEXT("Native item can be evaluated"),VM.Step(State,Profile,Intent,Error));
        return Intent;
    };
    for(bool Identified:{false,true})for(const TCHAR* Name:{TEXT("Coalesced Aetheria"),TEXT("Cloak")})for(int32 Level=0;Level<=5;++Level)
    {
        const auto Item=MakeItem(Name,FString(Name)==TEXT("Cloak")?ACEItemType::Clothing:ACEItemType::Gem,
            0x1234,Level?27699+Level:0,0,50000);
        if(Identified){FACEAppraisalInfo Appraisal;Appraisal.bSuccess=true;Appraisal.IntProperties.Add(319,Level);Host->Appraisals.Add(Item.Guid,Appraisal);}
        else Host->Appraisals.Remove(Item.Guid);
        auto State=Host->Snapshot();State->SetNumberField(TEXT("time"),100);
        const auto& Contents=State->GetArrayField(TEXT("contents"));
        if(!TestEqual(TEXT("Native corpse contains fixture item"),Contents.Num(),1))return false;
        auto Record=Contents[0]->AsObject();const auto Ints=Record->GetObjectField(TEXT("int_properties"));
        TestEqual(TEXT("Rendering icon retains the full texture DID"),Record->GetNumberField(TEXT("icon")),double(0x06001234));
        TestEqual(TEXT("Lua item icons receive the full overlay DID"),Record->GetNumberField(TEXT("icon_overlay")),double(Item.IconOverlayId));
        TestEqual(TEXT("Lua item icons receive the full underlay DID"),Record->GetNumberField(TEXT("icon_underlay")),double(Item.IconUnderlayId));
        TestEqual(TEXT("Lua item icons receive inventory enchantment effects"),Record->GetNumberField(TEXT("ui_effects")),double(uint32(Item.UiEffects)));
        TestEqual(TEXT("Decal Icon is its packed index"),Ints->GetNumberField(TEXT("218103809")),double(0x1234));
        TestEqual(TEXT("Decal level overlay is its packed index"),Ints->GetNumberField(TEXT("218103849")),double(Level?27699+Level:0));
        auto Intent=Decision(State);if(!Intent)return false;
        const FString ContextLabel=FString::Printf(TEXT("%s level %d identified=%d"),Name,Level,Identified);
        TestEqual(*(ContextLabel+TEXT(" only loots levels 4 and 5")),Intent->GetStringField(TEXT("action")),FString(Level>=4?TEXT("loot"):TEXT("close_corpse")));
        if(Level>=4)TestTrue(*(ContextLabel+TEXT(" matches the correct threshold/equality rule")),Intent->GetStringField(TEXT("status")).Contains(FString::Printf(TEXT("%d+"),Level)));
    }
    // Custom servers can use extended packed icon indices and WCIDs. Do not
    // truncate to 16 bits, derive a level from a name, or whitelist stock items.
    auto Custom=MakeItem(TEXT("Custom Cloak"),ACEItemType::Clothing,0x12345,0x23456,0x34567,0x123456);
    Host->Appraisals.Remove(Custom.Guid);
    auto State=Host->Snapshot();auto Record=State->GetArrayField(TEXT("contents"))[0]->AsObject();
    auto Ints=Record->GetObjectField(TEXT("int_properties"));
    TestEqual(TEXT("Extended base icon index preserved"),Ints->GetNumberField(TEXT("218103809")),double(0x12345));
    TestEqual(TEXT("Extended overlay index preserved"),Ints->GetNumberField(TEXT("218103849")),double(0x23456));
    TestEqual(TEXT("Extended underlay index preserved"),Ints->GetNumberField(TEXT("218103850")),double(0x34567));
    TestEqual(TEXT("Custom WCID remains unchanged"),Ints->GetNumberField(TEXT("218103808")),double(0x123456));
    Custom.ContainerId=Self.Guid;Session->WorldObjects[Custom.Guid]=Custom;++Session->InventoryDataRevision;
    State=Host->Snapshot();const auto& Inventory=State->GetArrayField(TEXT("inventory"));
    if(TestEqual(TEXT("Custom item moves into native inventory snapshot"),Inventory.Num(),1))
        TestEqual(TEXT("Inventory uses the same Decal icon conversion"),Inventory[0]->AsObject()->GetObjectField(TEXT("int_properties"))->GetNumberField(TEXT("218103849")),double(0x23456));
    TestEqual(TEXT("Native overlay DID remains intact"),Session->WorldObjects[Custom.Guid].IconOverlayId,int32(0x06023456));
    Custom.ItemType=ACEItemType::ManaStone;Session->WorldObjects[Custom.Guid]=Custom;
    FACEAppraisalInfo StoneInfo;StoneInfo.bSuccess=true;StoneInfo.IntProperties.Add(107,0);
    Host->Appraisals.Add(Custom.Guid,StoneInfo);++Session->InventoryDataRevision;
    State=Host->Snapshot();
    TestTrue(TEXT("An explicit zero mana quality is an empty stone"),State->GetArrayField(TEXT("inventory"))[0]->AsObject()->GetBoolField(TEXT("mana_empty")));
    Corpse.Name=TEXT("Corpse");Corpse.bHasPosition=true;Corpse.Position=Session->PlayerPosition;
    Session->WorldObjects[Corpse.Guid]=Corpse;
    FACEClientPlugin Plugin;Plugin.Id=TEXT("ucm");
    auto Close=MakeShared<FJsonObject>();Close->SetBoolField(TEXT("completed"),true);
    Host->ExecuteInventory(Plugin,Close,TEXT("close_corpse"));
    TestTrue(TEXT("Completed corpse recorded outside the Lua VM"),Host->LootedCorpses.Contains(Corpse.Guid));
    Host->ObserveTargetDeleted(Corpse.Guid);Session->WorldObjects.Remove(Corpse.Guid);
    TestTrue(TEXT("Visibility release does not forget a looted corpse"),Host->LootedCorpses.Contains(Corpse.Guid));
    Host->ObserveTargetCreated(Corpse);Session->WorldObjects.Add(Corpse.Guid,Corpse);
    TestTrue(TEXT("Returning to the same corpse preserves history"),Host->LootedCorpses.Contains(Corpse.Guid));
    State=Host->Snapshot();const auto& Seen=State->GetArrayField(TEXT("corpses"));
    if(TestEqual(TEXT("Native snapshot includes completed corpse"),Seen.Num(),1))
        TestTrue(TEXT("A fresh UCM VM receives completed-corpse history"),Seen[0]->AsObject()->GetBoolField(TEXT("looted")));
    Corpse.Position.Location.X+=1;Host->ObserveTargetCreated(Corpse);
    TestFalse(TEXT("Reused GUID at a new position is eligible again"),Host->LootedCorpses.Contains(Corpse.Guid));
    Plugin.Profile=MakeShared<FJsonObject>();Plugin.Permissions.Add(TEXT("loot"));
    auto Completed=MakeShared<FJsonObject>();Completed->SetNumberField(TEXT("completed_corpse"),uint32(Corpse.Guid));
    Host->Execute(Plugin,Completed);
    TestTrue(TEXT("Confirmed last-item receipt records a corpse already closed by the server"),Host->LootedCorpses.Contains(Corpse.Guid));
    FACEWorldObject Pet;Pet.Guid=40;Pet.ItemType=ACEItemType::Creature;Pet.bHasPosition=true;
    Pet.Position=Session->PlayerPosition;Pet.PetOwnerId=Self.Guid;Session->WorldObjects.Add(Pet.Guid,Pet);
    TestTrue(TEXT("Living pet blocks replacement"),Host->Snapshot()->GetBoolField(TEXT("owned_pet")));
    Session->WorldObjects[Pet.Guid].bDying=true;
    TestFalse(TEXT("Death animation no longer blocks the next eligible summon"),Host->Snapshot()->GetBoolField(TEXT("owned_pet")));
    Session->WorldObjects[Pet.Guid].bDying=false;Session->WorldObjects[Pet.Guid].bHasPosition=false;
    TestFalse(TEXT("Retained positionless pet record does not block summoning forever"),Host->Snapshot()->GetBoolField(TEXT("owned_pet")));

    // Click diagnostics use the same predicate and native item qualities while UCM is stopped.
    TestTrue(TEXT("Initialize isolated click profile"),Host->InitializeUCMSelection());
    auto Policy=Host->Find(TEXT("ucm"));Policy->Enabled=true;Policy->Running=false;
    auto ClickRule=MakeShared<FJsonObject>();ClickRule->SetStringField(TEXT("label"),TEXT("Guts"));
    ClickRule->SetStringField(TEXT("name"),TEXT("Ravener Guts"));ClickRule->SetStringField(TEXT("name_mode"),TEXT("exact"));
    ClickRule->SetStringField(TEXT("action"),TEXT("keep"));
    auto NameCondition=MakeShared<FJsonObject>();NameCondition->SetStringField(TEXT("field"),TEXT("string"));
    NameCondition->SetNumberField(TEXT("key"),1);NameCondition->SetStringField(TEXT("pattern"),TEXT("Ravener Guts"));
    ClickRule->SetArrayField(TEXT("conditions"),{MakeShared<FJsonValueObject>(NameCondition)});
    auto FirstRule=MakeShared<FJsonObject>();FirstRule->SetStringField(TEXT("label"),TEXT("Lucky Gold"));
    auto FirstCondition=MakeShared<FJsonObject>();FirstCondition->Values=NameCondition->Values;
    FirstCondition->SetStringField(TEXT("pattern"),TEXT("Lucky Gold"));
    FirstRule->SetArrayField(TEXT("conditions"),{MakeShared<FJsonValueObject>(FirstCondition)});
    Policy->Profile->SetArrayField(TEXT("loot_rules"),{MakeShared<FJsonValueObject>(FirstRule),MakeShared<FJsonValueObject>(ClickRule)});
    FACEWorldObject ClickItem;ClickItem.Guid=int32(0x80A46E10);ClickItem.Name=TEXT("Ravener Guts");ClickItem.ItemType=ACEItemType::Misc;
    ClickItem.ObjectDescriptionFlags=0x12;ClickItem.WeenieClassId=7040;ClickItem.ContainerId=Self.Guid;Session->WorldObjects.Add(ClickItem.Guid,ClickItem);
    auto* Client=GI->GetSubsystem<UACEClientSubsystem>();const int32 Before=Client->ChatHistory.Num();
    Client->SelectInventoryObject(ClickItem.Guid);Host->TickLootInspection();
    TestEqual(TEXT("Matching inventory click prints one local message"),Client->ChatHistory.Num(),Before+1);
    if(Client->ChatHistory.Num()>Before)
    {
        const auto& Message=Client->ChatHistory.Last();
        TestTrue(TEXT("Chat names profile and matching rule"),Message.Text.Contains(Host->UCMSectionName(TEXT("Loot")))&&Message.Text.Contains(TEXT("Guts (keep)")));
        TestEqual(TEXT("Matching stat line is bold"),Message.BoldText,Message.Text);
        TestEqual(TEXT("Matched item uses retail green chat"),Message.Type,ACEChatMessageType::Broadcast);
        TestTrue(TEXT("Stat line includes public value and burden"),Message.Text.Contains(TEXT("Value"))&&Message.Text.Contains(TEXT("Burden")));
    }
    Host->TickLootInspection();TestEqual(TEXT("Completed inspection does not spam chat"),Client->ChatHistory.Num(),Before+1);
    Client->SelectInventoryObject(ClickItem.Guid);Host->TickLootInspection();
    TestEqual(TEXT("Duplicate selection callbacks do not print a second line"),Client->ChatHistory.Num(),Before+1);
    TestTrue(TEXT("Matched item is explicitly flagged"),Client->ChatHistory.Last().Text.StartsWith(TEXT("[MATCH]")));
    TestTrue(TEXT("Matched item carries an explicit green color"),Client->ChatHistory.Last().LocalColor.IsSet()&&Client->ChatHistory.Last().LocalColor.GetValue().G>.9f);

    // Appraisal names can differ from the public name used by both the inventory
    // and the basic editor. Name (1) must refer to the same public identity.
    FACEAppraisalInfo RawName;RawName.ObjectGuid=ClickItem.Guid;RawName.bSuccess=true;
    RawName.StringProperties.Add(1,TEXT("raw template name"));Host->Appraisals.Add(ClickItem.Guid,RawName);
    TestEqual(TEXT("Appraisal cannot replace public Name (1)"),Host->LootItemSnapshot(ClickItem)->AsObject()->GetObjectField(TEXT("string_properties"))->GetStringField(TEXT("1")),ClickItem.Name);
    const int32 CheckBefore=Client->ChatHistory.Num();
    Host->LastInspectedAt=0;Client->SelectInventoryObject(ClickItem.Guid);Host->TickLootInspection();
    TestEqual(TEXT("Saved Lucky Gold then Ravener regex/exact rules match after appraisal"),Client->ChatHistory.Num(),CheckBefore+1);
    auto Draft=MakeShared<FJsonObject>();Draft->SetStringField(TEXT("name"),TEXT("Different item"));Draft->SetStringField(TEXT("name_mode"),TEXT("exact"));
    Host->InspectLootItem(ClickItem.Guid,Draft);Host->TickLootInspection();
    TestTrue(TEXT("Draft testing reports a nonmatch in chat"),Client->ChatHistory.Last().Text.Contains(TEXT("Does not match")));
    TestEqual(TEXT("Draft testing leaves the active rules untouched"),Policy->Profile->GetArrayField(TEXT("loot_rules")).Num(),2);
    Host->Appraisals.Remove(ClickItem.Guid);
    Client->ChatHistory.SetNum(Before+1);

    Host->LastInspectedAt=0;ClickRule->SetBoolField(TEXT("enabled"),false);Client->SelectInventoryObject(ClickItem.Guid);Host->TickLootInspection();
    TestEqual(TEXT("Disabled rule still prints a basic stat line"),Client->ChatHistory.Num(),Before+2);
    TestTrue(TEXT("Nonmatched stat line is plain text"),Client->ChatHistory.Last().BoldText.IsEmpty());
    TestFalse(TEXT("Disabled rule never reports a loot match"),Client->ChatHistory.Last().Text.Contains(TEXT("Matches")));
    TestTrue(TEXT("Nonmatching stat line is explicitly white"),Client->ChatHistory.Last().LocalColor.IsSet()&&Client->ChatHistory.Last().LocalColor.GetValue()==FLinearColor::White);
    Host->LastInspectedAt=0;ClickRule->SetBoolField(TEXT("enabled"),true);Client->SelectInventoryObject(ClickItem.Guid);Client->SelectInventoryObject(0);Host->TickLootInspection();
    TestEqual(TEXT("Changing selection does not print a provisional duplicate"),Client->ChatHistory.Num(),Before+2);

    auto NeedsId=MakeShared<FJsonObject>();NeedsId->SetStringField(TEXT("field"),TEXT("int"));
    NeedsId->SetNumberField(TEXT("key"),265);NeedsId->SetStringField(TEXT("op"),TEXT("ge"));NeedsId->SetNumberField(TEXT("value"),1);
    ClickRule->SetArrayField(TEXT("conditions"),{MakeShared<FJsonValueObject>(NeedsId)});
    Client->SelectInventoryObject(ClickItem.Guid);Host->TickLootInspection();
    TestTrue(TEXT("Unknown item qualities defer the match"),Host->InspectLootWaiting);
    TestEqual(TEXT("Unknown qualities never print a speculative match"),Client->ChatHistory.Num(),Before+2);
    FACEAppraisalInfo ClickAppraisal;ClickAppraisal.ObjectGuid=ClickItem.Guid;ClickAppraisal.bSuccess=true;
    ClickAppraisal.IntProperties.Add(265,1);Host->ObserveAppraisal(ClickAppraisal);Host->TickLootInspection();
    TestEqual(TEXT("Completed appraisal resumes the clicked item match"),Client->ChatHistory.Num(),Before+3);
    Policy->Profile->SetArrayField(TEXT("loot_rules"),{});Policy->Enabled=false;
    Session->WorldObjects[ClickItem.Guid].ItemType=ACEItemType::MeleeWeapon;
    ClickAppraisal.bHasWeaponProfile=true;ClickAppraisal.Damage=40;ClickAppraisal.DamageVariance=.25;ClickAppraisal.WeaponTime=30;
    Host->LastInspectedAt=0;ClickAppraisal.FloatProperties.Add(29,1.2);ClickAppraisal.FloatProperties.Add(149,1.1);ClickAppraisal.FloatProperties.Add(150,1.05);ClickAppraisal.IntProperties.Add(204,12);
    Host->ObserveAppraisal(ClickAppraisal);Client->SelectInventoryObject(ClickItem.Guid);Host->TickLootInspection();
    TestTrue(TEXT("Weapon click prints appraised damage and speed without any loot rules or enabled UCM"),Client->ChatHistory.Last().Text.Contains(TEXT("Dmg 30–40"))&&Client->ChatHistory.Last().Text.Contains(TEXT("Speed 30")));
    TestTrue(TEXT("Weapon includes attack, three defenses and damage bonuses"),Client->ChatHistory.Last().Text.Contains(TEXT("Attack"))&&Client->ChatHistory.Last().Text.Contains(TEXT("Melee D"))&&Client->ChatHistory.Last().Text.Contains(TEXT("Missile D"))&&Client->ChatHistory.Last().Text.Contains(TEXT("Magic D"))&&Client->ChatHistory.Last().Text.Contains(TEXT("Damage bonus 12")));
    TestTrue(TEXT("Unmatched weapon stat line stays unbolded"),Client->ChatHistory.Last().BoldText.IsEmpty());
    TestFalse(TEXT("Inspecting loot never starts UCM"),Policy->Running);
    return true;
}
#endif
