#include "Knowledge/ACEWikiReference.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Internationalization/Regex.h"

namespace ACEWikiReference
{
FString SourceName(ESource S){return S==ESource::ACPedia?TEXT("ACPedia"):TEXT("Fandom");}
FString Endpoint(ESource S){return S==ESource::ACPedia?TEXT("https://acpedia.celestro.com/api.php"):TEXT("https://asheron.fandom.com/api.php");}
FString ArticleUrl(ESource S,const FString& Title)
{return (S==ESource::ACPedia?TEXT("https://acpedia.celestro.com/wiki/"):TEXT("https://asheron.fandom.com/wiki/"))+FGenericPlatformHttp::UrlEncode(Title.Replace(TEXT(" "),TEXT("_")));}
FString SearchUrl(ESource S,const FString& Query)
{return Endpoint(S)+TEXT("?action=query&generator=search&gsrnamespace=0&gsrlimit=15&prop=categories%7Cinfo&inprop=url&cllimit=30&format=json&gsrsearch=")+FGenericPlatformHttp::UrlEncode(Query.Left(128));}
FString ParseUrl(const FEntry& E)
{return Endpoint(E.Source)+TEXT("?action=parse&prop=text%7Crevid%7Cdisplaytitle&format=json&page=")+FGenericPlatformHttp::UrlEncode(E.Title);}

bool ParseSearch(const TSharedPtr<FJsonObject>& Root,ESource S,TArray<FEntry>& Out)
{
    if(!Root||Root->HasField(TEXT("error")))return false;
    const TSharedPtr<FJsonObject>* Query=nullptr;const TSharedPtr<FJsonObject>* Pages=nullptr;
    if(!Root->TryGetObjectField(TEXT("query"),Query))return Root->HasField(TEXT("batchcomplete"));
    if(!(*Query)->TryGetObjectField(TEXT("pages"),Pages))return true;
    if((*Pages)->Values.Num()>50)return false;
    TArray<FEntry> Parsed;
    for(const auto& Pair:(*Pages)->Values)
    {
        const TSharedPtr<FJsonObject>* Page=nullptr;if(!Pair.Value->TryGetObject(Page))return false;
        if((*Page)->HasField(TEXT("missing")))continue;
        FEntry E;E.Source=S;double Id=0,Ns=0;
        if(!(*Page)->TryGetStringField(TEXT("title"),E.Title)||E.Title.IsEmpty()||E.Title.Len()>256
            ||!(*Page)->TryGetNumberField(TEXT("pageid"),Id)||Id<1||Id>MAX_int32)return false;
        if((*Page)->TryGetNumberField(TEXT("ns"),Ns)&&Ns!=0)continue;
        E.PageId=int32(Id);E.Url=ArticleUrl(S,E.Title);E.Kind=TEXT("Reference");
        const TArray<TSharedPtr<FJsonValue>>* Categories=nullptr;
        if((*Page)->TryGetArrayField(TEXT("categories"),Categories))
        {
            if(Categories->Num()>100)return false;
            TSet<FString> Names;
            for(const auto& C:*Categories){const TSharedPtr<FJsonObject>* O=nullptr;FString Title;
                if(C->TryGetObject(O)&&(*O)->TryGetStringField(TEXT("title"),Title))Names.Add(Title.ToLower().Replace(TEXT("category:"),TEXT("")));}
            if(Names.Contains(TEXT("quest"))||Names.Contains(TEXT("quests")))E.Kind=TEXT("Quests");
            else if(Names.Contains(TEXT("creature"))||Names.Contains(TEXT("creatures")))E.Kind=TEXT("Monsters");
            else if(Names.Contains(TEXT("npc"))||Names.Contains(TEXT("npcs")))E.Kind=TEXT("NPCs");
            else if(Names.Contains(TEXT("portal"))||Names.Contains(TEXT("portals")))E.Kind=TEXT("Portals");
            else if(Names.Contains(TEXT("dungeon"))||Names.Contains(TEXT("town"))||Names.Contains(TEXT("location"))||Names.Contains(TEXT("landmark")))E.Kind=TEXT("Locations");
            else if(Names.Contains(TEXT("item"))||Names.Contains(TEXT("trophy"))||Names.Contains(TEXT("quest items"))||Names.Contains(TEXT("armor"))||Names.Contains(TEXT("weapon")))E.Kind=TEXT("Items");
        }
        Parsed.Add(MoveTemp(E));
    }
    Out=MoveTemp(Parsed);return true;
}

namespace
{
FString Decode(FString Text)
{
    // Decode text, never execute markup. Decode ampersands last to avoid repeated decoding.
    for(const auto& Pair:TArray<TPair<FString,FString>>{{TEXT("&nbsp;"),TEXT(" ")},{TEXT("&lt;"),TEXT("<")},{TEXT("&gt;"),TEXT(">")},
        {TEXT("&quot;"),TEXT("\"")},{TEXT("&apos;"),TEXT("'")}})Text.ReplaceInline(*Pair.Key,*Pair.Value,ESearchCase::CaseSensitive);
    FRegexMatcher M(FRegexPattern(TEXT("&#(x[0-9a-fA-F]+|[0-9]+);")),Text);FString Result;int32 At=0;
    while(M.FindNext())
    {
        Result+=Text.Mid(At,M.GetMatchBeginning()-At);FString Code=M.GetCaptureGroup(1);
        uint64 Value=FCString::Strtoui64(*(Code.StartsWith(TEXT("x"))?Code.Mid(1):Code),nullptr,Code.StartsWith(TEXT("x"))?16:10);
        if(Value>0&&Value<=0x10ffff&&!(Value>=0xd800&&Value<=0xdfff))
        {if(Value<=0xffff)Result.AppendChar(TCHAR(Value));else{Value-=0x10000;Result.AppendChar(TCHAR(0xd800+(Value>>10)));Result.AppendChar(TCHAR(0xdc00+(Value&1023)));}}
        At=M.GetMatchEnding();
    }
    Result+=Text.Mid(At);Result.ReplaceInline(TEXT("&amp;"),TEXT("&"),ESearchCase::CaseSensitive);return Result;
}
FString Remove(const FString& Text,const FString& Pattern)
{
    FRegexMatcher M(FRegexPattern(Pattern),Text);FString Out;int32 At=0;
    while(M.FindNext()){Out+=Text.Mid(At,M.GetMatchBeginning()-At);At=M.GetMatchEnding();}return Out+Text.Mid(At);
}
}
void FormatHtml(const FString& Html,TArray<FBlock>& Blocks,TArray<FString>& Links,bool& Truncated)
{
    Blocks.Reset();Links.Reset();Truncated=false;
    if(Html.Len()>1024*1024){Truncated=true;return;}
    FString Clean=Remove(Html,TEXT("(?is)<(?:script|style|noscript)\\b[^>]*>.*?</(?:script|style|noscript)\\s*>"));
    Clean=Remove(Clean,TEXT("(?s)<!--.*?-->"));
    FRegexMatcher Tokens(FRegexPattern(TEXT("(?s)<[^>]*>|[^<]+")),Clean);
    FString Text;bool Heading=false,LastSpace=false;
    auto Flush=[&](){Text.TrimStartAndEndInline();if(!Text.IsEmpty())Blocks.Add({Text,Heading});Text.Empty();LastSpace=false;};
    while(Tokens.FindNext())
    {
        if(Blocks.Num()>=1024){Truncated=true;break;}
        const FString Token=Tokens.GetCaptureGroup(0);
        if(!Token.StartsWith(TEXT("<")))
        {
            for(TCHAR C:Decode(Token))
            {if(FChar::IsWhitespace(C)){if(!LastSpace&&!Text.IsEmpty())Text.AppendChar(' ');LastSpace=true;}else{Text.AppendChar(C);LastSpace=false;}}
            continue;
        }
        FRegexMatcher Tag(FRegexPattern(TEXT("^<\\s*(/?)\\s*([a-zA-Z0-9]+)")),Token);if(!Tag.FindNext())continue;
        const bool Closing=!Tag.GetCaptureGroup(1).IsEmpty();const FString Name=Tag.GetCaptureGroup(2).ToLower();
        if(Name.Len()==2&&Name[0]=='h'&&Name[1]>='1'&&Name[1]<='6'){Flush();Heading=!Closing;}
        else if(Name==TEXT("p")||Name==TEXT("div")||Name==TEXT("tr")||Name==TEXT("table")||Name==TEXT("ul")||Name==TEXT("ol")||Name==TEXT("dl"))Flush();
        else if(Name==TEXT("li")){Flush();if(!Closing)Text=TEXT("• ");}
        else if(Name==TEXT("br"))Flush();
        else if(!Closing&&(Name==TEXT("td")||Name==TEXT("th"))&&!Text.IsEmpty())Text+=TEXT("  ·  ");
        if(Name==TEXT("a")&&!Closing&&Links.Num()<64)
        {
            FRegexMatcher Href(FRegexPattern(TEXT("(?i)href\\s*=\\s*[\"']([^\"']+)[\"']")),Token);
            if(!Href.FindNext())continue;
            FString Url=Decode(Href.GetCaptureGroup(1)),Title;
            if(Url.StartsWith(TEXT("/wiki/")))Title=FGenericPlatformHttp::UrlDecode(Url.Mid(6));
            if(Title.IsEmpty())continue;
            int32 Fragment;if(Title.FindChar('#',Fragment))Title.LeftInline(Fragment);
            Title.ReplaceInline(TEXT("_"),TEXT(" "));
            if(Title.IsEmpty()||Title.Len()>256||Title.Contains(TEXT(":")))continue;
            Links.AddUnique(Title);
        }
    }
    Flush();
}

bool ParseArticle(const TSharedPtr<FJsonObject>& Root,ESource S,FArticle& Out)
{
    const TSharedPtr<FJsonObject>* Parse=nullptr;const TSharedPtr<FJsonObject>* Text=nullptr;FString Html;
    if(!Root||Root->HasField(TEXT("error"))||!Root->TryGetObjectField(TEXT("parse"),Parse)
        ||!(*Parse)->TryGetObjectField(TEXT("text"),Text)||!(*Text)->TryGetStringField(TEXT("*"),Html))return false;
    FArticle Parsed;double Revision=0;
    if(!(*Parse)->TryGetStringField(TEXT("title"),Parsed.Title)||Parsed.Title.IsEmpty()||Parsed.Title.Len()>256
        ||!(*Parse)->TryGetNumberField(TEXT("revid"),Revision)||Revision<1||Revision>9007199254740991.)return false;
    Parsed.Revision=int64(Revision);Parsed.Url=ArticleUrl(S,Parsed.Title);
    FormatHtml(Html,Parsed.Blocks,Parsed.Links,Parsed.Truncated);
    if(Parsed.Blocks.IsEmpty())return false;
    Out=MoveTemp(Parsed);return true;
}
}
