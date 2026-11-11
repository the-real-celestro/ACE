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
    FormatHtml(TEXT("<div id='toc'><h2>Contents</h2><ul><li>Clutter</li></ul></div>"
        "<h2>Walk Through<span class='mw-editsection'><span>[</span>edit<span>]</span></span></h2>"
        "<p>Bring the key.</p><img src='/images/a/ab/Quest.jpg'>"
        "<img src='https://outside.invalid/tracker.png'><div class='navbox'><p>Website navigation</p></div>"),Blocks,Links,Truncated);
    TestTrue(TEXT("Clean section heading preserved"),Blocks.ContainsByPredicate([](const auto& B){return B.Heading&&B.Text==TEXT("Walk Through");}));
    TestTrue(TEXT("Guide instructions survive filtering"),Blocks.ContainsByPredicate([](const auto& B){return B.Text==TEXT("Bring the key.");}));
    TestFalse(TEXT("Web navigation and edit controls removed"),Blocks.ContainsByPredicate([](const auto& B){return B.Text.Contains(TEXT("Clutter"))||B.Text.Contains(TEXT("edit"))||B.Text.Contains(TEXT("Website navigation"));}));
    TestEqual(TEXT("Only restored-site images retained"),Blocks.FilterByPredicate([](const auto& B){return !B.ImageUrl.IsEmpty();}).Num(),1);
    FormatHtml(TEXT("<table><tr><th>Strength</th><td>400</td></tr><tr><th>Endurance</th><td>150</td></tr>"
        "<tr><th>Coordination</th><td>190</td></tr></table><p>Bring the key.</p>"
        "<img src='/images/a/ab/Image_Needed_Live.jpg'><p>Strength: 150 required for this quest.</p>"),Blocks,Links,Truncated);
    TestTrue(TEXT("Attribute rows become one semantic section"),Blocks.Num()>0&&Blocks[0].Attributes.Num()==3);
    if(Blocks.Num()&&Blocks[0].Attributes.Num()==3)
    {TestEqual(TEXT("Attribute name retained"),Blocks[0].Attributes[2].Name,FString(TEXT("Coordination")));
        TestEqual(TEXT("Attribute value retained without table punctuation"),Blocks[0].Attributes[2].Value,FString(TEXT("190")));}
    TestTrue(TEXT("Quest requirements are not mistaken for infobox stats"),Blocks.ContainsByPredicate([](const auto& B){return B.Text==TEXT("Strength: 150 required for this quest.");}));
    TestFalse(TEXT("Image Needed artwork is omitted"),Blocks.ContainsByPredicate([](const auto& B){return !B.ImageUrl.IsEmpty();}));
    FormatHtml(TEXT("<p>Strength</p><p>1,000</p><p>Endurance</p><p>&#63;&#63;&#63;</p><p>Health</p><p>250 / 250</p>"),Blocks,Links,Truncated);
    TestTrue(TEXT("Split table cells also form an attribute section"),Blocks.Num()==1&&Blocks[0].Attributes.Num()==3);
    FormatHtml(TEXT("<p>Strength: 400</p><p>Bring a key.</p>"),Blocks,Links,Truncated);
    TestTrue(TEXT("An isolated requirement is left in its original context"),Blocks.Num()==2&&Blocks[0].Attributes.IsEmpty());
    FormatHtml(TEXT("<img src='/images/e/ed/Live.jpg' width='266' height='400'>"
        "<img src='/images/thumb/e/ed/Live.jpg/200px-Live.jpg'>"
        "<img src='/images/6/62/Icon_Horizontal_Spacer.png' width='1' height='34'>"
        "<img src='/images/a/ab/Creature.jpg' width='266' height='400'>"),Blocks,Links,Truncated);
    TestTrue(TEXT("Real portrait survives Live.jpg placeholders and spacer clutter"),Blocks.Num()==1&&Blocks[0].Portrait&&Blocks[0].ImageUrl.EndsWith(TEXT("Creature.jpg")));
    FormatHtml(TEXT("<table><tr><td>Olthoi<br>(Flyer)</td><td>Level<br>50</td></tr>"
        "<tr><th>Strength</th><td>100</td></tr><tr><th>Endurance</th><td>200</td></tr><tr><th>Coordination</th><td>120</td></tr>"
        "<tr><td></td><th>XP</th><td>10,000</td><td></td></tr><tr><th>Luminance</th><td>0</td></tr><tr><th>Loot Tier</th><td>3</td></tr>"
        "<tr><th>Trophies</th></tr><tr><td>&#63;&#63;</td></tr><tr><th>Common Attacks</th><th>Weaknessess</th></tr>"
        "<tr><td>Pierce, Acid</td><td>Bludgeon, Pierce</td></tr>"
        "<tr><th>Melee A</th><th>Missile A</th><th>Magic A</th><th>Melee D</th><th>Missile D</th><th>Magic D</th></tr>"
        "<tr><td>213</td><td>--</td><td>--</td><td>223</td><td>&#160;</td><td>185</td></tr></table>"),Blocks,Links,Truncated);
    const auto* Identity=Blocks.FindByPredicate([](const auto& B){return B.Layout==ELayout::Identity;});
    TestTrue(TEXT("Species and level form a compact identity row"),Identity&&Identity->Fields.Num()==2&&Identity->Fields[0].Value==TEXT("50"));
    const auto* Rewards=Blocks.FindByPredicate([](const auto& B){return B.Layout==ELayout::Rewards;});
    TestTrue(TEXT("XP, luminance and loot tier form one reward summary"),Rewards&&Rewards->Fields.Num()==3&&Rewards->Fields[0].Value==TEXT("10,000"));
    const auto* Combat=Blocks.FindByPredicate([](const auto& B){return B.Layout==ELayout::Combat;});
    TestTrue(TEXT("Attacks cannot be confused with weaknesses"),Combat&&Combat->Fields.Num()==2
        &&Combat->Fields[0].Value==TEXT("Pierce, Acid")&&Combat->Fields[1].Value==TEXT("Bludgeon, Pierce"));
    const auto* Ratings=Blocks.FindByPredicate([](const auto& B){return B.Layout==ELayout::Ratings;});
    TestTrue(TEXT("Empty missile defense does not shift magic defense"),Ratings&&Ratings->Fields.Num()==6
        &&Ratings->Fields[4].Value==TEXT("Unknown")&&Ratings->Fields[5].Value==TEXT("185"));
    TestTrue(TEXT("Unspecified trophies are a labelled unknown value"),Blocks.ContainsByPredicate([](const auto& B){return B.Layout==ELayout::Trophies&&B.Fields[0].Value==TEXT("Unknown");}));
    TArray<FLinearColor> Capture;Capture.Init(FLinearColor(0,0,0,1),40*40);
    TArray<uint8> BGRA;FIntPoint Size;
    TestFalse(TEXT("Empty Examine capture cannot become a misleading portrait"),MakeExaminePortrait(Capture,40,40,BGRA,Size));
    Capture[20*40+20]=FLinearColor(.25f,0,0,.5f);
    TestTrue(TEXT("Examine inverse-alpha capture becomes a cropped portrait"),MakeExaminePortrait(Capture,40,40,BGRA,Size));
    TestEqual(TEXT("Empty capture margins are cropped"),Size,FIntPoint(9,9));
    if(BGRA.Num()==9*9*4)
    {
        const int32 Center=(4*9+4)*4;
        TestTrue(TEXT("Coverage alpha is inverted"),FMath::Abs(int32(BGRA[Center+3])-128)<=1);
        TestTrue(TEXT("Premultiplied linear color is restored without a black fringe"),FMath::Abs(int32(BGRA[Center+2])-188)<=1);
        TestEqual(TEXT("Background remains transparent"),BGRA[3],uint8(0));
        Capture[20*40+20]=FLinearColor(0,0,0,1);
        TestTrue(TEXT("Captured portrait is independent of later Examine changes"),BGRA[Center+3]>0);
    }
    return true;
}
#endif
