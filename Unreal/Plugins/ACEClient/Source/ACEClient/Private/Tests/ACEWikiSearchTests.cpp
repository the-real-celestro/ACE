#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Knowledge/ACEWikiReference.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWikiSearchTest,"ACE.Wiki.TitleSearch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEWikiSearchTest::RunTest(const FString&)
{
    using namespace ACEWikiReference;
    TSharedPtr<FJsonObject> Root;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(
        "{\"query\":{\"search\":[{\"ns\":0,\"title\":\"Hunting Aun Ralirea\"},"
        "{\"ns\":0,\"title\":\"Aun Ralirea\"},{\"ns\":0,\"title\":\"Aun Ralirea (Rumor)\"},"
        "{\"ns\":6,\"title\":\"File:Aun Ralirea Live.jpg\"},{\"ns\":0,\"title\":\"Aun Ralirea\"}]}}"))),Root);
    TArray<FString> Titles;
    TestTrue(TEXT("Title results parse"),ParseSuggestions(Root,Titles));
    TestEqual(TEXT("Files and duplicate titles are excluded"),Titles.Num(),3);
    TestEqual(TEXT("Screenshot's lowercase query opens the canonical title, even when ranked after a quest"),
        ChooseSearchTitle(TEXT("aun ralirea"),Titles),FString(TEXT("Aun Ralirea")));
    TestEqual(TEXT("Underscore spelling resolves too"),ChooseSearchTitle(TEXT(" aun_ralirea "),Titles),FString(TEXT("Aun Ralirea")));
    TestTrue(TEXT("Ambiguous partial query asks the user to choose"),ChooseSearchTitle(TEXT("aun"),Titles).IsEmpty());
    TestTrue(TEXT("Wildcard query does not silently select the wrong article"),ChooseSearchTitle(TEXT("aun ralirea*"),Titles).IsEmpty());
    TestEqual(TEXT("Unique partial result can open"),ChooseSearchTitle(TEXT("aether"),{TEXT("Aetheria")}),FString(TEXT("Aetheria")));
    TestTrue(TEXT("Query encoding preserves literal ampersands as data"),SuggestUrl(ESource::ACPedia,TEXT("A&B*")).Contains(TEXT("A%26B%2A")));
    TestTrue(TEXT("Wildcard-only input cannot request the whole wiki"),SuggestUrl(ESource::ACPedia,TEXT(" *** ")).IsEmpty());
    TestFalse(TEXT("Transport failure is not reported as an empty successful search"),ParseSuggestions(nullptr,Titles));
    TestTrue(TEXT("A failed parse clears stale results"),Titles.IsEmpty());
    return true;
}
#endif
