#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/GameInstance.h"
#include "ACEDatSubsystem.h"
#include "ACELootQuality.h"
#include "ACETestDatDirectory.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"
#include "Interfaces/IPluginManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEQualityLootGoalsTest,"ACE.Plugins.QualityLootGoals",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEQualityLootGoalsTest::RunTest(const FString&)
{
    auto Parse=[](const TCHAR* Text){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;};
    auto* GI=NewObject<UGameInstance>();GI->Init();
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();const FString Root=FPaths::ProjectSavedDir()/TEXT("Automation/QualityLootGoals");Host->StorageRoot=Root;Host->Settings=MakeShared<FJsonObject>();
    ON_SCOPE_EXIT{GI->Shutdown();IFileManager::Get().DeleteDirectory(*Root,false,true);};
    auto Rule=Parse(TEXT(R"({"action":"keep","quick_goal":true,"min_quality":3,"item_categories":["wand","jewelry"],"wield_mode":"exact","wield_min":270,"conditions":[{"field":"legacy","key":6,"args":[0]}]})"));
    auto Item=Parse(TEXT(R"({"name":"Test Wand","wcid":29259,"identified":true,"quality_grade":5,"quality_category":"wand","quality_weapon":true,"skill_wield":270,"quality_estimated":false})"));
    auto Profile=Parse(TEXT(R"({"loot_rules":[]})"));Profile->SetArrayField(TEXT("loot_rules"),{MakeShared<FJsonValueObject>(Rule)});
    TestTrue(TEXT("Quality and wield goal validates"),Host->ValidatePublicProfile(Profile));
    const FString Matcher=TEXT("return function(s,p) local m=lootmatch(p.rule,s.item,s,{}); return {matched=m==true,unknown=m==nil} end");
    auto Match=[&](bool Expected,bool Unknown=false)
    {
        FACEPluginVM VM;FString Error;TSharedPtr<FJsonObject> Result;auto S=MakeShared<FJsonObject>(),P=MakeShared<FJsonObject>();S->SetObjectField(TEXT("item"),Item);P->SetObjectField(TEXT("rule"),Rule);
        if(TestTrue(*Error,VM.Load(Matcher,Error)&&VM.Step(S,P,Result,Error))){TestEqual(TEXT("Shared matcher respects quality/category/wield"),Result->GetBoolField(TEXT("matched")),Expected);TestEqual(TEXT("Only missing identification asks for appraisal"),Result->GetBoolField(TEXT("unknown")),Unknown);}
    };
    Match(true);Item->SetNumberField(TEXT("quality_grade"),3);Match(true);Item->SetNumberField(TEXT("quality_grade"),2);Match(false);
    Item->SetNumberField(TEXT("quality_grade"),5);Item->SetNumberField(TEXT("skill_wield"),375);Match(false);
    Rule->SetStringField(TEXT("wield_mode"),TEXT("range"));Rule->SetNumberField(TEXT("wield_max"),400);Match(true);
    Rule->SetStringField(TEXT("wield_mode"),TEXT("none"));Match(false);Item->SetNumberField(TEXT("skill_wield"),0);Match(true);
    Item->SetStringField(TEXT("quality_category"),TEXT("jewelry"));Match(true);Item->SetStringField(TEXT("quality_category"),TEXT("armor"));Match(false);
    Item->SetStringField(TEXT("quality_category"),TEXT("wand"));Item->SetBoolField(TEXT("identified"),false);Match(false,true);
    Item->SetBoolField(TEXT("identified"),true);Item->RemoveField(TEXT("quality_grade"));Match(false);
    Item->SetNumberField(TEXT("quality_grade"),5);Item->SetBoolField(TEXT("quality_estimated"),true);Match(true);Rule->SetBoolField(TEXT("allow_estimated"),false);Match(false);Rule->RemoveField(TEXT("allow_estimated"));
    Rule->SetStringField(TEXT("usability"),TEXT("now"));Match(false);
    Item->SetBoolField(TEXT("usable_now"),true);Match(true);
    Item->SetBoolField(TEXT("usable_now"),false);Item->SetBoolField(TEXT("usable_close"),true);Match(false);
    Rule->SetStringField(TEXT("usability"),TEXT("close"));Match(true);
    Item->SetBoolField(TEXT("usable_close"),false);Match(false);
    Rule->SetStringField(TEXT("usability"),TEXT("any"));Match(true);
    Rule->SetStringField(TEXT("conditions_mode"),TEXT("any"));Match(true);
    auto Conditions=Rule->GetArrayField(TEXT("conditions"));auto Extra=Parse(TEXT(R"({"field":"int","key":19,"op":"ge","value":100})"));Conditions.Add(MakeShared<FJsonValueObject>(Extra));Rule->SetArrayField(TEXT("conditions"),Conditions);
    Item->SetObjectField(TEXT("int_properties"),Parse(TEXT(R"({"19":50})")));Match(false);Item->SetObjectField(TEXT("int_properties"),Parse(TEXT(R"({"19":150})")));Match(true);
    Conditions.RemoveAt(1);Rule->SetArrayField(TEXT("conditions"),Conditions);Rule->RemoveField(TEXT("conditions_mode"));
    Rule->SetBoolField(TEXT("quick_goal"),false);Match(false);Rule->SetBoolField(TEXT("quick_goal"),true);
    Rule->SetArrayField(TEXT("item_categories"),{MakeShared<FJsonValueString>(TEXT("weapons"))});Item->SetStringField(TEXT("quality_category"),TEXT("twohand"));Match(true);
    Rule->SetNumberField(TEXT("min_quality"),6);TestFalse(TEXT("Out of range quality rejected"),Host->ValidatePublicProfile(Profile));Rule->SetNumberField(TEXT("min_quality"),3);
    Rule->SetStringField(TEXT("wield_mode"),TEXT("range"));Rule->SetNumberField(TEXT("wield_max"),200);TestFalse(TEXT("Reversed wield range rejected"),Host->ValidatePublicProfile(Profile));Rule->SetStringField(TEXT("wield_mode"),TEXT("any"));
    Rule->SetArrayField(TEXT("item_categories"),{});TestFalse(TEXT("Empty item types rejected"),Host->ValidatePublicProfile(Profile));

    // Native snapshot: a Lore requirement is never confused with skill wield.
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();if(!TestTrue(TEXT("DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    FACEWorldObject Wand;Wand.Guid=169;Wand.WeenieClassId=29259;Wand.ItemType=ACEItemType::Caster;Wand.Name=TEXT("War caster");
    FACEAppraisalInfo A;A.ObjectGuid=Wand.Guid;A.bSuccess=true;A.IntProperties={{158,2},{159,34},{160,375},{109,400},{171,0}};A.FloatProperties={{29,1.2},{152,1.16},{144,.09}};
    FACEPlayerVitals User;User.bValid=true;User.Level=180;
    for(int32 Id:{14,34}){auto& Skill=User.Skills.AddDefaulted_GetRef();Skill.SkillId=Id;Skill.Base=Skill.Current=Id==14?380:350;Skill.AdvancementClass=2;}
    TestFalse(TEXT("Current usability includes both base wield and Lore"),ACELootQuality::CanUseWithin(A,User));
    TestTrue(TEXT("Exact near thresholds admit numeric shortfalls"),ACELootQuality::CanUseWithin(A,User,25,5,10,25));
    User.Skills[1].Base=349;TestFalse(TEXT("A shortfall beyond the near threshold is excluded"),ACELootQuality::CanUseWithin(A,User,25,5,10,25));
    User.Skills[1].Base=375;User.Skills[0].Current=400;TestTrue(TEXT("Max-skill users retain top wield items"),ACELootQuality::CanUseWithin(A,User));
    A.IntProperties.Add(270,8);A.IntProperties.Add(271,34);A.IntProperties.Add(272,3);
    TestFalse(TEXT("Near filtering never assumes future specialization"),ACELootQuality::CanUseWithin(A,User,25,5,10,25));
    A.IntProperties.Remove(270);A.IntProperties.Remove(271);A.IntProperties.Remove(272);
    Host->ObserveAppraisal(A);const auto Native=Host->PluginItemSnapshot(Wand)->AsObject();
    TestEqual(TEXT("Native wand class exported"),Native->GetStringField(TEXT("quality_category")),FString(TEXT("wand")));
    TestEqual(TEXT("Only the base skill gate defines wield filtering"),Native->GetNumberField(TEXT("skill_wield")),375.);
    TestTrue(TEXT("Shared hover rating is exported to UCM"),Native->HasField(TEXT("quality_grade")));
    A.IntProperties.Remove(158);A.IntProperties.Remove(159);A.IntProperties.Remove(160);Host->ObserveAppraisal(A);
    TestEqual(TEXT("Lore-only item has no skill wield requirement"),Host->PluginItemSnapshot(Wand)->AsObject()->GetNumberField(TEXT("skill_wield")),0.);

    // Exercise the real UCM pickup decision, rather than just its predicate.
    FString Source,Error;const auto Plugin=Host->Find(TEXT("ucm"));
    if(!TestTrue(TEXT("UCM source loads"),Plugin&&FFileHelper::LoadFileToString(Source,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("ClientMods/ucm/main.lua")))))return false;
    auto S=Parse(TEXT(R"({"time":100,"player":1,"health":100,"max_health":100,"mana":100,"max_mana":100,"stamina":100,"max_stamina":100,"ready":true,"busy":false,"container":9,"inventory":[],"targets":[],"position":{"cell":2103705613,"x":25,"y":97,"z":12},"spells":[],"enchantments":[],"action_serial":0,"action_error":0,"contents":[]})"));
    Rule->SetArrayField(TEXT("item_categories"),{MakeShared<FJsonValueString>(TEXT("weapons"))});Item->SetNumberField(TEXT("id"),50);Item->SetNumberField(TEXT("count"),1);S->SetArrayField(TEXT("contents"),{MakeShared<FJsonValueObject>(Item)});
    Profile->SetBoolField(TEXT("looting"),true);Profile->SetBoolField(TEXT("buffing"),false);Profile->SetBoolField(TEXT("recovery"),false);Profile->SetStringField(TEXT("combat"),TEXT("off"));
    FACEPluginVM Policy;TSharedPtr<FJsonObject> Intent;
    const bool PickOK=Policy.Load(Source,Error)&&Policy.Step(S,Profile,Intent,Error);
    if(!PickOK)AddError(Error);
    if(TestTrue(TEXT("Real UCM policy evaluates quality goals"),PickOK))TestEqual(TEXT("Matching item produces pickup intent"),Intent->GetStringField(TEXT("action")),FString(TEXT("loot")));
    Item->SetNumberField(TEXT("quality_grade"),2);FACEPluginVM Reject;
    if(TestTrue(TEXT("Lower quality decision evaluates"),Reject.Load(Source,Error)&&Reject.Step(S,Profile,Intent,Error)))TestFalse(TEXT("Lower quality item never produces a pickup"),Intent->HasField(TEXT("action"))&&Intent->GetStringField(TEXT("action"))==TEXT("loot"));
    return !HasAnyErrors();
}
#endif
