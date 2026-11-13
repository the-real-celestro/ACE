// Included by ACELootQuality.cpp so the audit uses the production catalogue and evaluator.
#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELootRatingAuditTest,"ACE.LootQuality.RatingAudit",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELootRatingAuditTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();auto* Dat=NewObject<UACEDatSubsystem>(GI);
    FString Directory;FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    if(!TestTrue(TEXT("Audit opens retail effect names"),Dat->LoadDatDirectory(Directory)))return false;
    struct FProfile {FString Name;FACEWorldObject Item;FACEAppraisalInfo A;FACEPlayerVitals C;double Base=0;};
    TArray<FProfile> Profiles;
    auto Character=[](int32 Primary)
    {
        FACEPlayerVitals C;C.bValid=true;C.Level=275;
        for(int32 Id:{6,7,14,15,16,18,20,21,22,23,24,27,28,29,30,31,32,33,35,36,37,38,39,40,48,49,50,51,52})
        {auto& S=C.Skills.AddDefaulted_GetRef();S.SkillId=Id;S.Base=S.Current=300;S.AdvancementClass=2;}
        auto* Main=C.Skills.FindByPredicate([&](const auto& S){return S.SkillId==Primary;});
        if(!Main){Main=&C.Skills.AddDefaulted_GetRef();}Main->SkillId=Primary;Main->Base=Main->Current=800;Main->AdvancementClass=3;return C;
    };
    for(int32 Index=0;Index<UE_ARRAY_COUNT(LootVariants);++Index)
    {
        const auto& V=LootVariants[Index];const FLootCohort* G=nullptr;const FLootTemplate* T=nullptr;
        for(const auto& Candidate:LootCohorts)if(Candidate.Variant==Index&&(!G||Candidate.Wield>G->Wield))G=&Candidate;
        for(const auto& Candidate:LootTemplates)if(Candidate.Variant==Index){T=&Candidate;break;}
        if(!G||!T)continue;
        auto& P=Profiles.AddDefaulted_GetRef();P.Name=FString(TEXT("Weapon · "))+V.Label;P.C=Character(V.Skill?V.Skill:34);
        auto& I=P.Item;I.Guid=170;I.WeenieClassId=T->WCID;I.Name=V.Label;I.ItemType=V.Family==1?ACEItemType::MeleeWeapon:V.Family==2?ACEItemType::MissileWeapon:ACEItemType::Caster;
        I.ValidLocations=ACEEquipMask::MeleeWeapon;I.AmmoType=FString(V.Label).Contains(TEXT("Atlatl"))?4:1;
        auto& A=P.A;A.ObjectGuid=I.Guid;A.bSuccess=true;A.ItemType=I.ItemType;A.WeaponSkill=V.Skill;A.bHasWeaponProfile=V.Family<=2;A.DamageType=V.Family>=3?8:1;
        A.IntProperties={{171,0},{105,7}};if(G->Kind){A.IntProperties.Add(158,G->Kind);A.IntProperties.Add(159,G->Skill);A.IntProperties.Add(160,G->Wield);}
        const auto& B=G->Bounds;A.Damage=FMath::Max(0.,B[0].Low);A.DamageVariance=FMath::Max(0.,B[1].High);A.WeaponOffense=B[2].Low<0?1:B[2].Low;A.WeaponDamageMod=B[4].Low<0?1:B[4].Low;
        if(B[3].Low>=0)A.FloatProperties.Add(29,B[3].Low);if(B[5].Low>=0)A.IntProperties.Add(204,B[5].Low);
        if(B[6].Low>=0)A.FloatProperties.Add(152,B[6].Low);if(B[8].Low>=0)A.FloatProperties.Add(144,B[8].High);
    }
    const int32 Builds[]={44,45,46,41,47,34,43,54};
    const TCHAR* BuildNames[]={TEXT("Heavy"),TEXT("Light"),TEXT("Finesse"),TEXT("Two-handed"),TEXT("Missile"),TEXT("War Magic"),TEXT("Void Magic"),TEXT("Summoning")};
    for(int32 Build=0;Build<8;++Build)for(int32 Kind=0;Kind<4;++Kind)
    {
        auto& P=Profiles.AddDefaulted_GetRef();P.C=Character(Builds[Build]);P.Name=FString(Kind==0?TEXT("Jewelry"):Kind==1?TEXT("Underclothes"):Kind==2?TEXT("Armor"):TEXT("Covenant"))+TEXT(" · ")+BuildNames[Build];
        auto& I=P.Item;I.Guid=170;I.Name=P.Name;I.ItemType=Kind==0?ACEItemType::Jewelry:Kind==1?ACEItemType::Clothing:ACEItemType::Armor;
        I.ValidLocations=Kind==0?0x100:Kind==1?ACEEquipMask::ChestWear:ACEEquipMask::ChestArmor;I.ClothingPriority=Kind==1?ACECoverageMask::UnderwearChest:0;
        auto& A=P.A;A.ObjectGuid=I.Guid;A.bSuccess=true;A.ItemType=I.ItemType;A.IntProperties={{171,0},{105,7}};
        if(Kind==1)A.IntProperties.Add(28,0);
        if(Kind==2){I.WeenieClassId=25638;A.IntProperties.Add(158,7);A.IntProperties.Add(159,1);A.IntProperties.Add(160,150);A.IntProperties.Add(28,304);A.ArmorResistances={1,1,1,.5,.5,.5,0,.6};}
        if(Kind==3)
        {
            for(const auto& T:LootArmorTemplates)if(T.Family==3){I.WeenieClassId=T.WCID;break;}
            const FLootArmorCohort* G=nullptr;for(const auto& Candidate:LootArmorCohorts)if(Candidate.Family==3&&(!G||Candidate.Wield>G->Wield))G=&Candidate;
            if(G){A.IntProperties.Add(158,G->Kind);A.IntProperties.Add(159,G->Skill);A.IntProperties.Add(160,G->Wield);A.IntProperties.Add(28,int32(G->Bounds.High));A.IntProperties.Add(36,9999);if(G->Level){A.IntProperties.Add(270,7);A.IntProperties.Add(272,G->Level);}}
            A.ArmorResistances={1.6,1.6,1.6,1.6,1.6,1.6,0,1.6};
        }
    }
    auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("version"),TEXT("v171"));
    Root->SetStringField(TEXT("context"),TEXT("Marginal score from one cantrip, relative to the listed fixture. Support skills trained at 300; primary skill specialized at 800; other offensive skills untrained. All grades are custom utility, not drop rarity."));
    TArray<TSharedPtr<FJsonValue>> ProfileJson,Cantrips,Native,Sets,WeaponRanges,ArmorRanges;
    for(auto& P:Profiles)
    {
        const auto R=ACELootQuality::Evaluate(P.Item,P.A,P.C,{},Dat);P.Base=R.Points;
        TestTrue(FString(TEXT("Rated audit fixture: "))+P.Name,R.bRated);
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),P.Name);J->SetNumberField(TEXT("base"),P.Base);J->SetNumberField(TEXT("wield"),P.A.IntProperties.FindRef(160));J->SetNumberField(TEXT("wcid"),P.Item.WeenieClassId);ProfileJson.Add(MakeShared<FJsonValueObject>(J));
    }
    int32 Count=0;TSet<int32> Families;
    for(const auto& S:LootSpells)
    {
        FString Name;uint32 Icon=0;if(!Dat->TryGetSpellInfo(S.Id,Name,Icon))Name=FString::Printf(TEXT("Spell %d"),S.Id);
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("id"),S.Id);J->SetStringField(TEXT("name"),Name);J->SetNumberField(TEXT("type"),S.Type);J->SetNumberField(TEXT("key"),S.Key);J->SetNumberField(TEXT("amount"),S.Amount);J->SetNumberField(TEXT("family"),S.Family);J->SetNumberField(TEXT("rank"),S.Rank);
        if(!S.Family){Native.Add(MakeShared<FJsonValueObject>(J));continue;}++Count;Families.Add(S.Family);
        TArray<TSharedPtr<FJsonValue>> Scores,Tiers;
        for(const auto& P:Profiles){auto A=P.A;A.SpellBookEntries={int32(S.Id)};const auto R=ACELootQuality::Evaluate(P.Item,A,P.C,{},Dat);Scores.Add(MakeShared<FJsonValueNumber>(R.Points-P.Base));const auto* Grade=R.SpellGrades.Find(Name);Tiers.Add(MakeShared<FJsonValueNumber>(Grade?int32(*Grade):-1));}
        if((S.Type&255)==1)for(const auto& Value:Scores)TestTrue(TEXT("Every attribute earns positive credit in every profile"),Value->AsNumber()>0);
        for(int32 P=41;P<Profiles.Num();++P)TestTrue(TEXT("No single cantrip grants a perfect item score"),Scores[P]->AsNumber()<100);
        J->SetArrayField(TEXT("scores"),Scores);J->SetArrayField(TEXT("tiers"),Tiers);Cantrips.Add(MakeShared<FJsonValueObject>(J));
    }
    TestEqual(TEXT("Every cantrip ID is audited"),Count,297);TestEqual(TEXT("Every canonical effect family is audited"),Families.Num(),69);
    for(int32 Set=1;Set<=140;++Set)
    {
        TArray<uint32> Spells;if(!Dat->TryGetEquipmentSetSpells(Set,MAX_uint32,Spells))continue;
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("id"),Set);J->SetStringField(TEXT("name"),ACEAppraisalFormatting::EquipmentSetName(Set));TArray<TSharedPtr<FJsonValue>> Scores;
        for(const auto& P:Profiles){auto A=P.A;A.IntProperties.Add(265,Set);Scores.Add(MakeShared<FJsonValueNumber>(ACELootQuality::Evaluate(P.Item,A,P.C,{},Dat).Points-P.Base));}
        if(Set==21)for(int32 P=41;P<Profiles.Num();++P)TestTrue(TEXT("Wise has positive potential for every wearable build"),Scores[P]->AsNumber()>0);
        TArray<TSharedPtr<FJsonValue>> Effects;for(uint32 Id:Spells)Effects.Add(MakeShared<FJsonValueNumber>(Id));J->SetArrayField(TEXT("effects"),Effects);
        J->SetArrayField(TEXT("scores"),Scores);Sets.Add(MakeShared<FJsonValueObject>(J));
    }
    for(const auto& G:LootCohorts)
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("class"),LootVariants[G.Variant].Label);J->SetNumberField(TEXT("requirement_kind"),G.Kind);J->SetNumberField(TEXT("skill"),G.Skill);J->SetNumberField(TEXT("wield"),G.Wield);
        const TCHAR* Keys[]={TEXT("Maximum damage"),TEXT("Variance"),TEXT("Attack multiplier"),TEXT("Melee defense multiplier"),TEXT("Launcher multiplier"),TEXT("Elemental damage bonus"),TEXT("Spell damage multiplier"),TEXT("Mean hit"),TEXT("Mana conversion")};
        for(int32 K=0;K<9;++K)if(G.Bounds[K].Low>=0)J->SetArrayField(Keys[K],{MakeShared<FJsonValueNumber>(G.Bounds[K].Low),MakeShared<FJsonValueNumber>(G.Bounds[K].High)});WeaponRanges.Add(MakeShared<FJsonValueObject>(J));
    }
    for(const auto& G:LootArmorCohorts){auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("family"),G.Family);J->SetNumberField(TEXT("requirement_kind"),G.Kind);J->SetNumberField(TEXT("skill"),G.Skill);J->SetNumberField(TEXT("wield"),G.Wield);J->SetNumberField(TEXT("level"),G.Level);J->SetArrayField(TEXT("AL"),{MakeShared<FJsonValueNumber>(G.Bounds.Low),MakeShared<FJsonValueNumber>(G.Bounds.High)});ArmorRanges.Add(MakeShared<FJsonValueObject>(J));}
    Root->SetArrayField(TEXT("profiles"),ProfileJson);Root->SetArrayField(TEXT("cantrips"),Cantrips);Root->SetArrayField(TEXT("ordinary_effects"),Native);Root->SetArrayField(TEXT("sets"),Sets);Root->SetArrayField(TEXT("weapon_ranges"),WeaponRanges);Root->SetArrayField(TEXT("armor_ranges"),ArmorRanges);
    TArray<TSharedPtr<FJsonValue>> Weights,Gear;
    const TCHAR* RoleNames[]={TEXT("Irrelevant"),TEXT("Damage"),TEXT("Attack"),TEXT("Defender"),TEXT("Melee defense"),TEXT("Magic defense"),TEXT("Missile defense"),TEXT("Matching weapon/magic skill"),TEXT("Primary attribute"),TEXT("Secondary attribute"),TEXT("Survival attribute"),TEXT("Useful support skill"),TEXT("Character ward"),TEXT("Armor"),TEXT("Mana efficiency"),TEXT("Speed"),TEXT("Universal attribute"),TEXT("Shield"),TEXT("Regeneration"),TEXT("Crafting"),TEXT("Utility"),TEXT("Movement"),TEXT("Social"),TEXT("Appraisal"),TEXT("Health vital"),TEXT("Stamina vital"),TEXT("Mana vital"),TEXT("Native rating set effect")};
    for(int32 K=0;K<UE_ARRAY_COUNT(RoleNames);++K){const auto Effect=static_cast<ACELootScoring::Role>(K);auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),RoleNames[K]);J->SetNumberField(TEXT("weapon"),ACELootScoring::LegendaryPoints(Effect,false));J->SetNumberField(TEXT("caster"),ACELootScoring::LegendaryPoints(Effect,true));J->SetNumberField(TEXT("wearable"),K?100*ACELootScoring::WearableRelevanceFactor(Effect):0);Weights.Add(MakeShared<FJsonValueObject>(J));}
    for(int32 Key=370;Key<=379;++Key){auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("key"),Key);for(int32 Value=1;Value<=3;++Value)J->SetNumberField(FString::FromInt(Value),ACELootScoring::NativeGearPoints(Key,Value));J->SetNumberField(TEXT("armor_maximum"),ACELootScoring::NativeGearPoints(Key,2,2));Gear.Add(MakeShared<FJsonValueObject>(J));}
    Root->SetArrayField(TEXT("weights"),Weights);Root->SetArrayField(TEXT("gear_ratings"),Gear);
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));
    TestTrue(TEXT("Native audit exported"),FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectSavedDir()/TEXT("Loot-rating-audit.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    Dat->Deinitialize();return !HasAnyErrors();
}
