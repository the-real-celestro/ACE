#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../UI/ACEAppraisalKnowledge.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEExaminationKnowledgeTest,"ACE.Inspection.Knowledge",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEExaminationKnowledgeTest::RunTest(const FString&)
{
    FACEWorldKnowledge Catalog;FString Error;
    const FString Json=TEXT(R"({"schema":1,"source":"Test baseline","creatures":[{"wcid":5890,"name":"Hoary Mattekar","melee_types":2,"resistances":{"65":0.9,"67":1.0,"68":0},"spells":[],"drops":[{"wcid":5892,"name":"Hoary Mattekar Hide","chance":0.03},{"wcid":9,"name":"Unknown trophy"}]}]})");
    if(!TestTrue(TEXT("Bounded catalog parses"),Catalog.LoadJson(Json,Error)))return false;
    FACEAppraisalInfo Info;Info.bIsCreature=Info.bSuccess=true;Info.Name=TEXT("Hoary Mattekar");
    FACEWorldObject Subject;Subject.WeenieClassId=5890;Subject.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;
    auto Rows=ACEAppraisalKnowledge::CreatureInsights(Info,&Subject,nullptr,Catalog);
    auto Has=[&](const FString& Text){return Rows.ContainsByPredicate([&](const auto& Row){return Row.Value.Contains(Text);});};
    TestTrue(TEXT("Monster attack types are explicit"),Has(TEXT("Melee: Piercing")));
    TestTrue(TEXT("No spell list does not claim the monster cannot cast"),Has(TEXT("No spells listed")));
    TestTrue(TEXT("Known drop has a labelled baseline rate"),Has(TEXT("Hoary Mattekar Hide — baseline 3.00%")));
    TestTrue(TEXT("Absent rate stays unknown"),Has(TEXT("Unknown trophy — rate unknown")));
    TestTrue(TEXT("Known zero resistance multiplier is retained"),Has(TEXT("Cold: x0.00 (reference)")));
    Info.FloatProperties.Add(67,.2);
    Rows=ACEAppraisalKnowledge::CreatureInsights(Info,&Subject,nullptr,Catalog);
    TestTrue(TEXT("Live appraisal supersedes reference per field"),Has(TEXT("Fire: x0.20 (appraised)")));
    TestTrue(TEXT("Relative weakness updates with the live value"),Has(TEXT("known types: Piercing")));
    Info.bSuccess=false;Rows=ACEAppraisalKnowledge::CreatureInsights(Info,&Subject,nullptr,Catalog);
    TestTrue(TEXT("Failed appraisal cannot disclose cached live resistance"),Has(TEXT("Fire: x1.00 (reference)")));
    const auto Compact=ACEAppraisalKnowledge::CompactCreatureInsights(Info,&Subject,nullptr,Catalog);
    TArray<FString> Weaknesses;
    for(const auto& R:Compact)if(R.TileGroup==2)Weaknesses.Add(R.Tooltip);
    TestTrue(TEXT("Vulnerabilities precede attack groups"),Compact.Num()>0&&Compact[0].Value==TEXT("Vulnerabilities"));
    TestTrue(TEXT("All known damage types are sorted strongest first"),Weaknesses.Num()==3
        &&Weaknesses[0].StartsWith(TEXT("Fire:"))&&Weaknesses[1].StartsWith(TEXT("Piercing:"))&&Weaknesses[2].StartsWith(TEXT("Cold:")));
    TestTrue(TEXT("Percentages and names are visible without hovering"),Compact.ContainsByPredicate([](const auto& R){return R.TileGroup==2&&R.DisplayValue==TEXT("Piercing 90%");}));
    TestTrue(TEXT("Known immunity remains visible as zero percent"),Compact.ContainsByPredicate([](const auto& R){return R.TileGroup==2&&R.DisplayValue==TEXT("Cold 0%");}));
    for(const TCHAR* Heading:{TEXT("Common Attacks"),TEXT("Melee"),TEXT("Magic"),TEXT("Missile")})
        TestTrue(Heading,Compact.ContainsByPredicate([&](const auto& R){return R.bFullWidth&&R.Value==Heading;}));
    TestFalse(TEXT("Melee damage cannot masquerade as missile damage"),Compact.ContainsByPredicate([](const auto& R){return R.TileGroup==5;}));
    FACEWorldKnowledge MissileCatalog;
    TestTrue(TEXT("Optional missile types parse"),MissileCatalog.LoadJson(Json.Replace(TEXT("\"melee_types\":2"),TEXT("\"melee_types\":2,\"missile_types\":64")),Error));
    const auto MissileRows=ACEAppraisalKnowledge::CompactCreatureInsights(Info,&Subject,nullptr,MissileCatalog);
    TestTrue(TEXT("Confirmed missile types get their own group"),MissileRows.ContainsByPredicate([](const auto& R){return R.TileGroup==5&&R.Tooltip.StartsWith(TEXT("Missile: Lightning"));}));
    TestTrue(TEXT("Compact attack tile retains piercing identity"),Compact.ContainsByPredicate([](const auto& R){return R.TileGroup==1&&R.Tooltip.Contains(TEXT("Piercing"));}));
    TestTrue(TEXT("Compact weakness tile retains numeric value and provenance"),Compact.ContainsByPredicate([](const auto& R){return R.TileGroup==2&&R.Tooltip.Contains(TEXT("Fire: x1.00 (reference)"));}));
    FACEAppraisalInfo Tied=Info;Tied.bSuccess=true;Tied.FloatProperties.Add(65,1.0);Tied.FloatProperties.Add(67,1.0);
    const auto TiedRows=ACEAppraisalKnowledge::CompactCreatureInsights(Tied,&Subject,nullptr,Catalog);
    TestEqual(TEXT("Ties and lower known types remain visible"),TiedRows.FilterByPredicate([](const auto& R){return R.TileGroup==2;}).Num(),3);
    TestTrue(TEXT("Trophy tile retains its name and baseline rate on hover"),Compact.ContainsByPredicate([](const auto& R){return R.TileGroup==3&&R.Tooltip.Contains(TEXT("Hoary Mattekar Hide — baseline 3.00%"));}));
    Subject.bIsPlayer=true;
    TestTrue(TEXT("Player inspection is unchanged"),ACEAppraisalKnowledge::CreatureInsights(Info,&Subject,nullptr,Catalog).IsEmpty());
    Subject.bIsPlayer=false;Subject.WeenieClassId=999999;
    Rows=ACEAppraisalKnowledge::CreatureInsights(Info,&Subject,nullptr,Catalog);
    TestTrue(TEXT("An equal name cannot borrow a different template's drops"),Has(TEXT("Drop information unavailable")));
    TestFalse(TEXT("Malformed rates rejected atomically"),Catalog.LoadJson(Json.Replace(TEXT("0.03"),TEXT("1.5")),Error));
    TestNotNull(TEXT("Previous valid catalog remains after rejection"),Catalog.Find(5890));
    const auto* Bundled=FACEWorldKnowledge::Get().Find(5890);
    TestNotNull(TEXT("Actual public retail catalog is bundled"),Bundled);
    if(Bundled)
        TestTrue(TEXT("Actual hide entry has its standard-setting baseline chance"),Bundled->Drops.ContainsByPredicate([](const auto& D){return D.Wcid==5892&&D.BaselineChance.IsSet()&&FMath::IsNearlyEqual(D.BaselineChance.GetValue(),.03,1e-7);}));

    FACEAppraisalInfo Weapon;Weapon.bSuccess=Weapon.bHasWeaponProfile=true;Weapon.Damage=50;
    Weapon.WeaponEnchantments=8u|(8u<<16);Weapon.SpellBookEntries={int32(0x80000001u)};
    auto Text=ACEAppraisalFormatting::ItemEnchantmentComparison(Weapon);
    TestTrue(TEXT("Missing base damage never invents a default roll"),Text.Contains(TEXT("Damage: 50 -> Not supplied")));
    Weapon.IntProperties.Add(44,0);
    TestTrue(TEXT("Explicit raw zero is a known base value"),ACEAppraisalFormatting::ItemEnchantmentComparison(Weapon).Contains(TEXT("Damage: 50 -> 0")));
    Weapon.IntProperties[44]=35;
    TestTrue(TEXT("Current and raw instance values compare without changing the appraisal"),ACEAppraisalFormatting::ItemEnchantmentComparison(Weapon).Contains(TEXT("Damage: 50 -> 35")));
    TestEqual(TEXT("Comparison does not mutate current damage"),Weapon.Damage,50);
    Weapon.bSuccess=false;
    TestTrue(TEXT("Failed item appraisal exposes no comparison"),ACEAppraisalFormatting::ItemEnchantmentComparison(Weapon).IsEmpty());
    Weapon.bSuccess=true;Weapon.SpellBookEntries={1};Weapon.WeaponEnchantments=0;
    TestTrue(TEXT("Innate spells alone do not count as active item enchantments"),ACEAppraisalFormatting::ItemEnchantmentComparison(Weapon).IsEmpty());
    return !HasAnyErrors();
}
#endif
