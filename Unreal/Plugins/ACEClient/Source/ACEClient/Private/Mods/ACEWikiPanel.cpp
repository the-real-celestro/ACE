#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginStyle.h"
#include "Knowledge/ACEWikiReference.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

// Per game instance: no game commands, credentials, or character state are sent.
struct FACEWikiState : TSharedFromThis<FACEWikiState>
{
    FString Title, Message=TEXT("Open an entry with Wiki → in Examine, or enter an article title above.");
    ACEWikiReference::FArticle Article;
    TArray<FString> History;
    int32 Cursor=-1;
    uint32 Generation=0, Revision=0;
    struct FMatch { FString Title; double RetryAt=0; bool Pending=false; };
    TMap<FString,FMatch> Matches;
    int32 Lookups=0;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> ArticleRequest;

    static void Get(const FString& Url,TFunction<void(TSharedPtr<FJsonObject>)> Done,
        TSharedPtr<IHttpRequest,ESPMode::ThreadSafe>* Request=nullptr)
    {
        auto R=FHttpModule::Get().CreateRequest();
        R->SetURL(Url); R->SetVerb(TEXT("GET")); R->SetTimeout(20);
        R->SetHeader(TEXT("Accept"),TEXT("application/json"));
        R->SetHeader(TEXT("User-Agent"),TEXT("ACUnreal-Encyclopedia/0.1"));
        R->OnProcessRequestComplete().BindLambda([Done](FHttpRequestPtr,FHttpResponsePtr Response,bool Success)
        {
            TSharedPtr<FJsonObject> Json;
            if(Success&&Response&&Response->GetResponseCode()==200&&Response->GetContent().Num()<=4*1024*1024)
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json);
            Done(Json);
        });
        if(Request)*Request=R;
        if(!R->ProcessRequest())Done(nullptr);
    }
    bool Matched(const FString& Name) const
    {const auto* M=Matches.Find(Name);return M&&!M->Title.IsEmpty();}
    void Resolve(const FString& Name)
    {
        if(Name.IsEmpty()||Name.Len()>256||Lookups>=4)return;
        if(auto* M=Matches.Find(Name);M&&(M->Pending||!M->Title.IsEmpty()||M->RetryAt>FPlatformTime::Seconds()))return;
        if(Matches.Num()>=256)for(auto It=Matches.CreateIterator();It;++It)if(!It.Value().Pending)It.RemoveCurrent();
        auto& M=Matches.FindOrAdd(Name);M.Pending=true; ++Lookups;
        TWeakPtr<FACEWikiState> Weak=AsShared();
        Get(ACEWikiReference::Endpoint(ACEWikiReference::ESource::ACPedia)
            +TEXT("?action=query&redirects=1&format=json&titles=")+FGenericPlatformHttp::UrlEncode(Name),
            [Weak,Name](TSharedPtr<FJsonObject> Root)
        {
            auto S=Weak.Pin();if(!S)return;
            --S->Lookups;auto& Match=S->Matches.FindOrAdd(Name);Match.Pending=false;Match.RetryAt=FPlatformTime::Seconds()+60;
            const TSharedPtr<FJsonObject>* Q=nullptr;const TSharedPtr<FJsonObject>* Pages=nullptr;
            if(!Root||!Root->TryGetObjectField(TEXT("query"),Q)||!(*Q)->TryGetObjectField(TEXT("pages"),Pages))return;
            for(const auto& Pair:(*Pages)->Values)
            {
                const TSharedPtr<FJsonObject>* P=nullptr;double Ns=-1;
                if(Pair.Value->TryGetObject(P)&&!(*P)->HasField(TEXT("missing"))&&!(*P)->HasField(TEXT("invalid"))
                    &&(*P)->TryGetNumberField(TEXT("ns"),Ns)&&Ns==0)
                    (*P)->TryGetStringField(TEXT("title"),Match.Title);
            }
        });
    }
    void Load(const FString& Name,bool AddHistory=true)
    {
        FString Clean=Name.TrimStartAndEnd();if(Clean.IsEmpty()||Clean.Len()>256)return;
        const uint32 Ticket=++Generation;
        if(ArticleRequest)ArticleRequest->CancelRequest();
        Title=Clean;Article={};Message=TEXT("Loading…");++Revision;
        if(AddHistory&&(Cursor<0||History[Cursor]!=Clean))
        {
            History.SetNum(Cursor+1);History.Add(Clean);
            if(History.Num()>64)History.RemoveAt(0);
            Cursor=History.Num()-1;
        }
        TWeakPtr<FACEWikiState> Weak=AsShared();
        ACEWikiReference::FEntry E;E.Title=Clean;
        Get(ACEWikiReference::ParseUrl(E),[Weak,Ticket](TSharedPtr<FJsonObject> Root)
        {
            auto S=Weak.Pin();if(!S||Ticket!=S->Generation)return;
            if(ACEWikiReference::ParseArticle(Root,ACEWikiReference::ESource::ACPedia,S->Article))
            {S->Title=S->Article.Title;S->Message.Empty();}
            else S->Message=TEXT("Could not load this article. It may be missing or ACPedia may be unavailable. Retry below.");
            ++S->Revision;
        },&ArticleRequest);
    }
};
namespace
{
TSharedRef<SWidget> Text(const FString& Value,int32 Size=14)
{return SNew(SACEPluginText).Text(FText::FromString(Value)).Font(FACEPluginStyle::NativeFont("Regular",Size)).AutoWrapText(true);}
TSharedRef<SWidget> Button(const FString& Value,TFunction<void()> Action,TAttribute<bool> Enabled=true)
{return SNew(SACEPluginButton).IsFocusable(false).IsEnabled(Enabled).ButtonStyle(&FACEPluginStyle::Current()->Button)
    .ContentPadding(FMargin(5,3)).OnClicked_Lambda([Action]{Action();return FReply::Handled();})[Text(Value)];}
class SWikiPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWikiPanel){} SLATE_ARGUMENT(TSharedPtr<FACEWikiState>,State) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        State=Args._State;
        ChildSlot[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("‹ Back"),[S=State]{S->Load(S->History[--S->Cursor],false);},TAttribute<bool>::CreateLambda([S=State]{return S->Cursor>0;}))]
                +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Forward ›"),[S=State]{S->Load(S->History[++S->Cursor],false);},TAttribute<bool>::CreateLambda([S=State]{return S->Cursor+1<S->History.Num();}))]
                +SHorizontalBox::Slot().FillWidth(1).Padding(5,0)[SAssignNew(TitleBox,SEditableTextBox).HintText(FText::FromString(TEXT("Article title")))
                    .OnTextCommitted_Lambda([S=State](const FText& T,ETextCommit::Type Why){if(Why==ETextCommit::OnEnter)S->Load(T.ToString());})]
                +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Open"),[this]{State->Load(TitleBox->GetText().ToString());})]]
            +SVerticalBox::Slot().FillHeight(1).Padding(8)[SAssignNew(Scroll,SScrollBox)
                +SScrollBox::Slot()[SAssignNew(Body,SVerticalBox)]]];
        Rebuild();
    }
    void Tick(const FGeometry& G,double T,float D) override
    {SCompoundWidget::Tick(G,T,D);if(Seen!=State->Revision)Rebuild();}
