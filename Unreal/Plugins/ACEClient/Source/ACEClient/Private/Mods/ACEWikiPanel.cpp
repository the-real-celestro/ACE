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
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSuggestionTextBox.h"
#include "InputCoreTypes.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "UI/ACEAppraisalKnowledge.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Modules/ModuleManager.h"

// Per game instance: no game commands, credentials, or character state are sent.
struct FACEWikiState : TSharedFromThis<FACEWikiState>
{
    FString Title, Message=TEXT("Open an entry with ACPedia → in Examine, or enter an article title above.");
    ACEWikiReference::FArticle Article;
    TArray<FString> History;
    TSet<FString> IdentifiedTitles;
    TMap<FString,bool> AttributesExpanded;
    int32 Cursor=-1,FontSize=20;
    TFunction<void(int32)> SaveFont;
    bool ReplaceExamine=false;
    TFunction<void(bool)> SaveReplacement;
    TMap<FString,TArray<ACEAppraisalFormatting::FCreatureDetailLine>> DetailsByName,CreatureDetails;
    TMap<FString,TArray<TFunction<void(const FString&)>>> ResolveCallbacks;
    struct FImage {TSharedPtr<FSlateDynamicImageBrush> Brush;bool Complete=false;};
    TMap<FString,TSharedPtr<FImage>> Images;
    TMap<FString,TSharedPtr<FImage>> ExaminePortraits;
    void RememberPortrait(const FString& ArticleTitle,UTextureRenderTarget2D* Target)
    {
        if(!Target||int64(Target->SizeX)*Target->SizeY>4*1024*1024)return;
        auto* Resource=Target->GameThread_GetRenderTargetResource();if(!Resource)return;
        TArray<FLinearColor> Pixels;
        if(!Resource->ReadLinearColorPixels(Pixels,FReadSurfaceDataFlags(RCM_MinMax)))return;
        TArray<uint8> BGRA;FIntPoint Size;
        if(!ACEWikiReference::MakeExaminePortrait(Pixels,Target->SizeX,Target->SizeY,BGRA,Size))return;
        auto Image=MakeShared<FImage>();Image->Complete=true;
        Image->Brush=FSlateDynamicImageBrush::CreateWithImageData(
            FName(*FString::Printf(TEXT("WikiPortrait_%s"),*FGuid::NewGuid().ToString())),FVector2D(Size.X,Size.Y),BGRA);
        if(!Image->Brush)return;
        if(ExaminePortraits.Num()>=16&&!ExaminePortraits.Contains(ArticleTitle))
            ExaminePortraits.Remove(ExaminePortraits.CreateConstIterator().Key());
        ExaminePortraits.Add(ArticleTitle,Image);++Revision;
    }
    void SetFont(int32 Size)
    {FontSize=FMath::Clamp(Size,8,32);if(SaveFont)SaveFont(FontSize);++Revision;}
    void FetchImages()
    {
        const uint32 Ticket=Generation;
        for(const auto& B:Article.Blocks)
        {
            if(B.ImageUrl.IsEmpty()||Images.Contains(B.ImageUrl)||Images.Num()>=8)continue;
            auto Image=MakeShared<FImage>();Images.Add(B.ImageUrl,Image);
            auto Request=FHttpModule::Get().CreateRequest();Request->SetURL(B.ImageUrl);Request->SetVerb(TEXT("GET"));Request->SetTimeout(20);
            Request->SetHeader(TEXT("User-Agent"),TEXT("ACUnreal-Encyclopedia/0.1"));
            Request->SetHeader(TEXT("Referer"),TEXT("https://acpedia.celestro.com/"));
            TWeakPtr<FACEWikiState> Weak=AsShared();
            Request->OnProcessRequestComplete().BindLambda([Weak,Ticket,Image](FHttpRequestPtr,FHttpResponsePtr Response,bool Success)
            {
                auto S=Weak.Pin();if(!S||Ticket!=S->Generation)return;
                Image->Complete=true;
                if(Success&&Response&&Response->GetResponseCode()==200&&Response->GetContent().Num()<=8*1024*1024)
                {
                    const auto& Data=Response->GetContent();auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
                    const auto Format=Module.DetectImageFormat(Data.GetData(),Data.Num());
                    if(Format==EImageFormat::PNG||Format==EImageFormat::JPEG)
                    {
                        auto Decoder=Module.CreateImageWrapper(Format);TArray<uint8> Raw;
                        if(Decoder&&Decoder->SetCompressed(Data.GetData(),Data.Num())&&Decoder->GetWidth()>0&&Decoder->GetHeight()>0
                            &&Decoder->GetWidth()<=4096&&Decoder->GetHeight()<=4096&&int64(Decoder->GetWidth())*Decoder->GetHeight()<=4*1024*1024
                            &&Decoder->GetRaw(ERGBFormat::BGRA,8,Raw))
                            Image->Brush=FSlateDynamicImageBrush::CreateWithImageData(FName(*FString::Printf(TEXT("WikiImage_%s"),*FGuid::NewGuid().ToString())),FVector2D(Decoder->GetWidth(),Decoder->GetHeight()),Raw);
                    }
                }
                ++S->Revision;
            });
            if(!Request->ProcessRequest())Image->Complete=true;
        }
    }
    uint32 Generation=0, Revision=0;
    struct FMatch { FString Title; double RetryAt=0; bool Pending=false; };
    TMap<FString,FMatch> Matches;
    int32 Lookups=0;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> ArticleRequest;
    FString SearchQuery,SearchMessage;
    TArray<FString> SearchTitles;
    double SearchAt=0;
    uint32 SearchGeneration=0,SearchRevision=0;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> SearchRequest;

