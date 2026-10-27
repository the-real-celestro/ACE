#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Knowledge/ACEWikiReference.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWikiReferenceTest,"ACE.Wiki.NativeReference",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEWikiReferenceTest::RunTest(const FString&)
{
    using namespace ACEWikiReference;
    TArray<FBlock> Blocks;TArray<FString> Links;bool Truncated=false;
    FormatHtml(TEXT("<h2>Uses</h2><p>Hide &amp; trophies &#8212; trade</p><script>secret()</script>"
        "<style>badstyle</style><p><a href='/wiki/Hoary_Mattekar_Hide#Uses'>Hide</a>"
        "<a href='https://outside.invalid/wiki/Bad'>External</a><a href='/wiki/Special:Login'>Login</a></p>"
        "<table><tr><td>Chance</td><td>3%</td></tr></table>"),Blocks,Links,Truncated);
    TestTrue(TEXT("Heading retained"),Blocks.Num()>0&&Blocks[0].Heading&&Blocks[0].Text==TEXT("Uses"));
    TestTrue(TEXT("Readable entities"),Blocks.ContainsByPredicate([](const FBlock& B){return B.Text.Contains(TEXT("Hide & trophies — trade"));}));
    TestFalse(TEXT("Executable/style content omitted"),Blocks.ContainsByPredicate([](const FBlock& B){return B.Text.Contains(TEXT("secret"))||B.Text.Contains(TEXT("badstyle"));}));
    TestEqual(TEXT("Only article links offered"),Links.Num(),1);
    if(Links.Num())TestEqual(TEXT("Link title decoded and fragment removed"),Links[0],FString(TEXT("Hoary Mattekar Hide")));
    TestTrue(TEXT("Table values remain readable"),Blocks.ContainsByPredicate([](const FBlock& B){return B.Text.Contains(TEXT("Chance  ·  3%"));}));
    FormatHtml(FString::ChrN(1024*1024+1,'x'),Blocks,Links,Truncated);
    TestTrue(TEXT("Oversized markup refused"),Truncated&&Blocks.IsEmpty());
    auto Read=[](const FString& Json){TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root);return Root;};
    FArticle Article;
    TestFalse(TEXT("Missing article is not fabricated"),ParseArticle(Read(TEXT("{\"error\":{\"code\":\"missingtitle\"}}")),ESource::ACPedia,Article));
    TestTrue(TEXT("Article title and revision required"),ParseArticle(Read(TEXT("{\"parse\":{\"title\":\"Mattekar\",\"revid\":12,\"text\":{\"*\":\"<p>Creature</p>\"}}}")),ESource::ACPedia,Article));
    TestEqual(TEXT("Provenance revision retained"),Article.Revision,int64(12));
    TestEqual(TEXT("Source URL stays on ACPedia"),Article.Url,FString(TEXT("https://acpedia.celestro.com/wiki/Mattekar")));
    FEntry Entry;Entry.Title=TEXT("A & B?x=1");
    TestTrue(TEXT("Titles URL encoded"),ParseUrl(Entry).Contains(TEXT("A%20%26%20B%3Fx%3D1")));
    return true;
}
#endif