private:
    TSharedPtr<FACEWikiState> State;
    TSharedPtr<SEditableTextBox> TitleBox;
    TSharedPtr<SVerticalBox> Body;
    TSharedPtr<SScrollBox> Scroll;
    uint32 Seen=MAX_uint32;
    void Rebuild()
    {
        Seen=State->Revision;Body->ClearChildren();Scroll->SetScrollOffset(0);
        if(!State->Title.IsEmpty())Body->AddSlot().AutoHeight().Padding(0,0,0,8)[Text(State->Title,18)];
        if(!State->Message.IsEmpty())
        {
            Body->AddSlot().AutoHeight()[Text(State->Message)];
            if(!State->Title.IsEmpty())Body->AddSlot().AutoHeight()[Button(TEXT("Retry"),[S=State]{S->Load(S->Title,false);})];
            return;
        }
        for(const auto& B:State->Article.Blocks)
            Body->AddSlot().AutoHeight().Padding(0,B.Heading?10:2,0,4)[Text(B.Text,B.Heading?16:14)];
        if(State->Article.Truncated)Body->AddSlot().AutoHeight()[Text(TEXT("Article shortened for this preview."))];
        if(State->Article.Links.Num())Body->AddSlot().AutoHeight().Padding(0,12,0,4)[Text(TEXT("Related entries"),16)];
        for(const auto& Link:State->Article.Links)
            Body->AddSlot().AutoHeight().Padding(0,1)[Button(Link+TEXT(" →"),[S=State,Link]{S->Load(Link);})];
        Body->AddSlot().AutoHeight().Padding(0,12,0,4)[Text(FString::Printf(TEXT("ACPedia · revision %lld\n%s\nNative text preview; images and complex table layout are not yet supported."),State->Article.Revision,*State->Article.Url),11)];
    }
};
class SWikiLink : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWikiLink){} SLATE_ARGUMENT(UACEPluginSubsystem*,Host) SLATE_ARGUMENT(FString,Name) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Host=Args._Host;Name=Args._Name;State=Host->WikiState;
        ChildSlot[SNew(SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
            .IsFocusable(false).ContentPadding(0).ToolTipText(FText::FromString(TEXT("Read this entry in the encyclopedia")))
            .OnClicked_Lambda([this]{if(Host.IsValid())if(const auto* M=State->Matches.Find(Name))Host->OpenWiki(M->Title);return FReply::Handled();})
            [SNew(STextBlock).Text(FText::FromString(TEXT("Wiki →"))).Font(FCoreStyle::GetDefaultFontStyle("Regular",10)).ColorAndOpacity(FACEPluginStyle::Gold())]];
        State->Resolve(Name);SetVisibility(EVisibility::Collapsed);
    }
    void Tick(const FGeometry& G,double T,float D) override
    {SCompoundWidget::Tick(G,T,D);State->Resolve(Name);}
    EVisibility GetMatchVisibility() const{return State->Matched(Name)?EVisibility::Visible:EVisibility::Hidden;}
