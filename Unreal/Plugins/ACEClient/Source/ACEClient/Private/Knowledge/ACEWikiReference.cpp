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

static FString SearchText(const FString& Query)
{return Query.TrimStartAndEnd().Replace(TEXT("_"),TEXT(" ")).Replace(TEXT("*"),TEXT("")).Left(128).TrimStartAndEnd();}
FString SuggestUrl(ESource Source,const FString& Query)
{const FString Clean=SearchText(Query);return Clean.IsEmpty()?FString():Endpoint(Source)+TEXT("?action=query&list=search&srwhat=title&srlimit=8&srprop=&format=json&srsearch=")
    +FGenericPlatformHttp::UrlEncode(Clean+TEXT("*"));}
bool ParseSuggestions(const TSharedPtr<FJsonObject>& Root,TArray<FString>& Out)
{
    Out.Reset();const TSharedPtr<FJsonObject>* Q=nullptr;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Root||Root->HasField(TEXT("error"))||!Root->TryGetObjectField(TEXT("query"),Q)||!(*Q)->TryGetArrayField(TEXT("search"),Rows)||Rows->Num()>20)return false;
    TArray<FString> Parsed;
    for(const auto& Row:*Rows)
    {
        const TSharedPtr<FJsonObject>* Item=nullptr;FString Title;double Ns=-1;
        if(!Row->TryGetObject(Item)||!(*Item)->TryGetStringField(TEXT("title"),Title)||Title.IsEmpty()||Title.Len()>256
            ||!(*Item)->TryGetNumberField(TEXT("ns"),Ns))return false;
        if(Ns==0)Parsed.AddUnique(Title);
    }
    Out=MoveTemp(Parsed);
    return true;
}
FString ChooseSearchTitle(const FString& Query,const TArray<FString>& Titles)
{
    if(Query.Contains(TEXT("*")))return FString(); // A wildcard requests choices, not an arbitrary first article.
    const FString Clean=SearchText(Query);
    for(const FString& Title:Titles)if(Title.Equals(Clean,ESearchCase::IgnoreCase))return Title;
    return Titles.Num()==1?Titles[0]:FString();
}

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
    FString Text;bool Heading=false,LastSpace=false;int32 SkipDepth=0,Images=0;
    struct FRow {TArray<FString> Cells;FString Cell,Hint;int32 Start=0;bool InCell=false,Nested=false;};
    TArray<FRow> Rows;
    auto Flush=[&](){Text.TrimStartAndEndInline();if(!Text.IsEmpty())Blocks.Add({Text,Heading});Text.Empty();LastSpace=false;};
    while(Tokens.FindNext())
    {
        if(Blocks.Num()>=1024){Truncated=true;break;}
        const FString Token=Tokens.GetCaptureGroup(0);
        if(!Token.StartsWith(TEXT("<")))
        {
            if(SkipDepth)continue;
            if(Rows.Num()&&Rows.Last().InCell)Rows.Last().Cell+=Decode(Token);
            for(TCHAR C:Decode(Token))
            {if(FChar::IsWhitespace(C)){if(!LastSpace&&!Text.IsEmpty())Text.AppendChar(' ');LastSpace=true;}else{Text.AppendChar(C);LastSpace=false;}}
            continue;
        }
        FRegexMatcher Tag(FRegexPattern(TEXT("^<\\s*(/?)\\s*([a-zA-Z0-9]+)")),Token);if(!Tag.FindNext())continue;
        const bool Closing=!Tag.GetCaptureGroup(1).IsEmpty();const FString Name=Tag.GetCaptureGroup(2).ToLower();
        const bool Void=Name==TEXT("img")||Name==TEXT("br")||Name==TEXT("hr")||Name==TEXT("input")||Name==TEXT("meta")||Name==TEXT("link")||Name==TEXT("wbr")||Name==TEXT("source")||Token.EndsWith(TEXT("/>"));
        if(SkipDepth){if(Closing)--SkipDepth;else if(!Void)++SkipDepth;continue;}
        // Website controls/navigation are not part of the game reference article.
        FRegexMatcher Chrome(FRegexPattern(TEXT("(?i)(?:class|id)\\s*=\\s*[\"'][^\"']*\\b(?:mw-editsection|mw-jump-link|mw-empty-elt|navbox|noprint|catlinks|printfooter|toclimit|tocnumber|toc)\\b[^\"']*[\"']")),Token);
        if(!Closing&&!Void&&Chrome.FindNext()){SkipDepth=1;continue;}
        if(Name==TEXT("tr"))
        {
            Flush();
            if(!Closing)
            {
                if(Rows.Num())Rows.Last().Nested=true;
                FRow Row;Row.Start=Blocks.Num();Rows.Add(MoveTemp(Row));
            }
            else if(Rows.Num())
            {
                auto Row=Rows.Pop();
                if(!Row.Nested&&Row.Cells.Num())
                {
                    if(Blocks.Num()==Row.Start)Blocks.AddDefaulted();
                    Blocks.Last().Cells=MoveTemp(Row.Cells);Blocks.Last().RowBlocks=Blocks.Num()-Row.Start;
                }
            }
            continue;
        }
        if(Rows.Num())
        {
            auto& Row=Rows.Last();
            if(Name==TEXT("td")||Name==TEXT("th"))
            {
                if(!Closing){Row.Cell.Empty();Row.Hint.Empty();Row.InCell=true;}
                else if(Row.InCell)
                {
                    Row.Cell.TrimStartAndEndInline();
                    FRegexMatcher Spaces(FRegexPattern(TEXT("\\s+")),Row.Cell);FString Normal;int32 At=0;
                    while(Spaces.FindNext()){Normal+=Row.Cell.Mid(At,Spaces.GetMatchBeginning()-At)+TEXT(" ");At=Spaces.GetMatchEnding();}
                    Normal+=Row.Cell.Mid(At);Normal.TrimStartAndEndInline();
                    Row.Cells.Add(Normal.IsEmpty()?Row.Hint:Normal);Row.InCell=false;
                }
            }
            else if(Row.InCell&&(Name==TEXT("br")||Name==TEXT("p")||Name==TEXT("div")))Row.Cell+=TEXT(" ");
        }
        if(!Closing&&Name==TEXT("img"))
        {
            FRegexMatcher Src(FRegexPattern(TEXT("(?i)\\bsrc\\s*=\\s*[\"']([^\"']+)[\"']")),Token);
            FString Url;
            if(Src.FindNext())Url=Decode(Src.GetCaptureGroup(1));
            if(Url.StartsWith(TEXT("/images/")))Url=TEXT("https://acpedia.celestro.com")+Url;
            FString ImageName=FGenericPlatformHttp::UrlDecode(Url).ToLower().Replace(TEXT("_"),TEXT("")).Replace(TEXT(" "),TEXT(""));
            if(ImageName.Contains(TEXT("imageneeded"))||ImageName.Contains(TEXT("imagenotavailable")))continue;
            // ACPedia's generic missing creature portrait is named Live.jpg.
            FString Filename=ImageName.Mid(ImageName.Find(TEXT("/"),ESearchCase::CaseSensitive,ESearchDir::FromEnd)+1);
            FRegexMatcher Thumbnail(FRegexPattern(TEXT("^[0-9]+px-(.*)$")),Filename);
            if(Thumbnail.FindNext())Filename=Thumbnail.GetCaptureGroup(1);
            if(Filename==TEXT("live.jpg")||ImageName.Contains(TEXT("iconhorizontalspacer")))continue;
            if(Url.Contains(TEXT("No_Spawn_Map"))||Url.Contains(TEXT("Spawn_Map_Base")))continue;
            if(Images<8&&Url.StartsWith(TEXT("https://acpedia.celestro.com/images/"))&&!Url.Contains(TEXT("?")))
            {
                Flush();FBlock Image;Image.ImageUrl=Url;
                FRegexMatcher Width(FRegexPattern(TEXT("(?i)\\bwidth=[\"']([0-9]+)[\"']")),Token);
                FRegexMatcher Height(FRegexPattern(TEXT("(?i)\\bheight=[\"']([0-9]+)[\"']")),Token);
                Image.Portrait=Images==0&&Width.FindNext()&&Height.FindNext()
                    &&FCString::Atoi(*Width.GetCaptureGroup(1))>=96&&FCString::Atoi(*Height.GetCaptureGroup(1))>=96;
                Blocks.Add(MoveTemp(Image));++Images;
            }
            continue;
        }

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
            if(Rows.Num()&&Rows.Last().InCell&&Rows.Last().Hint.IsEmpty())Rows.Last().Hint=Title;
        }
    }
    Flush();
    Blocks.RemoveAll([](const FBlock& B){return B.ImageUrl.IsEmpty()&&(B.Text==TEXT("Contents")||B.Text.StartsWith(TEXT("File:"))||B.Text.StartsWith(TEXT("Data courtesy of Mob Tracker"))||B.Text.StartsWith(TEXT("Introduced:"))||B.Text.StartsWith(TEXT("Introduced In:"))||B.Text.StartsWith(TEXT("Updated In:")));});
    // Preserve semantics instead of displaying infobox cells as loose paragraphs.
    const FRegexPattern StatPattern(TEXT("^(Strength|Endurance|Coordination|Quickness|Focus|Self|Health|Stamina|Mana)\\s*[:·]?\\s*([0-9][0-9,]*(?:\\s*/\\s*[0-9,]+)?|[?]{2,3})\\s*[·.]?\\s*$"));
    const FRegexPattern NamePattern(TEXT("^(Strength|Endurance|Coordination|Quickness|Focus|Self|Health|Stamina|Mana)[:·]?\\s*$"));
    const FRegexPattern ValuePattern(TEXT("^([0-9][0-9,]*(?:\\s*/\\s*[0-9,]+)?|[?]{2,3})\\s*[·.]?\\s*$"));
    auto ReadStat=[&](int32 At,FAttribute& Out)->int32
    {
        if(!Blocks.IsValidIndex(At)||Blocks[At].Heading||!Blocks[At].ImageUrl.IsEmpty())return 0;
        FRegexMatcher Full(StatPattern,Blocks[At].Text);
        if(Full.FindNext()){Out={Full.GetCaptureGroup(1),Full.GetCaptureGroup(2)};return 1;}
        FRegexMatcher Name(NamePattern,Blocks[At].Text);
        if(Name.FindNext()&&Blocks.IsValidIndex(At+1)&&!Blocks[At+1].Heading&&Blocks[At+1].ImageUrl.IsEmpty())
        {FRegexMatcher Value(ValuePattern,Blocks[At+1].Text);if(Value.FindNext()){Out={Name.GetCaptureGroup(1),Value.GetCaptureGroup(1)};return 2;}}
        return 0;
    };
    TArray<FBlock> Structured;
    for(int32 I=0;I<Blocks.Num();)
    {
        FBlock Stats;Stats.Text=TEXT("Attributes");int32 End=I;
        TSet<FString> Names;
        while(End<Blocks.Num())
        {
            FAttribute A;const int32 Used=ReadStat(End,A);
            if(!Used||Names.Contains(A.Name))break;
            Names.Add(A.Name);Stats.Attributes.Add(MoveTemp(A));End+=Used;
        }
        if(Stats.Attributes.Num()>=3){Structured.Add(MoveTemp(Stats));I=End;}
        else Structured.Add(MoveTemp(Blocks[I++]));
    }
    Blocks=MoveTemp(Structured);
    if(Blocks.ContainsByPredicate([](const auto& B){return !B.Attributes.IsEmpty();}))
    {
        // Only creature infoboxes are restructured. Preserve blank cells so a
        // missing missile defense cannot shift the magic-defense value left.
        TArray<FBlock> Result;
        for(int32 I=0;I<Blocks.Num();++I)
        {
            const auto& B=Blocks[I];FBlock Semantic;
            TArray<FString> NonEmpty;for(const auto& Cell:B.Cells)if(!Cell.IsEmpty())NonEmpty.Add(Cell);
            int32 Consumed=0;
            for(const auto& Cell:NonEmpty)
            {
                FRegexMatcher Level(FRegexPattern(TEXT("^Level\\s+([0-9]+|[?]+)$")),Cell);
                if(Level.FindNext())
                {
                    Semantic.Layout=ELayout::Identity;Semantic.Fields.Add({TEXT("Level"),Level.GetCaptureGroup(1)});
                    for(const auto& Species:NonEmpty)if(Species!=Cell)Semantic.Fields.Add({TEXT("Type"),Species});
                    break;
                }
            }
            if(Semantic.Layout==ELayout::Text&&NonEmpty.Num()==2
                &&(NonEmpty[0]==TEXT("XP")||NonEmpty[0]==TEXT("Luminance")||NonEmpty[0]==TEXT("Loot Tier")))
            {Semantic.Layout=ELayout::Rewards;Semantic.Fields.Add({NonEmpty[0],NonEmpty[1]});}
            if(I+1<Blocks.Num()&&Semantic.Layout==ELayout::Text)
            {
                const auto& Next=Blocks[I+1];
                if(NonEmpty.Num()==1&&NonEmpty[0]==TEXT("Trophies")&&Next.Cells.Num())
                {Semantic.Layout=ELayout::Trophies;Semantic.Fields.Add({TEXT("Trophies"),FString::Join(Next.Cells,TEXT(", "))});Consumed=1;}
                else if(B.Cells.Num()==2&&B.Cells[0]==TEXT("Common Attacks")&&B.Cells[1].StartsWith(TEXT("Weakness"))&&Next.Cells.Num()==2)
                {Semantic.Layout=ELayout::Combat;Semantic.Fields={{TEXT("Common attacks"),Next.Cells[0]},{TEXT("Vulnerabilities"),Next.Cells[1]}};Consumed=1;}
                else if(B.Cells.Num()==6&&B.Cells[0]==TEXT("Melee A")&&Next.Cells.Num()==6)
                {
                    Semantic.Layout=ELayout::Ratings;
                    const TCHAR* Labels[]={TEXT("Melee attack"),TEXT("Missile attack"),TEXT("Magic attack"),TEXT("Melee defense"),TEXT("Missile defense"),TEXT("Magic defense")};
                    for(int32 C=0;C<6;++C)Semantic.Fields.Add({Labels[C],Next.Cells[C]});
                    Consumed=1;
                }
            }
            if(Semantic.Layout!=ELayout::Text)
            {
                // Previous fragments in a split table row belong to this row.
                for(int32 R=1;R<B.RowBlocks&&Result.Num();++R)Result.Pop();
                for(auto& Field:Semantic.Fields)
                {
                    Field.Value.TrimStartAndEndInline();
                    if(Field.Value.IsEmpty()||Field.Value==TEXT("??")||Field.Value==TEXT("--"))Field.Value=TEXT("Unknown");
                }
                if(Semantic.Layout==ELayout::Rewards&&Result.Num()&&Result.Last().Layout==ELayout::Rewards)
                    Result.Last().Fields.Append(Semantic.Fields);
                else Result.Add(MoveTemp(Semantic));
                I+=Consumed;
            }
            else if(!B.Text.IsEmpty()||!B.ImageUrl.IsEmpty()||!B.Attributes.IsEmpty())Result.Add(B);
        }
        Blocks=MoveTemp(Result);
    }
}