    void ClearSearch()
    {
        ++SearchGeneration;SearchAt=0;SearchTitles.Reset();SearchMessage.Empty();++SearchRevision;
        if(SearchRequest)SearchRequest->CancelRequest();
    }
    void QueueSearch(const FString& Query)
    {
        ClearSearch();SearchQuery=Query.TrimStartAndEnd();
        if(SearchQuery.Len()>=2&&SearchQuery.Len()<=128&&!ACEWikiReference::SuggestUrl(ACEWikiReference::ESource::ACPedia,SearchQuery).IsEmpty())
            SearchAt=FPlatformTime::Seconds()+.3;
    }
    void Search(const FString& Query,bool Open)
    {
        ClearSearch();SearchQuery=Query.TrimStartAndEnd();
        if(SearchQuery.IsEmpty()||SearchQuery.Len()>128)return;
        const FString Url=ACEWikiReference::SuggestUrl(ACEWikiReference::ESource::ACPedia,SearchQuery);
        if(Url.IsEmpty())return;
        const uint32 Ticket=SearchGeneration;
        SearchMessage=TEXT("Finding matching titles…");++SearchRevision;
        TWeakPtr<FACEWikiState> Weak=AsShared();
        Get(Url,[Weak,Ticket,Open](TSharedPtr<FJsonObject> Root)
        {
            auto S=Weak.Pin();if(!S||Ticket!=S->SearchGeneration)return;
            if(!ACEWikiReference::ParseSuggestions(Root,S->SearchTitles))S->SearchMessage=TEXT("Search is unavailable. Try Open again.");
            else if(S->SearchTitles.IsEmpty())S->SearchMessage=TEXT("No matching titles. Try fewer words or a word prefix, such as aeth*.");
            else
            {
                S->SearchMessage=TEXT("Choose a matching title from the search box.");
                if(Open)
                {
                    const FString Title=ACEWikiReference::ChooseSearchTitle(S->SearchQuery,S->SearchTitles);
                    if(!Title.IsEmpty())S->Load(Title);
                }
            }
            ++S->SearchRevision;
        },&SearchRequest);
    }
    void TickSearch()
    {if(SearchAt>0&&FPlatformTime::Seconds()>=SearchAt)Search(SearchQuery,false);}

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
    void Resolve(const FString& Name,TFunction<void(const FString&)> Done={})
    {
        if(Name.IsEmpty()||Name.Len()>256){if(Done)Done(FString());return;}
        if(auto* M=Matches.Find(Name))
        {
            if(!M->Title.IsEmpty()){if(Done)Done(M->Title);return;}
            if(M->Pending){if(Done)ResolveCallbacks.FindOrAdd(Name).Add(MoveTemp(Done));return;}
            if(M->RetryAt>FPlatformTime::Seconds()){if(Done)Done(FString());return;}
        }
        if(Lookups>=4){if(Done)Done(FString());return;}
        if(Done)ResolveCallbacks.FindOrAdd(Name).Add(MoveTemp(Done));
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
            if(Root&&Root->TryGetObjectField(TEXT("query"),Q)&&(*Q)->TryGetObjectField(TEXT("pages"),Pages))
            for(const auto& Pair:(*Pages)->Values)
            {
                const TSharedPtr<FJsonObject>* P=nullptr;double Ns=-1;
                if(Pair.Value->TryGetObject(P)&&!(*P)->HasField(TEXT("missing"))&&!(*P)->HasField(TEXT("invalid"))
                    &&(*P)->TryGetNumberField(TEXT("ns"),Ns)&&Ns==0)
                    (*P)->TryGetStringField(TEXT("title"),Match.Title);
            }
            const FString Resolved=Match.Title;
            if(!Resolved.IsEmpty())if(auto Details=S->DetailsByName.Find(Name))
            {
                if(S->CreatureDetails.Num()>=32&&!S->CreatureDetails.Contains(Resolved))
                    S->CreatureDetails.Remove(S->CreatureDetails.CreateConstIterator().Key());
                S->CreatureDetails.Add(Resolved,*Details);
            }
            TArray<TFunction<void(const FString&)>> Callbacks;
            if(auto Found=S->ResolveCallbacks.Find(Name))Callbacks=MoveTemp(*Found);
            S->ResolveCallbacks.Remove(Name);
            for(auto& Callback:Callbacks)Callback(Resolved);
        });
    }
    void Load(const FString& Name,bool AddHistory=true)
    {
        FString Clean=Name.TrimStartAndEnd();if(Clean.IsEmpty()||Clean.Len()>256)return;
        ClearSearch();
        const uint32 Ticket=++Generation;
        if(ArticleRequest)ArticleRequest->CancelRequest();
        Title=Clean;Article={};Images.Reset();Message=TEXT("Loading…");++Revision;
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
            {S->Title=S->Article.Title;S->Message.Empty();S->FetchImages();}
            else S->Message=TEXT("Could not load this article. It may be missing or ACPedia may be unavailable. Retry below.");
            ++S->Revision;
        },&ArticleRequest);
    }
};
namespace
{
class SWikiSearchBox : public SSuggestionTextBox
{
public:
    using FArguments=SSuggestionTextBox::FArguments;
    FOnTextCommitted Commit;
    TFunction<void()> Cancel;
    void Construct(const FArguments& Args){Commit=Args._OnTextCommitted;SSuggestionTextBox::Construct(Args);}
    void Publish(TArray<FString> Titles)
    {if(Titles.IsEmpty())ClearSuggestions();else if(HasFocusedDescendants())SetSuggestions(Titles,false);}
    FReply OnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E) override
    {
        if(E.GetKey()==EKeys::Enter){ClearSuggestions();Commit.ExecuteIfBound(GetText(),ETextCommit::OnEnter);return FReply::Handled();}
        if(E.GetKey()==EKeys::Escape){ClearSuggestions();if(Cancel)Cancel();return FReply::Handled();}
        return SSuggestionTextBox::OnPreviewKeyDown(G,E);
    }
};
TSharedRef<SWidget> Text(const FString& Value,int32 Size=14)
{return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(FLinearColor(.92f,.9f,.83f)).AutoWrapText(true);}
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
                +SHorizontalBox::Slot().FillWidth(1).Padding(5,0)[SAssignNew(TitleBox,SWikiSearchBox).HintText(FText::FromString(TEXT("Search ACPedia — names or partial words")))
                    .ClearKeyboardFocusOnCommit(false).CommitOnSuggestionSelected(false).SuggestionListMaxHeight(220)
                    .TextStyle(&FACEPluginStyle::Current()->Edit)
                    .OnTextChanged_Lambda([S=State](const FText& T){if(!S->SearchTitles.Contains(T.ToString()))S->QueueSearch(T.ToString());})
                    .OnTextCommitted_Lambda([S=State](const FText& T,ETextCommit::Type Why){if(Why==ETextCommit::OnEnter)S->Search(T.ToString(),true);})]
                +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Open"),[this]{State->Search(TitleBox->GetText().ToString(),true);})]]
            +SVerticalBox::Slot().AutoHeight().Padding(8,0)
                [SNew(STextBlock).Text_Lambda([S=State]{return FText::FromString(S->SearchMessage);})
                    .Visibility_Lambda([S=State]{return S->SearchMessage.IsEmpty()?EVisibility::Collapsed:EVisibility::Visible;})
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).AutoWrapText(true)]
            +SVerticalBox::Slot().AutoHeight().Padding(8,2)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                    [SNew(SCheckBox).IsChecked_Lambda([S=State]{return S->ReplaceExamine?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                        .OnCheckStateChanged_Lambda([S=State](ECheckBoxState Value){S->ReplaceExamine=Value==ECheckBoxState::Checked;if(S->SaveReplacement)S->SaveReplacement(S->ReplaceExamine);})
                        .ToolTipText(FText::FromString(TEXT("Open matching ACPedia entries when you Examine. Unmatched objects and spells keep the original Examine window.")))
                        [Text(TEXT("Use for Examine"),13)]]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6,0)[Text(TEXT("Text"),12)]
                +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("−"),[S=State]{S->SetFont(S->FontSize-2);})]
                +SHorizontalBox::Slot().AutoWidth().Padding(6,0).VAlign(VAlign_Center)[SNew(STextBlock).Text_Lambda([S=State]{return FText::AsNumber(S->FontSize);})]
                +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("+"),[S=State]{S->SetFont(S->FontSize+2);})]]
            +SVerticalBox::Slot().FillHeight(1).Padding(8)[SAssignNew(Scroll,SScrollBox)
                +SScrollBox::Slot()[SAssignNew(Body,SVerticalBox)]]];
        TitleBox->Cancel=[S=State]{S->ClearSearch();};
        Rebuild();
    }
    void Tick(const FGeometry& G,double T,float D) override
    {
        SCompoundWidget::Tick(G,T,D);State->TickSearch();
        if(Seen!=State->Revision)Rebuild();
        if(SeenSearch!=State->SearchRevision)
        {
            SeenSearch=State->SearchRevision;
            if(TitleBox->GetText().ToString().TrimStartAndEnd()==State->SearchQuery)TitleBox->Publish(State->SearchTitles);
        }
    }