private:
    TWeakObjectPtr<UACEPluginSubsystem> Host;
    TSharedPtr<FACEWikiState> State;
    FString Name;
};
}
TSharedRef<SWidget> UACEPluginSubsystem::MakeWikiPanel()
{
    if(!WikiState)WikiState=MakeShared<FACEWikiState>();
    return SNew(SWikiPanel).State(WikiState);
}
TSharedRef<SWidget> UACEPluginSubsystem::MakeWikiLink(const FString& ObjectName)
{
    if(!WikiState)WikiState=MakeShared<FACEWikiState>();
    auto Link=SNew(SWikiLink).Host(this).Name(ObjectName);
    Link->SetVisibility(TAttribute<EVisibility>::CreateLambda([Weak=TWeakPtr<SWikiLink>(Link),S=WikiState,ObjectName]{S->Resolve(ObjectName);auto P=Weak.Pin();return P?P->GetMatchVisibility():EVisibility::Collapsed;}));
    return Link;
}
void UACEPluginSubsystem::OpenWiki(const FString& Title)
{
    if(!WikiState)WikiState=MakeShared<FACEWikiState>();
    WikiState->Load(Title);
    if(!IsPluginWindowOpen(TEXT("wiki")))TogglePluginWindow(TEXT("wiki"));
}