bool MakeExaminePortrait(const TArray<FLinearColor>& Pixels,int32 Width,int32 Height,TArray<uint8>& BGRA,FIntPoint& Size)
{
    BGRA.Reset();Size=FIntPoint::ZeroValue;
    if(Width<=0||Height<=0||int64(Width)*Height>4*1024*1024||Pixels.Num()!=int64(Width)*Height)return false;
    int32 Left=Width,Top=Height,Right=-1,Bottom=-1;
    for(int32 Y=0;Y<Height;++Y)for(int32 X=0;X<Width;++X)
        if(1.f-Pixels[Y*Width+X].A>.01f)
        {Left=FMath::Min(Left,X);Right=FMath::Max(Right,X);Top=FMath::Min(Top,Y);Bottom=FMath::Max(Bottom,Y);}
    if(Right<Left)return false;
    Left=FMath::Max(0,Left-4);Top=FMath::Max(0,Top-4);
    Right=FMath::Min(Width-1,Right+4);Bottom=FMath::Min(Height-1,Bottom+4);
    const int32 CropWidth=Right-Left+1,CropHeight=Bottom-Top+1;
    const float Scale=FMath::Min(1.f,512.f/FMath::Max(CropWidth,CropHeight));
    Size={FMath::Max(1,FMath::RoundToInt(CropWidth*Scale)),FMath::Max(1,FMath::RoundToInt(CropHeight*Scale))};
    BGRA.SetNumZeroed(Size.X*Size.Y*4);
    for(int32 Y=0;Y<Size.Y;++Y)for(int32 X=0;X<Size.X;++X)
    {
        const auto& Source=Pixels[(Top+FMath::Min(CropHeight-1,int32(Y/Scale)))*Width+Left+FMath::Min(CropWidth-1,int32(X/Scale))];
        const float Alpha=FMath::Clamp(1.f-Source.A,0.f,1.f);
        if(Alpha<=.01f)continue;
        const FColor Color=FLinearColor(Source.R/Alpha,Source.G/Alpha,Source.B/Alpha,Alpha).ToFColor(true);
        const int32 Index=(Y*Size.X+X)*4;
        BGRA[Index]=Color.B;BGRA[Index+1]=Color.G;BGRA[Index+2]=Color.R;BGRA[Index+3]=Color.A;
    }
    return true;
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
    if(Parsed.Blocks.ContainsByPredicate([](const auto& B){return !B.Attributes.IsEmpty();}))
        Parsed.Blocks.RemoveAll([&](const auto& B){return B.Layout==ELayout::Text&&!B.Heading&&B.Text==Parsed.Title;});
    if(Parsed.Blocks.IsEmpty())return false;
    Out=MoveTemp(Parsed);return true;
}
}