private:
    TSharedPtr<FACEWikiState> State;
    TSharedPtr<SWikiSearchBox> TitleBox;
    TSharedPtr<SVerticalBox> Body;
    TSharedPtr<SScrollBox> Scroll;
    uint32 Seen=MAX_uint32,SeenGeneration=MAX_uint32;
    uint32 SeenSearch=MAX_uint32;
    FTextBlockStyle LinkStyle;
    TSharedRef<SWidget> Caption(const FString& Value)
    {return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle("Bold",State->FontSize+1)).ColorAndOpacity(FACEPluginStyle::Gold()).AutoWrapText(true);}
    TSharedRef<SWidget> Fields(const TArray<ACEWikiReference::FAttribute>& Values)
    {
        auto Grid=SNew(SGridPanel).FillColumn(0,1.f);int32 Row=0;
        for(const auto& Field:Values)
        {
            Grid->AddSlot(0,Row).Padding(0,2,10,2)[Text(Field.Name,State->FontSize)];
            Grid->AddSlot(1,Row++).Padding(0,2).HAlign(HAlign_Right)
                [SNew(STextBlock).Text(FText::FromString(Field.Value)).Font(FCoreStyle::GetDefaultFontStyle("Bold",State->FontSize)).AutoWrapText(true)];
        }
        return Grid;
    }
    void StructuredBlock(const ACEWikiReference::FBlock& Block)
    {
        using namespace ACEWikiReference;
        if(Block.Layout==ELayout::Identity)
        {
            FString Type,Level;
            for(const auto& Field:Block.Fields)if(Field.Name==TEXT("Type"))Type=Field.Value;else if(Field.Name==TEXT("Level"))Level=Field.Value;
            Body->AddSlot().AutoHeight().Padding(0,0,0,2)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[Text(Type,State->FontSize)]
                +SHorizontalBox::Slot().AutoWidth().Padding(10,0)[Caption(TEXT("Level ")+Level)]];
        }
        else if(Block.Layout==ELayout::Rewards)
        {
            auto Wrap=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12,4));
            for(const auto& Field:Block.Fields)
                Wrap->AddSlot().FillEmptySpace(true)[SNew(SBox).MinDesiredWidth(FMath::Max(96.f,State->FontSize*7.f))
                    [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Caption(Field.Name)]
                        +SVerticalBox::Slot().AutoHeight()[Text(Field.Value,State->FontSize+2)]]];
            Body->AddSlot().AutoHeight().Padding(0,6)[Wrap];
        }
        else if(Block.Layout==ELayout::Trophies)
            Body->AddSlot().AutoHeight().Padding(0,4)[Fields(Block.Fields)];
        else if(Block.Layout==ELayout::Combat)
        {
            auto Wrap=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(16,8));
            for(const auto& Field:Block.Fields)
            {
                auto Card=SNew(SVerticalBox);Card->AddSlot().AutoHeight().Padding(0,0,0,3)[Caption(Field.Name)];
                TArray<FAttribute> Multipliers;
                if(Field.Name==TEXT("Vulnerabilities"))if(auto Details=State->CreatureDetails.Find(State->Article.Title))
                    for(const auto& Detail:*Details)if(Detail.TileGroup==2)
                        Multipliers.Add({Detail.Label,Detail.DisplayValue.Mid(Detail.Label.Len()).TrimStartAndEnd()});
                if(Multipliers.Num())
                {
                    Card->AddSlot().AutoHeight().Padding(0,0,0,3)[Text(TEXT("Damage taken · 100% = normal"),FMath::Max(12,State->FontSize-2))];
                    Card->AddSlot().AutoHeight()[Fields(Multipliers)];
                }
                else
                {
                    Card->AddSlot().AutoHeight()[Text(Field.Value,State->FontSize)];
                    if(Field.Name==TEXT("Vulnerabilities"))Card->AddSlot().AutoHeight()
                        [Text(TEXT("Wiki list · percentages unavailable"),FMath::Max(12,State->FontSize-2))];
                }
                Wrap->AddSlot().FillEmptySpace(true)[SNew(SBox).MinDesiredWidth(State->FontSize*11.f)[Card]];
            }
            Body->AddSlot().AutoHeight().Padding(0,6)[Wrap];
        }
        else if(Block.Layout==ELayout::Ratings&&Block.Fields.Num()==6)
        {
            Body->AddSlot().AutoHeight().Padding(0,6,0,3)[Caption(TEXT("Combat ratings"))];
            auto Grid=SNew(SGridPanel).FillColumn(0,1.f).FillColumn(1,1.f).FillColumn(2,1.f);
            Grid->AddSlot(1,0).HAlign(HAlign_Right).Padding(4,2)[Caption(TEXT("Attack"))];
            Grid->AddSlot(2,0).HAlign(HAlign_Right).Padding(4,2)[Caption(TEXT("Defense"))];
            const TCHAR* Types[]={TEXT("Melee"),TEXT("Missile"),TEXT("Magic")};
            for(int32 I=0;I<3;++I)
            {
                Grid->AddSlot(0,I+1).Padding(0,2)[Text(Types[I],State->FontSize)];
                Grid->AddSlot(1,I+1).HAlign(HAlign_Right).Padding(4,2)[Text(Block.Fields[I].Value,State->FontSize)];
                Grid->AddSlot(2,I+1).HAlign(HAlign_Right).Padding(4,2)[Text(Block.Fields[I+3].Value,State->FontSize)];
            }
            Body->AddSlot().AutoHeight()[Grid];
        }
    }
    void Rebuild()
    {
        const bool NewArticle=SeenGeneration!=State->Generation;
        const float Offset=Scroll->GetScrollOffset();
        SeenGeneration=State->Generation;Seen=State->Revision;Body->ClearChildren();Scroll->SetScrollOffset(NewArticle?0:Offset);
        if(!State->Title.IsEmpty())Body->AddSlot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).Text(FText::FromString(State->Title)).Font(FCoreStyle::GetDefaultFontStyle("Bold",State->FontSize+8)).ColorAndOpacity(FACEPluginStyle::Gold()).AutoWrapText(true)];
        if(!State->Message.IsEmpty())
        {
            Body->AddSlot().AutoHeight()[Text(State->Message)];
            if(!State->Title.IsEmpty())Body->AddSlot().AutoHeight()[Button(TEXT("Retry"),[S=State]{S->Load(S->Title,false);})];
            return;
        }
        const bool HasAttributes=State->Article.Blocks.ContainsByPredicate([](const auto& B){return !B.Attributes.IsEmpty();});
        TSharedPtr<FACEWikiState::FImage> Portrait;
        bool LocalPortrait=false;
        for(const auto& B:State->Article.Blocks)if(B.Portrait)
            if(auto Found=State->Images.Find(B.ImageUrl);Found&&(*Found)->Brush){Portrait=*Found;break;}
        if(!Portrait)if(auto Found=State->ExaminePortraits.Find(State->Article.Title))
        {Portrait=*Found;LocalPortrait=true;}
        bool PortraitPlaced=false;
        TSharedPtr<SWrapBox> ImageRow;
        for(const auto& B:State->Article.Blocks)
        {
            if(B.ImageUrl.IsEmpty())ImageRow.Reset();
            if(B.Layout!=ACEWikiReference::ELayout::Text){StructuredBlock(B);continue;}
            if(!B.Attributes.IsEmpty())
            {
                auto Grid=SNew(SGridPanel).Tag(TEXT("WikiAttributes")).FillColumn(1,1.f);int32 Row=0;
                for(const auto& A:B.Attributes)
                {
                    Grid->AddSlot(0,Row).Padding(10,3,12,3).VAlign(VAlign_Center)
                        [SNew(STextBlock).Tag(FName(*(TEXT("WikiAttributeName.")+A.Name))).Text(FText::FromString(A.Name))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular",State->FontSize)).ColorAndOpacity(FLinearColor(.92f,.9f,.83f))];
                    Grid->AddSlot(2,Row++).Padding(12,3,10,3).HAlign(HAlign_Right).VAlign(VAlign_Center)
                        [SNew(STextBlock).Tag(FName(*(TEXT("WikiAttributeValue.")+A.Name))).Text(FText::FromString(A.Value)).Font(FCoreStyle::GetDefaultFontStyle("Bold",State->FontSize))
                            .ColorAndOpacity(FLinearColor(.96f,.94f,.88f))];
                }
                if(Portrait&&!PortraitPlaced)
                {
                    PortraitPlaced=true;
                    Grid->AddSlot(1,0).RowSpan(B.Attributes.Num()).Padding(6,4).VAlign(VAlign_Center)
                        [SNew(SBox).WidthOverride(0).HeightOverride(B.Attributes.Num()*(State->FontSize+10.f))
                            [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                                [SNew(SImage).Tag(TEXT("WikiPortrait")).Image_Lambda([Portrait]{return Portrait->Brush.Get();})
                                    .ToolTipText(FText::FromString(LocalPortrait?TEXT("In-game Examine preview"):TEXT("ACPedia portrait")))]]];
                }
                const FString Title=State->Article.Title;
                const bool* Explicit=State->AttributesExpanded.Find(Title);
                const bool Expanded=Explicit?*Explicit:!State->IdentifiedTitles.Contains(Title);
                Body->AddSlot().AutoHeight().Padding(0,8,0,8)
                    [SNew(SExpandableArea).InitiallyCollapsed(!Expanded)
                        .BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
                        .BodyBorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
                        .OnAreaExpansionChanged_Lambda([S=State,Title](bool Open){S->AttributesExpanded.Add(Title,Open);})
                        .HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Attributes")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold",State->FontSize+2)).ColorAndOpacity(FACEPluginStyle::Gold())]
                        .BodyContent()[Grid]];
                continue;
            }
            if(!B.ImageUrl.IsEmpty())
            {
                if(HasAttributes&&B.Portrait)continue;
                const auto* Found=State->Images.Find(B.ImageUrl);if(!Found)continue;const auto Image=*Found;
                if(!ImageRow)
                {
                    ImageRow=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12,8));
                    Body->AddSlot().AutoHeight().Padding(0,6)[ImageRow.ToSharedRef()];
                }
                if(Image->Brush)
                {
                    const FVector2D Native=Image->Brush->ImageSize;
                    const float Available=FMath::Max(120.f,Scroll->GetCachedGeometry().GetLocalSize().X-24.f);
                    const float Width=FMath::Min(Native.X,Available),Height=Native.Y*Width/Native.X;
                    ImageRow->AddSlot()
                        [SNew(SBox).WidthOverride(Width).HeightOverride(Height)
                            [SNew(SImage).Tag(FName(*(TEXT("WikiArticleImage.")+B.ImageUrl))).Image_Lambda([Image]{return Image->Brush.Get();})]];
                }
                else ImageRow->AddSlot()[Text(Image->Complete?TEXT("Image unavailable"):TEXT("Loading image…"),14)];
                continue;
            }
            if(B.Heading)
                Body->AddSlot().AutoHeight().Padding(0,12,0,6)
                    [SNew(STextBlock).Text(FText::FromString(B.Text)).Font(FCoreStyle::GetDefaultFontStyle("Bold",State->FontSize+4)).ColorAndOpacity(FACEPluginStyle::Gold()).AutoWrapText(true)];
            else Body->AddSlot().AutoHeight().Padding(0,1,0,3)[Text(B.Text,State->FontSize)];
        }
        if(State->Article.Truncated)Body->AddSlot().AutoHeight()[Text(TEXT("Article shortened for this preview."))];
        if(State->Article.Links.Num())Body->AddSlot().AutoHeight().Padding(0,12,0,4)[Text(TEXT("Related entries"),State->FontSize+4)];
        LinkStyle=FTextBlockStyle()
            .SetFont(FCoreStyle::GetDefaultFontStyle("Regular",State->FontSize)).SetColorAndOpacity(FACEPluginStyle::Gold());
        for(const auto& Link:State->Article.Links)
            Body->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0,2)
                [SNew(SHyperlink).TextStyle(&LinkStyle).Text(FText::FromString(Link))
                    .OnNavigate_Lambda([S=State,Link]{S->Load(Link);})];
        Body->AddSlot().AutoHeight().Padding(0,8,0,2)
            [SNew(SACEPluginText).Text(FText::FromString(FString::Printf(TEXT("ACPedia · revision %lld"),State->Article.Revision)))
                .Font(FACEPluginStyle::NativeFont("Regular",12)).ToolTipText(FText::FromString(State->Article.Url))];
    }
};
class SWikiLink : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWikiLink){} SLATE_ARGUMENT(UACEPluginSubsystem*,Host) SLATE_ARGUMENT(FString,Name) SLATE_ARGUMENT(bool,Identified) SLATE_ARGUMENT(TFunction<UTextureRenderTarget2D*()>,Portrait) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Host=Args._Host;Name=Args._Name;Identified=Args._Identified;Preview=Args._Portrait;State=Host->WikiState;
        ChildSlot[SNew(SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
            .IsFocusable(false).ContentPadding(0).ToolTipText(FText::FromString(TEXT("Read this entry in ACPedia")))
            .OnClicked_Lambda([this]
            {
                if(Host.IsValid())
                {
                    if(const auto* M=State->Matches.Find(Name))
                    {
                        if(Preview)State->RememberPortrait(M->Title,Preview());
                        Host->OpenWiki(M->Title,Identified);
                    }
                }
                return FReply::Handled();
            })
            [SNew(STextBlock).Text(FText::FromString(TEXT("ACPedia →"))).Font(FCoreStyle::GetDefaultFontStyle("Regular",10)).ColorAndOpacity(FACEPluginStyle::Gold())]];
        State->Resolve(Name);SetVisibility(EVisibility::Collapsed);
    }
    void Tick(const FGeometry& G,double T,float D) override
    {SCompoundWidget::Tick(G,T,D);State->Resolve(Name);}
    EVisibility GetMatchVisibility() const{return State->Matched(Name)?EVisibility::Visible:EVisibility::Hidden;}
