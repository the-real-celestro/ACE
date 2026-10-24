#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginStyle.h"
#include "ACEClientSubsystem.h"
#include "ACEHoverTooltipWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameInstance.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"

namespace
{
    using FSendEyeEvent=TFunction<void(const FString&,const FString&,TSharedPtr<FJsonValue>)>;
    bool EyeBool(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,bool Default=false)
    {bool V=Default;if(O)O->TryGetBoolField(Key,V);return V;}
    FString EyeString(const TSharedPtr<FJsonObject>& O,const TCHAR* Key)
    {FString V;if(O)O->TryGetStringField(Key,V);return V;}
    const FButtonStyle& EyeTextStyle()
    {
        static const FButtonStyle Style=[]
        {
            FButtonStyle S;S.SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource()).SetPressed(FSlateNoResource()).SetDisabled(FSlateNoResource());
            S.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));return S;
        }();return Style;
    }
    TSharedRef<SWidget> CursorSafe(const TSharedRef<SWidget>& W)
    {
        W->EnableToolTipForceField(true);
        W->SetToolTipForceFieldExpansion(TAttribute<TOptional<FSlateRect>>::CreateLambda([]
        {return TOptional<FSlateRect>(UACEHoverTooltipWidget::GetCursorExclusionRect(FSlateApplication::Get().GetCursorPos()));}));
        return W;
    }
    class SACEThirdEyePanel : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SACEThirdEyePanel){} SLATE_ARGUMENT(UACEPluginSubsystem*,Host) SLATE_ARGUMENT(FSendEyeEvent,Send) SLATE_ARGUMENT(bool,Settings) SLATE_END_ARGS()
        void Construct(const FArguments& A)
        {
            Host=A._Host;Send=A._Send;Page=A._Settings?1:0;FACEPluginStyle::Use(A._Host,TEXT("thirdeye"));
            for(auto P:A._Host->Plugins)if(P->Id==TEXT("thirdeye"))Plugin=P;
            auto Header=SNew(SHorizontalBox);
            Header->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)[Check(TEXT("Watch"),TEXT("watching"),true)];
            for(int32 I=0;I<3;++I)
                Header->AddSlot().AutoWidth().Padding(1,0)[SNew(SACEPluginTab).Tag(FName(*FString::Printf(TEXT("ThirdEye.Tab.%d"),I)))
                    .Text(FText::FromString(I==0?TEXT("Found"):I==1?TEXT("Setup"):TEXT("Options"))).ContentPadding(FMargin(4,1))
                    .Selected_Lambda([this,I]{return Page==I;}).OnClicked_Lambda([this,I]{Page=I;Rebuild();return FReply::Handled();})];
            ChildSlot[SNew(SBorder).BorderImage(&FACEPluginStyle::Current()->Background).Padding(4)
                [SAssignNew(Root,SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Header]
                +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[SAssignNew(Body,SVerticalBox)]]];
            Rebuild();
        }
        void Tick(const FGeometry& G,double Time,float Delta) override
        {
            SCompoundWidget::Tick(G,Time,Delta);
            auto P=Plugin.Pin();if(!P)return;
            if((Page==0&&Revision!=P->DetectedRevision)||(Page!=0&&Config.Pin()!=P->SavedState))Rebuild();
            // Content determines height, including lists taller than the viewport.
            // No paging, list viewport, scrollbar, or saved blank vertical area.
            Root->SlatePrepass(G.Scale);
            P->PanelSize=FVector2D(G.GetLocalSize().X,Root->GetDesiredSize().Y+8);
        }
    private:
        TWeakObjectPtr<UACEPluginSubsystem> Host;TWeakPtr<FACEClientPlugin> Plugin;
        FSendEyeEvent Send;int32 Page=0;uint32 Revision=MAX_uint32;
        TWeakPtr<FJsonObject> Config;FString Draft;bool Exact=false;
        TSharedPtr<SVerticalBox> Root,Body;TSharedPtr<SEditableTextBox> NameInput;
        TSharedPtr<FJsonObject> State()const {auto P=Plugin.Pin();return P?P->SavedState:nullptr;}
        TSharedRef<SWidget> Text(const FString& Value,int32 Size=13)
        {return SNew(SACEPluginText).Text(FText::FromString(Value)).Font(FACEPluginStyle::NativeFont("Regular",Size)).AutoWrapText(false);}
        void Add(TSharedRef<SWidget> W){Body->AddSlot().AutoHeight().Padding(0,1)[W];}
        TSharedRef<SWidget> Action(const FString& Label,FName Tag,TFunction<void()> Click)
        {
            return CursorSafe(SNew(SButton).ClickMethod(EButtonClickMethod::MouseDown).ButtonStyle(&EyeTextStyle()).ContentPadding(FMargin(2,1)).HAlign(HAlign_Left).VAlign(VAlign_Center)
                .IsFocusable(false).Cursor(EMouseCursor::Default).Tag(Tag)
                .OnClicked_Lambda([Click]{Click();return FReply::Handled();})[Text(Label)]);
        }
        TSharedRef<SWidget> Check(const FString& Label,const FString& Key,bool Default=false)
        {
            return SNew(SCheckBox).Style(&FACEPluginStyle::Current()->Check).Tag(FName(*(TEXT("ThirdEye.Option.")+Key)))
                .IsChecked_Lambda([this,Key,Default]{return EyeBool(State(),*Key,Default)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this,Key](ECheckBoxState V){Send(TEXT("change"),Key,MakeShared<FJsonValueBoolean>(V==ECheckBoxState::Checked));})[Text(Label)];
        }
        FString Feedback()const
        {
            auto P=Plugin.Pin();if(!P)return FString();
            TFunction<FString(const TArray<TSharedPtr<FJsonValue>>&)> Find;
            Find=[&](const TArray<TSharedPtr<FJsonValue>>& Rows)->FString
            {
                for(const auto& V:Rows)
                {
                    auto R=V->AsObject();if(EyeString(R,TEXT("id"))==TEXT("feedback"))return EyeString(R,TEXT("text"));
                    const TArray<TSharedPtr<FJsonValue>>* Children=nullptr;
                    if(R->TryGetArrayField(TEXT("children"),Children)){auto Value=Find(*Children);if(!Value.IsEmpty())return Value;}
                }
                return FString();
            };return Find(P->PanelRows);
        }
        void Rebuild()
        {
            auto P=Plugin.Pin();if(!P||!Body)return;
            FACEPluginStyle::Use(Host.Get(),TEXT("thirdeye"));Revision=P->DetectedRevision;Config=P->SavedState;Body->ClearChildren();
            if(Page==0)
            {
                if(P->DetectedObjects.IsEmpty()){Add(Text(P->Running?TEXT("No matches"):TEXT("Enable Third Eye to watch for matches")));return;}
                for(const auto& Value:P->DetectedObjects)
                {
                    auto D=Value->AsObject();const uint32 Id=uint32(D->GetNumberField(TEXT("id"))),Wcid=uint32(D->GetNumberField(TEXT("wcid")));
                    const FString Key=FString::Printf(TEXT("%u"),Id),Name=D->GetStringField(TEXT("name"));
                    auto Label=SNew(SACEPluginText).Text(FText::FromString(FString::Printf(TEXT("%s  %.1f m"),*Name,D->GetNumberField(TEXT("distance")))))
                        .Font(FACEPluginStyle::NativeFont("Regular",13)).AutoWrapText(false);
                    Label->SetClipping(EWidgetClipping::ClipToBounds);
                    Add(SNew(SHorizontalBox).Tag(FName(*(TEXT("ThirdEye.Item.")+Key)))
                        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[CursorSafe(SNew(SButton).ClickMethod(EButtonClickMethod::MouseDown).ButtonStyle(&EyeTextStyle()).ContentPadding(FMargin(0,0)).HAlign(HAlign_Left).IsFocusable(false)
                            .Tag(FName(*(TEXT("ThirdEye.Select.")+Key))).Cursor(EMouseCursor::Default)
                            .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Select ")+Name)))
                            .OnClicked_Lambda([H=Host,Id,Wcid]
                            {
                                if(H.IsValid())
                                {
                                    auto* C=H->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject O;
                                    if(C->GetWorldObject(int32(Id),O)&&uint32(O.WeenieClassId)==Wcid)C->SelectObject(int32(Id));
                                }
                                return FReply::Handled();
                            })[Label])]
                        +SHorizontalBox::Slot().AutoWidth().Padding(5,0,0,0)[SNew(SBox).WidthOverride(16)
                            [Action(TEXT("x"),FName(*(TEXT("ThirdEye.Dismiss.")+Key)),[this,Key]{Send(TEXT("click"),TEXT("dismiss_")+Key,nullptr);})]]);
                }
                return;
            }
            if(Page==1)
            {
                Add(SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SAssignNew(NameInput,SEditableTextBox)
                    .Style(&FACEPluginStyle::Current()->Edit).Font(FACEPluginStyle::NativeFont("Regular",13)).HintText(FText::FromString(TEXT("Name to watch")))
                    .Text(FText::FromString(Draft)).OnTextChanged_Lambda([this](const FText& T){Draft=T.ToString().Left(256);})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[Action(TEXT("+"),TEXT("ThirdEye.Add"),[this]
                    {Send(TEXT("change"),TEXT("draft"),MakeShared<FJsonValueString>(Draft));Send(TEXT("change"),TEXT("exact"),MakeShared<FJsonValueBoolean>(Exact));Send(TEXT("click"),TEXT("add"),nullptr);Draft.Empty();NameInput->SetText(FText::GetEmpty());})]);
                Add(SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).Style(&FACEPluginStyle::Current()->Check)
                    .IsChecked_Lambda([this]{return Exact?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Exact=V==ECheckBoxState::Checked;})[Text(TEXT("Exact name"))]]
                    +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[Action(TEXT("+ Selected"),TEXT("ThirdEye.AddSelected"),[this]
                    {Send(TEXT("change"),TEXT("exact"),MakeShared<FJsonValueBoolean>(Exact));Send(TEXT("click"),TEXT("add_selected"),nullptr);})]);
                Add(SNew(SACEPluginText).Font(FACEPluginStyle::NativeFont("Regular",12)).AutoWrapText(true)
                    .Visibility_Lambda([this]{return Feedback().IsEmpty()?EVisibility::Collapsed:EVisibility::Visible;})
                    .Text_Lambda([this]{return FText::FromString(Feedback());}));
                const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;auto S=State();
                if(S&&S->TryGetArrayField(TEXT("rules"),Rules))for(const auto& V:*Rules)
                {
                    auto R=V->AsObject();const FString Key=FString::Printf(TEXT("%.0f"),R->GetNumberField(TEXT("id")));
                    Add(SNew(SHorizontalBox).Tag(FName(*(TEXT("ThirdEye.Rule.")+Key)))
                        +SHorizontalBox::Slot().FillWidth(1)[SNew(SCheckBox).Clipping(EWidgetClipping::ClipToBounds).Style(&FACEPluginStyle::Current()->Check)
                            .IsChecked(EyeBool(R,TEXT("enabled"),true)?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                            .OnCheckStateChanged_Lambda([this,Key](ECheckBoxState V){Send(TEXT("change"),TEXT("enabled_")+Key,MakeShared<FJsonValueBoolean>(V==ECheckBoxState::Checked));})
                            .ToolTip(FACEPluginStyle::Tooltip(FText::FromString((EyeBool(R,TEXT("exact"))?TEXT("Equals: "):TEXT("Contains: "))+EyeString(R,TEXT("name")))))
                            [Text((EyeBool(R,TEXT("exact"))?TEXT("= "):TEXT("~ "))+EyeString(R,TEXT("name")))]]
                        +SHorizontalBox::Slot().AutoWidth().Padding(5,0,0,0)[SNew(SBox).WidthOverride(16)
                            [Action(TEXT("x"),FName(*(TEXT("ThirdEye.Remove.")+Key)),[this,Key]{Send(TEXT("click"),TEXT("remove_")+Key,nullptr);})]]);
                }
                return;
            }
            Add(SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(TEXT("Range (m)"))]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(70)[SNew(SSpinBox<double>).Style(&FACEPluginStyle::Current()->Spin)
                    .Font(FACEPluginStyle::NativeFont("Regular",13)).MinValue(5).MaxValue(240).Delta(5).MaxFractionalDigits(0)
                    .Value_Lambda([this]{double V=240;auto S=State();if(S)S->TryGetNumberField(TEXT("range"),V);return V;})
                    .OnValueCommitted_Lambda([this](double V,ETextCommit::Type){Send(TEXT("change"),TEXT("range"),MakeShared<FJsonValueNumber>(V));})]]);
            Add(Check(TEXT("Chat alerts"),TEXT("chat_alerts"),true));
            Add(Check(TEXT("World markers"),TEXT("world_markers")));
            Add(Check(TEXT("Map markers"),TEXT("map_markers"),true));
        }
    };
}
TSharedRef<SWidget> UACEPluginSubsystem::MakeThirdEyePanel(bool OpenSetup)
{
    const TWeakObjectPtr<UACEPluginSubsystem> H(this);
    return SNew(SACEThirdEyePanel).Host(this).Settings(OpenSetup).Send([H](const FString& Type,const FString& Key,TSharedPtr<FJsonValue> Value)
    {
        if(!H.IsValid())return;auto P=H->Find(TEXT("thirdeye"));
        if(!P||!P->Enabled||!P->Running)return;
        auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("type"),Type);E->SetStringField(TEXT("id"),Key);
        if(Value)E->SetField(TEXT("value"),Value);H->QueuePluginEvent(*P,E);P->NextDecision=0;
    });
}
