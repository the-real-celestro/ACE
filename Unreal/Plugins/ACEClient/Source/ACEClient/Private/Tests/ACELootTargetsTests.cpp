#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACELootTargets.h"
#include "ACELootQuality.h"
#include "ACELootQualitySubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACETestDatDirectory.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"
#include "Engine/GameInstance.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELootTargetsTest,"ACE.LootQuality.TargetTemplates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELootTargetsTest::RunTest(const FString&)
{
    auto Parse=[](const TCHAR* Text){TSharedPtr<FJsonObject> J;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J);return J;};
    auto* GI=NewObject<UGameInstance>();GI->Init();auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();
    const FString Root=FPaths::ProjectSavedDir()/TEXT("Automation/LootTargets");Host->StorageRoot=Root;Host->Settings=MakeShared<FJsonObject>();
    ON_SCOPE_EXIT{GI->Shutdown();IFileManager::Get().DeleteDirectory(*Root,false,true);};
    if(!TestTrue(TEXT("DAT opens"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    auto Profile=Parse(TEXT(R"({"loot_rules":[{"quick_goal":true,"action":"keep","min_quality":3,"item_categories":["jewelry"],"rating_mode":"templates","rating_targets":["war","missile"],"conditions":[{"field":"legacy","key":6,"args":[0]}]}]})"));
    FACELootTemplate Mage,Heavy,Archer;
    TestTrue(TEXT("Ready-made templates resolve without character login"),ACELootTargets::Resolve(TEXT("war"),Profile,Mage)&&ACELootTargets::Resolve(TEXT("heavy"),Profile,Heavy)&&ACELootTargets::Resolve(TEXT("missile"),Profile,Archer));
    TestTrue(TEXT("Multi-recipient profile validates"),Host->ValidatePublicProfile(Profile));
    FACELootTemplate Friend=Mage;Friend.Id=TEXT("custom:friend");Friend.Name=TEXT("Friend's mage");Profile->SetArrayField(TEXT("loot_rating_templates"),{MakeShared<FJsonValueObject>(ACELootTargets::ToJson(Friend))});
    FACELootTemplate Restored;TestTrue(TEXT("Named templates round-trip with skill identity"),ACELootTargets::Resolve(Friend.Id,Profile,Restored)&&Restored.Signature()==Friend.Signature());
    auto Rule=Profile->GetArrayField(TEXT("loot_rules"))[0]->AsObject();Rule->SetArrayField(TEXT("rating_targets"),{MakeShared<FJsonValueString>(Friend.Id),MakeShared<FJsonValueString>(TEXT("missile"))});
    TestTrue(TEXT("Custom and preset recipients mix"),Host->ValidatePublicProfile(Profile));
    auto Invalid=Parse(TEXT(R"({"loot_rules":[{"rating_mode":"templates","rating_targets":["custom:missing"]}]})"));
    TestFalse(TEXT("Missing recipients cannot silently fall back to looter"),Host->ValidatePublicProfile(Invalid));
    Invalid=Parse(TEXT(R"({"loot_rules":[{"rating_mode":"templates","rating_targets":[]}]})"));TestFalse(TEXT("Empty recipient selection is rejected"),Host->ValidatePublicProfile(Invalid));
    Invalid=Parse(TEXT(R"({"loot_rating_templates":[{"id":"custom:a","name":"Bad","primary":34,"skills":[44]}]})"));TestFalse(TEXT("Main skill must be trained in saved template"),Host->ValidatePublicProfile(Invalid));
    {
        auto All=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Saved,Targets;
        for(const auto& T:ACELootTargets::Presets())Targets.Add(MakeShared<FJsonValueString>(T.Id));
        for(int32 I=0;I<16;++I){auto T=Mage;T.Id=FString::Printf(TEXT("custom:%d"),I);T.Name=FString::Printf(TEXT("Friend %d"),I);Saved.Add(MakeShared<FJsonValueObject>(ACELootTargets::ToJson(T)));Targets.Add(MakeShared<FJsonValueString>(T.Id));}
        auto R=Parse(TEXT(R"({"action":"keep","rating_mode":"templates","min_quality":3,"item_categories":["jewelry"]})"));R->SetArrayField(TEXT("rating_targets"),Targets);
        All->SetArrayField(TEXT("loot_rating_templates"),Saved);All->SetArrayField(TEXT("loot_rules"),{MakeShared<FJsonValueObject>(R)});
        TestTrue(TEXT("All preset and saved recipients can be selected together"),Host->ValidatePublicProfile(All));
    }

    FACEWorldObject Ring;Ring.Guid=172;Ring.WeenieClassId=623;Ring.ItemType=ACEItemType::Jewelry;Ring.ValidLocations=0x100;Ring.Name=TEXT("Template comparison ring");
    FACEAppraisalInfo A;A.ObjectGuid=Ring.Guid;A.bSuccess=true;A.IntProperties={{171,0}};A.SpellBookEntries={6075};
    const auto Me=ACELootQuality::Evaluate(Ring,A,Heavy.Character(),{},Dat),ForFriend=ACELootQuality::Evaluate(Ring,A,Mage.Character(),{},Dat);
    TestTrue(TEXT("Good War jewelry is retained for a friend despite personal Poor score"),Me.bRated&&Me.Points==0&&ForFriend.bRated&&ForFriend.Grade==ACELootScoring::Grade::Rare&&ForFriend.Points==85);
    A.SpellBookEntries={6075,6072};double Universal=0;
    for(const auto& T:ACELootTargets::Presets())Universal=FMath::Max(Universal,ACELootQuality::Evaluate(Ring,A,T.Character(),{},Dat).Points);
    TestTrue(TEXT("Universal compares whole coherent templates instead of merging incompatible skills"),FMath::IsNearlyEqual(Universal,85.));
    auto Hybrid=Heavy;Hybrid.Skills.AddUnique(34);
    TestTrue(TEXT("An explicitly saved hybrid can value both real trained skills"),ACELootQuality::Evaluate(Ring,A,Hybrid.Character(),{},Dat).Points>Universal);

    A.SpellBookEntries={6075};Host->ObserveAppraisal(A);auto* Quality=GI->GetSubsystem<UACELootQualitySubsystem>();
    const auto Plugin=Host->Find(TEXT("ucm"));Plugin->Profile=Profile;
    TSharedPtr<FJsonObject> Snapshot;
    for(int32 Frame=0;Frame<32;++Frame){++GFrameCounter;Snapshot=Host->PluginItemSnapshot(Ring)->AsObject();const auto Targets=Snapshot->GetObjectField(TEXT("quality_targets"));if(!Targets->GetObjectField(Friend.Id)->GetBoolField(TEXT("pending")))break;}
    TestEqual(TEXT("Recipient's score is exported independently"),Snapshot->GetObjectField(TEXT("quality_targets"))->GetObjectField(Friend.Id)->GetNumberField(TEXT("grade")),3.);
    const auto Count=Quality->TargetEvaluationCount;
    for(int32 Repeat=0;Repeat<100;++Repeat)Quality->ReviewFor(Ring,Mage);
    TestEqual(TEXT("Repeated snapshots reuse template scores"),Quality->TargetEvaluationCount,Count);
    Rule->SetStringField(TEXT("rating_mode"),TEXT("universal"));
    for(int32 Frame=0;Frame<64;++Frame){++GFrameCounter;Snapshot=Host->PluginItemSnapshot(Ring)->AsObject();if(!Snapshot->GetObjectField(TEXT("quality_universal"))->GetBoolField(TEXT("pending")))break;}
    TestEqual(TEXT("Universal mode finds War quality even when the looter cannot use it"),Snapshot->GetObjectField(TEXT("quality_universal"))->GetNumberField(TEXT("grade")),3.);
    TestEqual(TEXT("The universal explanation names the matching archetype"),Snapshot->GetObjectField(TEXT("quality_universal"))->GetStringField(TEXT("name")),FString(TEXT("War mage")));

    const FString Matcher=TEXT("return function(s,p) local scan={}; local m=lootmatch(p.rule,s.item,s,scan); return {matched=m==true,pending=m=='pending',target_pending=scan.target_pending,recipient=scan.target_match or ''} end");
    auto Item=Parse(TEXT(R"({"identified":true,"quality_category":"jewelry","quality_grade":0,"usable_now":false,"usable_close":false,"quality_targets":{"war":{"grade":3,"score":85,"name":"Friend's mage"},"missile":{"grade":0,"score":0,"name":"Archer alt"}}})"));
    Rule->SetStringField(TEXT("rating_mode"),TEXT("templates"));Rule->SetArrayField(TEXT("rating_targets"),{MakeShared<FJsonValueString>(TEXT("war")),MakeShared<FJsonValueString>(TEXT("missile"))});Rule->SetStringField(TEXT("usability"),TEXT("now"));
    auto Match=[&](bool Expected,bool Pending)
    {
        FACEPluginVM VM;FString Error;TSharedPtr<FJsonObject> Out;auto S=MakeShared<FJsonObject>(),P=MakeShared<FJsonObject>();S->SetObjectField(TEXT("item"),Item);P->SetObjectField(TEXT("rule"),Rule);
        if(TestTrue(TEXT("Recipient matcher evaluates"),VM.Load(Matcher,Error)&&VM.Step(S,P,Out,Error))){TestEqual(TEXT("Recipient thresholds use OR, independently of looter usability"),Out->GetBoolField(TEXT("matched")),Expected);TestEqual(TEXT("Budgeted scores wait without identifying again"),Out->GetBoolField(TEXT("pending")),Pending);TestEqual(TEXT("Only score waits request a refreshed snapshot"),Out->GetBoolField(TEXT("target_pending")),Pending);}
    };
    Match(true,false);Item->GetObjectField(TEXT("quality_targets"))->GetObjectField(TEXT("war"))->SetNumberField(TEXT("grade"),2);Match(false,false);
    Item->GetObjectField(TEXT("quality_targets"))->GetObjectField(TEXT("war"))->SetBoolField(TEXT("pending"),true);Match(false,true);
    auto Global=Parse(TEXT(R"({"grade":5,"score":110,"name":"War mage","estimated":true})")),Exact=Parse(TEXT(R"({"grade":3,"score":85,"name":"Archer alt","estimated":false})"));
    Item->SetObjectField(TEXT("quality_universal"),Global);Item->SetObjectField(TEXT("quality_universal_exact"),Exact);Rule->SetStringField(TEXT("rating_mode"),TEXT("universal"));Rule->SetBoolField(TEXT("allow_estimated"),false);Match(true,false);
    Exact->SetNumberField(TEXT("grade"),2);Match(false,false);

    // Target fit concerns training identity, not whether the looter meets wield values.
    FACEWorldObject Wand;Wand.Guid=173;Wand.WeenieClassId=29259;Wand.ItemType=ACEItemType::Caster;Wand.Name=TEXT("War caster");
    FACEAppraisalInfo W;W.ObjectGuid=Wand.Guid;W.bSuccess=true;W.IntProperties={{158,2},{159,34},{160,375},{171,0}};W.FloatProperties={{29,1.2},{152,1.16},{144,.09}};
    Host->ObserveAppraisal(W);++GFrameCounter;
    TestFalse(TEXT("Archer-only templates do not collect unrelated War weapons"),Quality->ReviewFor(Wand,Archer).bRated);
    auto WandScore=Quality->ReviewFor(Wand,Mage);TestTrue(TEXT("Mage template can rate a wand independently of looter skill"),WandScore.bRated);
    A.SpellBookEntries.Empty();Quality->ObserveAppraisal(A);++GFrameCounter;
    TestTrue(TEXT("New appraisal invalidates the affected recipient score"),Quality->ReviewFor(Ring,Mage).Points==0);
    return !HasAnyErrors();
}
#endif