private:
    TWeakObjectPtr<UACEPluginSubsystem> Host;
    TSharedPtr<FACEWikiState> State;
    FString Name;
    bool Identified=false;
    TFunction<UTextureRenderTarget2D*()> Preview;
};
}
TSharedRef<SWidget> UACEPluginSubsystem::MakeWikiPanel()
{
    if(!WikiState)WikiState=MakeShared<FACEWikiState>();
    double Size=20;if(Settings)Settings->TryGetNumberField(TEXT("_wiki_font_size"),Size);
    WikiState->FontSize=FMath::Clamp(int32(Size),8,32);
    WikiState->SaveFont=[Weak=TWeakObjectPtr<UACEPluginSubsystem>(this)](int32 Value)
    {if(auto* Host=Weak.Get();Host&&Host->Settings){Host->Settings->SetNumberField(TEXT("_wiki_font_size"),Value);Host->SaveSettings();}};
    if(Settings)Settings->TryGetBoolField(TEXT("_wiki_replace_examine"),WikiState->ReplaceExamine);
    WikiState->SaveReplacement=[Weak=TWeakObjectPtr<UACEPluginSubsystem>(this)](bool Value)
    {if(auto* Host=Weak.Get();Host&&Host->Settings){Host->Settings->SetBoolField(TEXT("_wiki_replace_examine"),Value);Host->SaveSettings();}};
    return SNew(SWikiPanel).State(WikiState);
}
TSharedRef<SWidget> UACEPluginSubsystem::MakeWikiLink(const FString& ObjectName,bool bIdentified,TFunction<UTextureRenderTarget2D*()> Portrait)
{
    if(!WikiState)WikiState=MakeShared<FACEWikiState>();
    auto Link=SNew(SWikiLink).Host(this).Name(ObjectName).Identified(bIdentified).Portrait(MoveTemp(Portrait));
    Link->SetVisibility(TAttribute<EVisibility>::CreateLambda([Weak=TWeakPtr<SWikiLink>(Link),S=WikiState,ObjectName]{S->Resolve(ObjectName);auto P=Weak.Pin();return P?P->GetMatchVisibility():EVisibility::Collapsed;}));
    return Link;
}
void UACEPluginSubsystem::OpenWiki(const FString& Title,bool bIdentified)
{
    if(!WikiState)WikiState=MakeShared<FACEWikiState>();
    if(bIdentified)WikiState->IdentifiedTitles.Add(Title);else WikiState->IdentifiedTitles.Remove(Title);
    WikiState->Load(Title);
    if(!IsPluginWindowOpen(TEXT("wiki")))TogglePluginWindow(TEXT("wiki"));
}
void UACEPluginSubsystem::CacheWikiExamination(const FACEAppraisalInfo& Info,const FACEWorldObject* Subject)
{
    if(!WikiState)WikiState=MakeShared<FACEWikiState>();
    if(Info.Name.IsEmpty())return;
    auto* Dat=GetGameInstance()?GetGameInstance()->GetSubsystem<UACEDatSubsystem>():nullptr;
    auto Details=ACEAppraisalKnowledge::CompactCreatureInsights(Info,Subject,Dat);
    if(WikiState->DetailsByName.Num()>=32&&!WikiState->DetailsByName.Contains(Info.Name))
        WikiState->DetailsByName.Remove(WikiState->DetailsByName.CreateConstIterator().Key());
    WikiState->DetailsByName.Add(Info.Name,Details);
    if(auto Match=WikiState->Matches.Find(Info.Name);Match&&!Match->Title.IsEmpty())
    {
        if(WikiState->CreatureDetails.Num()>=32&&!WikiState->CreatureDetails.Contains(Match->Title))
            WikiState->CreatureDetails.Remove(WikiState->CreatureDetails.CreateConstIterator().Key());
        WikiState->CreatureDetails.Add(Match->Title,MoveTemp(Details));
    }
}
void UACEPluginSubsystem::OpenWikiExamination(const FString& Name,bool Identified,TFunction<UTextureRenderTarget2D*()> Portrait,
    TFunction<bool()> StillCurrent,TFunction<void()> OnOpened)
{
    bool Replace=false;if(Settings)Settings->TryGetBoolField(TEXT("_wiki_replace_examine"),Replace);
    auto Plugin=Find(TEXT("wiki"));if(!Replace||!Plugin||!Plugin->Enabled)return;
    if(!WikiState)WikiState=MakeShared<FACEWikiState>();
    WikiState->Resolve(Name,[Weak=TWeakObjectPtr<UACEPluginSubsystem>(this),Identified,Portrait=MoveTemp(Portrait),StillCurrent=MoveTemp(StillCurrent),OnOpened=MoveTemp(OnOpened)](const FString& Title)
    {
        auto* Host=Weak.Get();bool Enabled=false;
        if(Host&&Host->Settings)Host->Settings->TryGetBoolField(TEXT("_wiki_replace_examine"),Enabled);
        if(!Enabled||Title.IsEmpty()||!StillCurrent())return;
        if(Portrait)Host->WikiState->RememberPortrait(Title,Portrait());
        Host->OpenWiki(Title,Identified);OnOpened();
    });
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWikiExaminePreferenceTest,"ACE.Wiki.ExaminePreference",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEWikiExaminePreferenceTest::RunTest(const FString&)
{
    auto* Instance=NewObject<UGameInstance>();auto* Host=NewObject<UACEPluginSubsystem>(Instance);
    Host->Settings=MakeShared<FJsonObject>();Host->WikiState=MakeShared<FACEWikiState>();
    auto Plugin=MakeShared<FACEClientPlugin>();Plugin->Id=TEXT("wiki");Plugin->Enabled=true;Host->Plugins.Add(Plugin);
    Host->WikiState->Matches.FindOrAdd(TEXT("Known creature")).Title=TEXT("Canonical creature");
    bool Opened=false,Checked=false;
    auto Try=[&](){Host->OpenWikiExamination(TEXT("Known creature"),false,{},[&](){Checked=true;return false;},[&](){Opened=true;});};
    Try();TestFalse(TEXT("Stock Examine is the default"),Checked||Opened);
    Host->Settings->SetBoolField(TEXT("_wiki_replace_examine"),true);Try();
    TestTrue(TEXT("Enabled replacement resolves the cached exact title"),Checked);
    TestFalse(TEXT("A closed or changed examination cannot open a late article"),Opened);
    Checked=false;Plugin->Enabled=false;Try();TestFalse(TEXT("Disabled Encyclopedia leaves stock Examine alone"),Checked||Opened);
    Plugin->Enabled=true;
    auto& Missing=Host->WikiState->Matches.FindOrAdd(TEXT("Missing creature"));Missing.RetryAt=FPlatformTime::Seconds()+60;
    Host->OpenWikiExamination(TEXT("Missing creature"),false,{},[](){return true;},[&](){Opened=true;});
    TestFalse(TEXT("Missing exact articles leave stock Examine alone"),Opened);
    TestEqual(TEXT("Preference checks perform no network requests"),Host->WikiState->Lookups,0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWikiAttributeLayoutTest,"ACE.Wiki.AttributeLayout",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEWikiAttributeLayoutTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender())return true;
    TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName)> Find;
    Find=[&](TSharedRef<SWidget> W,FName Tag)->TSharedPtr<SWidget>
    {if(W->GetTag()==Tag)return W;auto* Children=W->GetChildren();for(int I=0;I<Children->Num();++I)if(auto Match=Find(Children->GetChildAt(I),Tag))return Match;return nullptr;};
    FWidgetRenderer Renderer(false,true);
    for(int Width:{460,640})for(int Font:{14,20,32})
    {
        auto State=MakeShared<FACEWikiState>();State->Title=State->Article.Title=TEXT("Test creature");State->Message.Empty();State->FontSize=Font;
        ACEWikiReference::FBlock Stats;Stats.Attributes={{TEXT("Strength"),TEXT("110")},{TEXT("Endurance"),TEXT("80")},
            {TEXT("Coordination"),TEXT("115")},{TEXT("Quickness"),TEXT("115")},{TEXT("Focus"),TEXT("80")},
            {TEXT("Self"),TEXT("70")},{TEXT("Health"),TEXT("80")},{TEXT("Stamina"),TEXT("165")},{TEXT("Mana"),TEXT("70")}};
        State->Article.Blocks.Add(Stats);
        ACEWikiReference::FBlock Rewards;Rewards.Layout=ACEWikiReference::ELayout::Rewards;
        Rewards.Fields={{TEXT("XP"),TEXT("10,000")},{TEXT("Luminance"),TEXT("0")},{TEXT("Loot Tier"),TEXT("3")}};State->Article.Blocks.Add(Rewards);
        ACEWikiReference::FBlock Combat;Combat.Layout=ACEWikiReference::ELayout::Combat;
        Combat.Fields={{TEXT("Common attacks"),TEXT("Pierce, Acid")},{TEXT("Vulnerabilities"),TEXT("Bludgeon, Pierce")}};State->Article.Blocks.Add(Combat);
        ACEWikiReference::FBlock Ratings;Ratings.Layout=ACEWikiReference::ELayout::Ratings;
        Ratings.Fields={{TEXT("Melee attack"),TEXT("213")},{TEXT("Missile attack"),TEXT("Unknown")},{TEXT("Magic attack"),TEXT("Unknown")},
            {TEXT("Melee defense"),TEXT("223")},{TEXT("Missile defense"),TEXT("Unknown")},{TEXT("Magic defense"),TEXT("185")}};State->Article.Blocks.Add(Ratings);
        auto Portrait=MakeShared<FACEWikiState::FImage>();TArray<uint8> Pixels;Pixels.Init(255,16*24*4);
        Portrait->Brush=FSlateDynamicImageBrush::CreateWithImageData(FName(*FGuid::NewGuid().ToString()),FVector2D(16,24),Pixels);
        State->ExaminePortraits.Add(State->Title,Portrait);
        for(int32 I=0;I<2;++I)
        {
            ACEWikiReference::FBlock Item;Item.ImageUrl=FString::Printf(TEXT("test-item-%d"),I);
            State->Images.Add(Item.ImageUrl,Portrait);State->Article.Blocks.Add(Item);
        }
        auto Panel=SNew(SWikiPanel).State(State);const FVector2D Size(Width,1000);
        auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
        for(int Pass=0;Pass<3;++Pass){Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();}
        auto Label=Find(Panel,TEXT("WikiAttributeName.Coordination")),Value=Find(Panel,TEXT("WikiAttributeValue.Coordination"));
        auto Image=Find(Panel,TEXT("WikiPortrait")),Grid=Find(Panel,TEXT("WikiAttributes"));
        if(TestTrue(TEXT("Attribute labels, portrait and values are present"),Label&&Value&&Image&&Grid))
        {
            const auto L=Label->GetCachedGeometry(),V=Value->GetCachedGeometry(),I=Image->GetCachedGeometry(),G=Grid->GetCachedGeometry();
            TestTrue(TEXT("Portrait stays between attribute names and values at every text size"),
                L.GetAbsolutePosition().X+L.GetAbsoluteSize().X<=I.GetAbsolutePosition().X
                &&I.GetAbsolutePosition().X+I.GetAbsoluteSize().X<=V.GetAbsolutePosition().X);
            TestTrue(TEXT("Values align at the right inset"),FMath::Abs(G.GetAbsolutePosition().X+G.GetAbsoluteSize().X-10
                -(V.GetAbsolutePosition().X+V.GetAbsoluteSize().X))<2);
        }
        auto First=Find(Panel,TEXT("WikiArticleImage.test-item-0")),Second=Find(Panel,TEXT("WikiArticleImage.test-item-1"));
        if(TestTrue(TEXT("Adjacent article images are present"),First&&Second))
        {
            const auto A=First->GetCachedGeometry(),B=Second->GetCachedGeometry();
            TestTrue(TEXT("Item images share a horizontal row at every window and text size"),
                FMath::Abs(A.GetAbsolutePosition().Y-B.GetAbsolutePosition().Y)<1.f
                &&B.GetAbsolutePosition().X>=A.GetAbsolutePosition().X+A.GetAbsoluteSize().X);
        }
        Target->ReleaseResource();
    }
    return true;
}
#endif
