#include "Mods/ACEPluginDesktop.h"
#include "Mods/ACEPluginPresentation.h"
#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginIcon.h"
#include "Mods/ACEActionDeckSubsystem.h"
#include "Mods/ACEActionDeckLayout.h"
#include "Mods/ACEPluginOrder.h"
#include "Widgets/Images/SImage.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACERuntimeOptions.h"
#include "UI/ACEUIElementManager.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "ACELobbyArt.h"

namespace
{
    const FString BarId = TEXT("__bar");
    // Hunt Ledger was fixed-size; its old .size entry stored a snapshot with
    // editor chrome. Only new user resizes should override its automatic height.
    FString SizeKey(const FString& Id){return Id+(Id==TEXT("huntledger")?TEXT(".body_size"):TEXT(".size"));}
    FString PanelSizeKey(const FString& Id)
    {
        return Id+((Id==TEXT("looteditor")||Id==TEXT("itemtools")||Id==TEXT("autocraft")||Id==TEXT("ucmmicro")||Id==TEXT("ucm.log")||Id==TEXT("thirdeye"))?TEXT(".compact_size"):TEXT(".size"));
    }
    const FButtonStyle& DockButtonStyle(){return FACEPluginStyle::Current()->DockButton;}
    TSharedRef<SWidget> DockIcon(const FString& Id)
    {return SNew(SBox).WidthOverride(26).HeightOverride(26)[SNew(SACEPluginIcon).Id(Id)];}
    TSharedRef<SWidget> WindowIcon(UACEPluginSubsystem* Host,const FString& Id)
    {
        if(Host)for(const auto& Plugin:Host->Plugins)
            if(Plugin->Id==Id)return SNew(SACEPluginBadge).Plugin(Plugin);
        return DockIcon(Id==TEXT("__manager")?TEXT("plugin.options"):Id);
    }
    TSharedRef<STextBlock> Caption(const FString& Text, int32 Size = 12)
    { return SNew(SACEPluginText).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)); }

    /** Codex window backdrop: retail title plaque (0x06004D26 header, 42px) over the lobby's tinted
     *  window body, a navy wash and a 1px gold border. Opacity fades all of it; text is unaffected. */
    class SCodexChrome : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SCodexChrome) : _Flat(false) {}
            SLATE_ARGUMENT(bool, Flat)
            SLATE_ARGUMENT(TSharedPtr<FACEPluginStyle>, Style)
            SLATE_ATTRIBUTE(bool, HeaderHidden)
        SLATE_END_ARGS()
        void Construct(const FArguments& Args){Style=Args._Style;HeaderHidden=Args._HeaderHidden;Flat=Args._Flat;SetVisibility(EVisibility::HitTestInvisible);}
        virtual FVector2D ComputeDesiredSize(float) const override {return FVector2D::ZeroVector;}
        virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& WidgetStyle,bool) const override
        {
            const FVector2D S=G.GetLocalSize();
            const float A=Style?Style->GetBackgroundOpacity():1.f;
            if(A<=0.f||S.X<8||S.Y<8)return Layer;
            const bool NoHeader=HeaderHidden.Get(false);
            const double HH=NoHeader?0.:FACEPluginStyle::HeaderHeight();
            const FLinearColor Mod=WidgetStyle.GetColorAndOpacityTint();
            const FSlateBrush* White=FCoreStyle::Get().GetBrush("WhiteBrush");
            auto Box=[&](const FSlateBrush* Brush,FVector2D Pos,FVector2D Size,FLinearColor Color)
            {if(Brush&&Size.X>0&&Size.Y>0)FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(Size,FSlateLayoutTransform(Pos)),Brush,ESlateDrawEffect::None,Color*Mod);};
            if(Flat)
            {
                // Use the actual retail blue window texture, independently of world tint.
                // The blue glaze keeps the narrow bar recognizable beside the gold AC trim.
                if(const FSlateBrush* Body=ACELobbyArt::TintedWindowBody(0))
                    Box(Body,FVector2D::ZeroVector,S,FLinearColor(.7f,.85f,1.f,A));
                else Box(White,FVector2D::ZeroVector,S,FLinearColor(FColor(14,33,73)).CopyWithNewOpacity(A));
                Box(White,FVector2D::ZeroVector,S,FLinearColor(FColor(16,46,112)).CopyWithNewOpacity(.3f*A));
                const TArray<FVector2D> Border={FVector2D(.5,.5),FVector2D(S.X-.5,.5),FVector2D(S.X-.5,S.Y-.5),FVector2D(.5,S.Y-.5),FVector2D(.5,.5)};
                FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Border,ESlateDrawEffect::None,FLinearColor(FColor(112,87,44)).CopyWithNewOpacity(A)*Mod,true,1.f);
                return Layer+1;
            }
            const FACELobbyArt* Art=ACELobbyArt::Get();
            const FLinearColor Fade(1,1,1,A);
            if(const FSlateBrush* Body=Art?ACELobbyArt::TintedWindowBody(FACEPluginStyle::WindowTint()):nullptr)
                Box(Body,FVector2D(0,HH),FVector2D(S.X,S.Y-HH),Fade);
            else Box(White,FVector2D(0,HH),FVector2D(S.X,S.Y-HH),FLinearColor(.006f,.014f,.04f,.92f*A));
            // Navy wash keeps small text legible over the art.
            FLinearColor Wash(FColor(4,9,22));Wash.A=.42f*A;
            Box(White,FVector2D(0,HH),FVector2D(S.X,S.Y-HH),Wash);
            TArray<FVector2D> Edge={FVector2D(.5,HH),FVector2D(.5,S.Y-.5),FVector2D(S.X-.5,S.Y-.5),FVector2D(S.X-.5,HH)};
            if(NoHeader)Edge.Add(FVector2D(.5,HH));
            if(!(Style&&Style->IsFrameHidden()))FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Edge,ESlateDrawEffect::None,FACEPluginStyle::Gold(.5f*A)*Mod,true,1.f);
            if(!NoHeader&&Art)
            {
                const double K=HH/FACELobbyArt::WindowHeaderH,L=80*K,R=60*K;
                FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(L,HH),FSlateLayoutTransform()),&Art->WindowHeader[0],ESlateDrawEffect::None,Fade*Mod);
                FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(FMath::Max(0.,S.X-L-R),HH),FSlateLayoutTransform(FVector2D(L,0))),&Art->WindowHeader[1],ESlateDrawEffect::None,Fade*Mod);
                FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(R,HH),FSlateLayoutTransform(FVector2D(S.X-R,0))),&Art->WindowHeader[2],ESlateDrawEffect::None,Fade*Mod);
            }
            else if(!NoHeader)Box(White,FVector2D::ZeroVector,FVector2D(S.X,HH),FLinearColor(.12f,.07f,.03f,.95f*A));
            return Layer+1;
        }
    private:
        TSharedPtr<FACEPluginStyle> Style;
        bool Flat=false;
        TAttribute<bool> HeaderHidden;
    };

    class SPluginFrame : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SPluginFrame) : _Host(nullptr), _Frameless(false), _CompactWhenLocked(false), _Minimized(false) {}
            SLATE_ATTRIBUTE(FString, Title)
            SLATE_ARGUMENT(UACEPluginSubsystem*, Host)
            SLATE_ARGUMENT(FString, WindowId)
            SLATE_ARGUMENT(bool, Frameless)
            SLATE_ARGUMENT(TFunction<void(FVector2D,bool)>, Move)
            SLATE_ARGUMENT(TFunction<void(FVector2D,bool)>, Resize)
            SLATE_ARGUMENT(TFunction<void()>, Raise)
            SLATE_ARGUMENT(TFunction<void()>, Close)
            SLATE_ARGUMENT(bool, CompactWhenLocked)
            SLATE_ATTRIBUTE(bool, Locked)
            SLATE_ARGUMENT(TFunction<bool()>, CanManipulate)
            SLATE_ATTRIBUTE(bool, Minimized)
            SLATE_ARGUMENT(TFunction<void()>, ToggleMinimized)
            /** Codex title plaque: icon on the ring and a caps subtitle under the title. */
            SLATE_ARGUMENT(TSharedPtr<SWidget>, Icon)
            SLATE_ATTRIBUTE(FString, Subtitle)
            SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Host=Args._Host;Id=Args._WindowId;
            Retail=Host.IsValid()?FACEPluginStyle::Use(Host.Get(),Id):FACEPluginStyle::Current();
            ArtworkReady=Retail->HasDatArtwork();
            SetClipping(EWidgetClipping::ClipToBounds);
            Move = Args._Move; Resize = Args._Resize; Raise = Args._Raise;
            Locked=Args._Locked;
            Frameless=Args._Frameless;
            CanManipulate=Args._CanManipulate;
            if(Frameless){ChildSlot[Args._Content.Widget];return;}
            auto Title = SNew(SHorizontalBox);
            const bool IsBar=bool(Args._ToggleMinimized);
            if(IsBar&&Retail->Codex){ConstructCodexBar(Args);return;}
            if(!IsBar&&Retail->Codex){ConstructCodex(Args);return;}
            if(IsBar)
            {
                Title->AddSlot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(22).HeightOverride(2)
                    [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.6f,.65f,.7f))]];
                Title->AddSlot().AutoWidth()[SNew(SACEPluginButton).Cursor(EMouseCursor::Default).ContentPadding(FMargin(FACEPluginStyle::ChromeButtonPadding)).Tag(TEXT("PluginBarMinimize")).ButtonStyle(&DockButtonStyle()).IsFocusable(false)
                    .ToolTip(FACEPluginStyle::Tooltip(TAttribute<FText>::CreateLambda([Mini=Args._Minimized](){return FText::FromString(Mini.Get()?TEXT("Expand plugin bar"):TEXT("Minimize plugin bar"));})))
                    .OnClicked_Lambda([Toggle=Args._ToggleMinimized](){Toggle();return FReply::Handled();})
                    [SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Bold",12))
                        .Text_Lambda([Mini=Args._Minimized](){return FText::FromString(Mini.Get()?TEXT("+"):TEXT("-"));})]];
            }
            else
            {
                if(Args._Icon)Title->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
                    [SNew(SBox).WidthOverride(18).HeightOverride(18).Tag(TEXT("PluginFrameIcon"))
                        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[Args._Icon.ToSharedRef()]]];
                Title->AddSlot().FillWidth(1).VAlign(VAlign_Center)[SNew(SACEPluginText).Text_Lambda([Title=Args._Title]{return FText::FromString(Title.Get());}).Font(FCoreStyle::GetDefaultFontStyle("Regular",12))];
            }
            if(!IsBar&&Host.IsValid())Title->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                [SNew(SBox).WidthOverride(20).HeightOverride(20)[SNew(SACEPluginButton).Cursor(EMouseCursor::Default).Tag(TEXT("PluginFrameSettings")).ContentPadding(FMargin(0))
                    .ButtonStyle(&Retail->DockButton).IsFocusable(false).ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Plugin settings"))))
                    .OnClicked_Lambda([H=Host,Window=Id]{if(H.IsValid())H->OpenPluginSettings(Window);return FReply::Handled();})
                    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[DockIcon(TEXT("plugin.options"))]]]];
            if(!IsBar&&Host.IsValid())Title->AddSlot().AutoWidth()[SNew(SACEPluginButton).Cursor(EMouseCursor::Default).ContentPadding(FMargin(FACEPluginStyle::ChromeButtonPadding)).Tag(TEXT("PluginFramePin"))
                .IsFocusable(false).ButtonStyle(&Retail->DockButton)
                .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Pin and hide frame. Hold Quick-show pinned windows to move or unpin."))))
                .OnClicked_Lambda([H=Host,Window=Id]{if(H.IsValid())H->SetPluginWindowPinned(Window,!H->IsPluginWindowPinned(Window));return FReply::Handled();})
                [SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Regular",12))
                    .Text_Lambda([H=Host,Window=Id]{return FText::FromString(H.IsValid()&&H->IsPluginWindowPinned(Window)?TEXT("Unpin"):TEXT("Pin"));})]];
            if (Args._Close) Title->AddSlot().AutoWidth()[SNew(SACEPluginButton).Cursor(EMouseCursor::Default).ContentPadding(FMargin(FACEPluginStyle::ChromeButtonPadding)).IsFocusable(false).ButtonStyle(&DockButtonStyle())
                .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Hide window (plugin keeps running)"))))
                .OnClicked_Lambda([Close=Args._Close](){Close();return FReply::Handled();})[Caption(TEXT("X"))]];
            ChildSlot[SNew(SBorder).BorderImage(&Retail->Frame).Padding(IsBar?0:3)
                [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(IsBar?18:ACELedger::ChromeHeaderHeight())
                    .Tag(Args._CompactWhenLocked?FName(TEXT("LedgerHeader")):FName(NAME_None))
                    .Visibility_Lambda([Lock=Locked]{return Lock.Get(false)?EVisibility::Collapsed:EVisibility::Visible;})
                    [SNew(SBorder).Tag(IsBar?TEXT("PluginBarHandle"):TEXT("PluginWindowHandle")).BorderImage(&Retail->TitleBackground)
                    .Padding(IsBar?0:3)
                    .ToolTip(FACEPluginStyle::Tooltip(TAttribute<FText>::CreateLambda([this](){return FText::FromString(CanManipulate&&!CanManipulate()?TEXT("Unlock the UI or hold Quick-show pinned windows to move"):TEXT("Drag to move"));})))
                    .Cursor_Lambda([this](){return CanManipulate&&!CanManipulate()?EMouseCursor::Default:EMouseCursor::CardinalCross;})
                    .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent& E)
                    {
                        if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
                        if(!Move||(CanManipulate&&!CanManipulate()))return FReply::Handled();
                        Dragging=true;return FReply::Handled().CaptureMouse(SharedThis(this));
                    })[Title]]]
                +SVerticalBox::Slot().FillHeight(1)[SNew(SBox).Padding(0,IsBar?0:3)
                    .Tag(Args._CompactWhenLocked?FName(TEXT("LedgerContent")):FName(NAME_None))
                    .Visibility_Lambda([Mini=Args._Minimized](){return Mini.Get(false)?EVisibility::Collapsed:EVisibility::Visible;})[Args._Content.Widget]]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SNew(SBorder).Padding(FMargin(12,2)).Visibility_Lambda([this]{return Resize&&(!CanManipulate||CanManipulate())?EVisibility::Visible:EVisibility::Collapsed;})
                    .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Drag to resize")))).Cursor(EMouseCursor::ResizeSouthEast)
                    .OnMouseButtonDown_Lambda([this](const FGeometry&,const FPointerEvent& E){if(E.GetEffectingButton()!=EKeys::LeftMouseButton||(CanManipulate&&!CanManipulate()))return FReply::Unhandled();Resizing=true;return FReply::Handled().CaptureMouse(SharedThis(this));})[Caption(TEXT("◢"))]]]];
        }
        void ConstructCodexBar(const FArguments& Args)
        {
            auto Header=SNew(SBorder).Tag(TEXT("PluginBarHandle"))
                .BorderImage(FCoreStyle::Get().GetBrush("NoBrush")).Padding(FMargin(3,1))
                .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Drag to move the plugin bar while unlocked. Hold Quick-show if pinned."))))
                .Cursor_Lambda([this]{return CanManipulate&&!CanManipulate()?EMouseCursor::Default:EMouseCursor::CardinalCross;})
                .OnMouseButtonDown_Lambda([this](const FGeometry&,const FPointerEvent& E){return BeginDrag(E);})
                [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,3,0)
                    [SNew(SBox).HAlign(HAlign_Left)[SNew(SBox).WidthOverride(10).HeightOverride(2)
                        [SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush")).ColorAndOpacity(FLinearColor(.55f,.40f,.20f))]]]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(14).HeightOverride(14)
                        [SNew(SACEPluginButton).Cursor(EMouseCursor::Default).Tag(TEXT("PluginBarMinimize")).ButtonStyle(&TitleControlStyle())
                            .ContentPadding(FMargin(0)).HAlign(HAlign_Center).VAlign(VAlign_Center).IsFocusable(false)
                            .ToolTip(FACEPluginStyle::Tooltip(TAttribute<FText>::CreateLambda([Mini=Args._Minimized]{return FText::FromString(Mini.Get()?TEXT("Expand plugin bar"):TEXT("Minimize plugin bar"));})))
                            .OnClicked_Lambda([Toggle=Args._ToggleMinimized]{Toggle();return FReply::Handled();})
                            [SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Regular",10)).ColorAndOpacity(FACEPluginStyle::Cream())
                                .Text_Lambda([Mini=Args._Minimized]{return FText::FromString(Mini.Get()?TEXT("+"):TEXT("-"));})]]]];
            ChildSlot[SNew(SOverlay)
                +SOverlay::Slot()[SNew(SCodexChrome).Tag(TEXT("PluginBarCodexChrome")).Style(Retail).Flat(true).HeaderHidden(true)]
                +SOverlay::Slot()[SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(18)[Header]]
                    +SVerticalBox::Slot().FillHeight(1)
                        [SNew(SBox).Visibility_Lambda([Mini=Args._Minimized]{return Mini.Get(false)?EVisibility::Collapsed:EVisibility::Visible;})[Args._Content.Widget]]]];
        }
        FReply BeginDrag(const FPointerEvent& E)
        {
            if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
            if(!Move||(CanManipulate&&!CanManipulate()))return FReply::Handled();
            Dragging=true;return FReply::Handled().CaptureMouse(SharedThis(this));
        }
        static const FButtonStyle& TitleControlStyle()
        {
            static const FButtonStyle Style=[]
            {
                FButtonStyle S=FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
                S.SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource()).SetPressed(FSlateNoResource())
                    .SetDisabled(FSlateNoResource())
                    .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));return S;
            }();return Style;
        }
        void ConstructCodex(const FArguments& Args)
        {
            auto Glyph=[](SACELobbyGlyph::EKind Kind){return SNew(SACELobbyGlyph).Kind(Kind).Size(22.f).Color(FLinearColor(1.f,.96f,.82f));};
            // The plaque's left piece carries the silver ring; the fancy gold title starts just after it,
            // with the caps subtitle on the same line.
            auto Title=SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(Id==TEXT("ucmmicro")?24.f:FMath::RoundToFloat(56*FACEPluginStyle::HeaderHeight()/42.)).HAlign(HAlign_Center)
                    [SNew(SBox).WidthOverride(24).HeightOverride(24).Tag(TEXT("PluginFrameIcon"))
                        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[Args._Icon.IsValid()?Args._Icon.ToSharedRef():SNullWidget::NullWidget]]]]
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,0,0)[SNew(SACEPluginText).Visibility_Lambda([this](){return Id==TEXT("ucmmicro")&&GetCachedGeometry().GetLocalSize().X<240?EVisibility::Collapsed:EVisibility::Visible;}).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Text_Lambda([T=Args._Title]{return FText::FromString(T.Get());})
                    .Font(FCoreStyle::GetDefaultFontStyle("Fancy",18)).ColorAndOpacity(FACEPluginStyle::Gold())]
                // Always present so the title buttons stay right-aligned even without a subtitle.
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Id==TEXT("ucmmicro")?FMargin(0):FMargin(10,0,6,0))[SNew(SBox)[SNew(SACEPluginText).Text_Lambda([T=Args._Subtitle]{return FText::FromString(T.Get().ToUpper());})
                    .Visibility_Lambda([this,T=Args._Subtitle]{return Id==TEXT("ucmmicro")||T.Get().IsEmpty()||GetCachedGeometry().GetLocalSize().X<480?EVisibility::Collapsed:EVisibility::Visible;})
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular",9)).ColorAndOpacity(FACEPluginStyle::Muted())]];
            if(Host.IsValid())Title->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                [SNew(SBox).WidthOverride(34).HeightOverride(32)[SNew(SACEPluginButton).Cursor(EMouseCursor::Default).Tag(TEXT("PluginFrameSettings"))
                    .ContentPadding(FMargin(1)).ButtonStyle(&TitleControlStyle()).IsFocusable(false)
                    .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Plugin settings"))))
                    .OnClicked_Lambda([H=Host,Window=Id]{if(H.IsValid())H->OpenPluginSettings(Window);return FReply::Handled();})
                    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[DockIcon(TEXT("plugin.options"))]]]];
            if(Host.IsValid())Title->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                [SNew(SBox).WidthOverride(34).HeightOverride(32)[SNew(SACEPluginButton).Cursor(EMouseCursor::Default).ContentPadding(FMargin(0)).Tag(TEXT("PluginFramePin"))
                .IsFocusable(false).ButtonStyle(&TitleControlStyle())
                .ToolTip(FACEPluginStyle::Tooltip(TAttribute<FText>::CreateLambda([H=Host,Window=Id]{return FText::FromString(H.IsValid()&&H->IsPluginWindowPinned(Window)?TEXT("Unpin"):TEXT("Pin and hide frame. Hold Quick-show pinned windows to move or unpin."));})))
                .OnClicked_Lambda([H=Host,Window=Id]{if(H.IsValid())H->SetPluginWindowPinned(Window,!H->IsPluginWindowPinned(Window));return FReply::Handled();})
                [Glyph(SACELobbyGlyph::EKind::Lock)]]];
            if(Args._Close)Title->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
                [SNew(SBox).WidthOverride(34).HeightOverride(32)[SNew(SACEPluginButton).Cursor(EMouseCursor::Default).ContentPadding(FMargin(0)).Tag(TEXT("PluginFrameClose")).IsFocusable(false).ButtonStyle(&TitleControlStyle())
                .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Hide window (plugin keeps running)"))))
                .OnClicked_Lambda([Close=Args._Close](){Close();return FReply::Handled();})[Glyph(SACELobbyGlyph::EKind::Cross)]]];
            ChildSlot[SNew(SOverlay)
                +SOverlay::Slot()[SNew(SCodexChrome).Style(Retail).HeaderHidden(Locked)]
                +SOverlay::Slot()[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(FACEPluginStyle::HeaderHeight())
                    .Tag(Args._CompactWhenLocked?FName(TEXT("LedgerHeader")):FName(NAME_None))
                    .Visibility_Lambda([Lock=Locked]{return Lock.Get(false)?EVisibility::Collapsed:EVisibility::Visible;})
                    [SNew(SBorder).Tag(TEXT("PluginWindowHandle")).BorderImage(FCoreStyle::Get().GetBrush("NoBrush")).Padding(FMargin(0,0,0,4))
                    .ToolTip(FACEPluginStyle::Tooltip(TAttribute<FText>::CreateLambda([this](){return FText::FromString(CanManipulate&&!CanManipulate()?TEXT("Unlock the UI or hold Quick-show pinned windows to move"):TEXT("Drag to move"));})))
                    .Cursor_Lambda([this](){return CanManipulate&&!CanManipulate()?EMouseCursor::Default:EMouseCursor::CardinalCross;})
                    .OnMouseButtonDown_Lambda([this](const FGeometry&,const FPointerEvent& E){return BeginDrag(E);})[Title]]]
                +SVerticalBox::Slot().FillHeight(1)[SNew(SBox).Padding((Args._CompactWhenLocked||Id==TEXT("ucmmicro"))?FMargin(0,6,0,0):FMargin(8,6,8,0))
                    .Tag(Args._CompactWhenLocked?FName(TEXT("LedgerContent")):FName(NAME_None))[Args._Content.Widget]]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush")).Padding(FMargin(8,2,4,4))
                    .Visibility_Lambda([this]{return Resize&&(!CanManipulate||CanManipulate())?EVisibility::Visible:EVisibility::Collapsed;})
                    .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Drag to resize")))).Cursor(EMouseCursor::ResizeSouthEast)
                    .OnMouseButtonDown_Lambda([this](const FGeometry&,const FPointerEvent& E){if(E.GetEffectingButton()!=EKeys::LeftMouseButton||(CanManipulate&&!CanManipulate()))return FReply::Unhandled();Resizing=true;return FReply::Handled().CaptureMouse(SharedThis(this));})
                    [SNew(SACELobbyGlyph).Kind(SACELobbyGlyph::EKind::Grip).Size(10.f).Color(FACEPluginStyle::Gold(.7f))]]]];
        }
        virtual void Tick(const FGeometry& G,double Time,float Delta) override
        {
            SCompoundWidget::Tick(G,Time,Delta);
            if(!Host.IsValid())return;
            bool ArtworkChanged=false;
            if(!ArtworkReady){Retail->Refresh();ArtworkChanged=ArtworkReady=Retail->HasDatArtwork();}
            const float Opacity=Host->GetPluginWindowOpacity(Id);
            const bool Hide=Host->IsPluginWindowChromeHidden(Id);
            if(!ArtworkChanged&&LastOpacity==Opacity&&LastHidden==Hide)return;
            LastOpacity=Opacity;LastHidden=Hide;Retail->SetWindowAppearance(Opacity,Hide);
            // Brush updates repaint the existing tree, preserving menus, focus and scroll.
            TFunction<void(SWidget&)> Repaint=[&](SWidget& W){W.Invalidate(ArtworkChanged?EInvalidateWidgetReason::Layout|EInvalidateWidgetReason::Paint:EInvalidateWidgetReason::Paint);
                if(auto* Children=W.GetChildren())for(int32 I=0;I<Children->Num();++I)Repaint(Children->GetChildAt(I).Get());};
            Repaint(*this);
        }
        virtual FReply OnPreviewMouseButtonDown(const FGeometry&,const FPointerEvent& E) override
        {
            if(Raise)Raise();
            return FReply::Unhandled();
        }
        virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override
        {
            if(!Frameless||E.GetEffectingButton()!=EKeys::LeftMouseButton||(CanManipulate&&!CanManipulate()))return FReply::Unhandled();
            const auto At=G.AbsoluteToLocal(E.GetScreenSpacePosition());
            Resizing=Resize&&At.X>=G.GetLocalSize().X-24&&At.Y>=G.GetLocalSize().Y-24;
            Dragging=!Resizing;return FReply::Handled().CaptureMouse(SharedThis(this));
        }
        virtual FReply OnMouseMove(const FGeometry& G,const FPointerEvent& E) override
        {
            if((!Dragging&&!Resizing)||!HasMouseCapture())return FReply::Unhandled();
            auto& Action=Resizing?Resize:Move;
            if(CanManipulate&&!CanManipulate())
            {Action(FVector2D::ZeroVector,true);Dragging=Resizing=false;return FReply::Handled().ReleaseMouseCapture();}
            Action(G.AbsoluteToLocal(E.GetScreenSpacePosition())-G.AbsoluteToLocal(E.GetLastScreenSpacePosition()),false);
            return FReply::Handled();
        }
        virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent& E) override
        {
            if((!Dragging&&!Resizing)||E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
            auto& Action=Resizing?Resize:Move;Action(FVector2D::ZeroVector,true);Dragging=Resizing=false;return FReply::Handled().ReleaseMouseCapture();
        }
        virtual void OnMouseCaptureLost(const FCaptureLostEvent& E) override
        { if(Dragging)Move(FVector2D::ZeroVector,true);if(Resizing&&Resize)Resize(FVector2D::ZeroVector,true);Dragging=Resizing=false;SCompoundWidget::OnMouseCaptureLost(E); }
    private:
        TSharedPtr<FACEPluginStyle> Retail;
        TWeakObjectPtr<UACEPluginSubsystem> Host;FString Id;
        float LastOpacity=-1;bool LastHidden=false;
        bool ArtworkReady=false;
        bool Dragging=false,Resizing=false,Frameless=false;
        TFunction<void(FVector2D,bool)> Move,Resize;
        TFunction<void()> Raise;
        TAttribute<bool> Locked;
        TFunction<bool()> CanManipulate;
    };
}

void SACEPluginDesktop::Construct(const FArguments& Args)
{
    Host=Args._Host;ManagerOnly=Args._ManagerOnly;FACEPluginStyle::Use(Args._Host);
    ConstrainWindows=ACERuntimeOptions::Get(TEXT("ConstrainUIWindows"))>.5f;
    SetVisibility(EVisibility::SelfHitTestInvisible);
    SetClipping(EWidgetClipping::ClipToBounds);
    ChildSlot[SAssignNew(Canvas,SConstraintCanvas)];
    if(ManagerOnly){Toggle(TEXT("__manager"));return;}
    FWindow Bar;
    Bar.Position=Host->GetPluginWindowPosition(BarId,FVector2D(8,80));Bar.Size=FVector2D(38,120);Bar.PluginBar=true;
    Bar.Widget=SNew(SPluginFrame).Host(Host.Get()).WindowId(BarId)
        .Minimized_Lambda([H=Host](){return H.IsValid()&&H->IsPluginBarMinimized();})
        .ToggleMinimized([H=Host](){if(H.IsValid())H->SetPluginBarMinimized(!H->IsPluginBarMinimized());})
        .CanManipulate([H=Host](){return H.IsValid()&&H->IsPluginBarUnlocked();})
        .Move([this](FVector2D D,bool Save){Move(BarId,D,Save);})
        .Raise([this](){Raise(BarId);})
        [SAssignNew(BarScroll,SScrollBox).ScrollBarVisibility(EVisibility::Collapsed)
            +SScrollBox::Slot()[SAssignNew(Buttons,SStackBox).Orientation(Orient_Vertical)]];
    Canvas->AddSlot().Expose(Bar.Slot).Alignment(FVector2D::ZeroVector).ZOrder(10000)[Bar.Widget.ToSharedRef()];
    Windows.Add(BarId,Bar);
    Refresh();
}
FVector2D SACEPluginDesktop::DisplaySize(const FWindow& W) const
{
    if(Host.IsValid())
    {
        if(!W.LedgerId.IsEmpty())
        {
            auto Size=Host->LedgerSize(W.LedgerId);
            Size.X=FMath::Max(W.Size.X,W.LedgerId==TEXT("supplyledger")?88.:160.);
            if(W.Size.Y>0)
            {
                const double SavedHeight=W.Size.Y+Host->LedgerChromeHeight(W.LedgerId,Host->IsPluginWindowChromeHidden(W.LedgerId));
                // Hunt rows must fit even when restoring a height saved before a new metric was added.
                Size.Y=W.LedgerId==TEXT("huntledger")?FMath::Max(Size.Y,SavedHeight):SavedHeight;
            }
            return Size;
        }
        if(W.PluginBar&&Host->IsPluginBarMinimized())return FVector2D(Host->IsPluginBarHorizontal()?70:38,18);
        if(W.MapOverlay&&Host->WaypointOption(TEXT("map_minimized"),false))return FVector2D(70,26);
    }
    auto Size=W.Size;
    if(W.Id==TEXT("ucmmicro"))Size.X=FMath::Max(160.,Size.X);
    if(W.Id==TEXT("ucmmicro")&&Size.X<312&&Host.IsValid()&&Host->LedgerOption(TEXT("ucmmicro"),TEXT("condensed")))
        Size.Y=FMath::Max(220.,Size.Y*.5);
    if(W.Id==TEXT("actiondeck")&&Host.IsValid())
    {
        const auto* Deck=Host->GetGameInstance()->GetSubsystem<UACEActionDeckSubsystem>();
        const bool Hidden=Host->IsPluginWindowChromeHidden(W.Id);
        // Square rows, toolbar, frame padding and the resize grip.
        // Only an active macro adds the status footer.
        // The grip is outside the content area and must not cover the last row.
        Size.Y=(Size.X-6)*Deck->VisibleRows()/UACEActionDeckSubsystem::Columns+68+(Host->CanManipulatePluginWindow(W.Id)?24:0)
            +(Deck->IsConfigOpen()?300:0)-(Hidden?ACEActionDeckLayout::ToolbarHeight:0)
            +(Deck->IsMacroRunning()?ACEActionDeckLayout::StatusHeight:0);
    }
    if(!W.PluginBar&&!W.MapOverlay&&W.Id!=TEXT("waypoint.arrow")&&Host.IsValid()&&Host->IsPluginWindowChromeHidden(W.Id))
        Size.Y=FMath::Max(1.,Size.Y-ACELedger::ChromeHeaderHeight());
    return Size;
}
void SACEPluginDesktop::Layout(FWindow& W)
{
    const FVector2D Display=DisplaySize(W);
    const bool HiddenHeader=!W.PluginBar&&!W.MapOverlay&&W.Id!=TEXT("waypoint.arrow")&&Host.IsValid()&&Host->IsPluginWindowChromeHidden(W.Id);
    const bool LockedLedger=HiddenHeader&&!W.LedgerId.IsEmpty();
    const double HeaderOffset=HiddenHeader?ACELedger::ChromeHeaderHeight()
        +(W.Id==TEXT("actiondeck")?ACEActionDeckLayout::ToolbarHeight:0):0.;
    // Saved placement describes the expanded window. Remove the header above
    // the content, and clamp against that same footprint in both lock states.
    // Otherwise both collapsing the header and bottom-edge clamping move rows.
    FVector2D Expanded=Display;
    if(LockedLedger)Expanded.Y+=Host->LedgerChromeHeight(W.LedgerId,false)-Host->LedgerChromeHeight(W.LedgerId,true);
    else if(HiddenHeader)Expanded.Y+=HeaderOffset;
    if(!ConstrainWindows||ViewSize.X<=0||ViewSize.Y<=0)
    {W.Slot->SetOffset(FMargin(W.Position.X,W.Position.Y+HeaderOffset,Display.X,Display.Y));return;}
    const FVector2D Size(FMath::Min(Display.X,ViewSize.X),FMath::Min(Display.Y,FMath::Max(0.,ViewSize.Y-HeaderOffset)));
    // Fit the rendered window without overwriting the user's saved placement
    // during login, minimization or a temporary viewport size change.
    const FVector2D At(FMath::Clamp(W.Position.X,0.,FMath::Max(0.,ViewSize.X-Size.X)),
        FMath::Clamp(W.Position.Y,0.,FMath::Max(0.,ViewSize.Y-Expanded.Y))+HeaderOffset);
    W.Slot->SetOffset(FMargin(At.X,At.Y,Size.X,Size.Y));
}
void SACEPluginDesktop::Tick(const FGeometry& G,double Time,float Delta)
{
    SCompoundWidget::Tick(G,Time,Delta);
    const bool Constrain=ACERuntimeOptions::Get(TEXT("ConstrainUIWindows"))>.5f;
    const bool BoundsChanged=Constrain!=ConstrainWindows||!G.GetLocalSize().Equals(ViewSize);
    ConstrainWindows=Constrain;ViewSize=G.GetLocalSize();
    if(auto* Arrow=Windows.Find(TEXT("waypoint.arrow"));Arrow&&Arrow->Open&&Host.IsValid())
        Arrow->Widget->SetVisibility(Host->IsWaypointUnlocked()?EVisibility::Visible:EVisibility::HitTestInvisible);
    if(auto* Map=Windows.Find(TEXT("waypoint.dungeon"));Map&&Map->Open&&Host.IsValid())
    {
        const auto P=Host->WaypointPlayerPosition();
        Map->Widget->SetVisibility(!P.IsValid()?EVisibility::Collapsed:EVisibility::Visible);
        Layout(*Map); // Minimize without replacing the saved expanded size.
    }
    for(auto& Pair:Windows)
    {
        const bool Hidden=Host.IsValid()&&Host->IsPluginWindowChromeHidden(Pair.Key);
        if(BoundsChanged||Hidden!=Pair.Value.HiddenChrome||!Pair.Value.LedgerId.IsEmpty()||Pair.Key==TEXT("actiondeck")||Pair.Key==TEXT("ucmmicro"))Layout(Pair.Value);
        Pair.Value.HiddenChrome=Hidden;
    }
}
void SACEPluginDesktop::Move(const FString& Id,FVector2D Delta,bool Save)
{
    if(auto* W=Windows.Find(Id))
    {
        if(!Delta.IsNearlyZero()&&(!Host.IsValid()||!Host->CanManipulatePluginWindow(Id)))return;
        if(!Delta.IsNearlyZero())
        {
            const auto Offset=W->Slot->GetOffset();W->Position=FVector2D(Offset.Left,Offset.Top)+Delta;
            if(ConstrainWindows&&ViewSize.X>0&&ViewSize.Y>0)
            {const FVector2D Size=DisplaySize(*W);W->Position.X=FMath::Clamp(W->Position.X,0.,FMath::Max(0.,ViewSize.X-Size.X));W->Position.Y=FMath::Clamp(W->Position.Y,0.,FMath::Max(0.,ViewSize.Y-Size.Y));}
        }
        Layout(*W);if(Save&&Host.IsValid())Host->SavePluginWindowPosition(Id,W->Position);
    }
}
void SACEPluginDesktop::SaveOverlayLayout()
{
    if(!Host.IsValid())return;
    // A logout during a drag must save every plugin, including third-party UI.
    for(const auto& Pair:Windows)
    {
        Host->SavePluginWindowPosition(Pair.Key,Pair.Value.Position);
        if(!Pair.Value.PluginBar)Host->SavePluginWindowPosition(SizeKey(Pair.Key),Pair.Value.Size);
    }
}
void SACEPluginDesktop::Resize(const FString& Id,FVector2D Delta,bool Save)
{
    if(!Delta.IsNearlyZero()&&(!Host.IsValid()||!Host->CanManipulatePluginWindow(Id)))return;
    if(auto* W=Windows.Find(Id))
    {
     if(UACEPluginSubsystem::IsLedger(Id))
     {
         // Persist the list area; pinning only hides the editor chrome.
         const double Chrome=Host->LedgerChromeHeight(Id,Host->IsPluginWindowChromeHidden(Id));
         const FVector2D Size=DisplaySize(*W)+Delta;
         const double MinWidth=Id==TEXT("supplyledger")?88.:160.;
         W->Size=FVector2D(FMath::Clamp(Size.X,MinWidth,FMath::Max(MinWidth,ViewSize.X)),
             FMath::Clamp(Size.Y-Chrome,40.,FMath::Max(40.,ViewSize.Y-Chrome)));
         Layout(*W);if(Save)Host->SavePluginWindowPosition(SizeKey(Id),W->Size);return;
     }
     if(Id==TEXT("ucmmicro")&&W->Size.X<312&&Host.IsValid()&&Host->LedgerOption(Id,TEXT("condensed"))&&!FMath::IsNearlyZero(Delta.Y))
         Delta.Y=2*(FMath::Max(220.,W->Size.Y*.5)+Delta.Y)-W->Size.Y;
     W->Size+=Delta;const double MinWidth=Id==TEXT("actiondeck")?360.:Id==TEXT("itemtools")?380.:Id==TEXT("ucmmicro")?160.:96.,MinHeight=64.;W->Size.X=FMath::Clamp(W->Size.X,MinWidth,FMath::Max(MinWidth,ViewSize.X));W->Size.Y=FMath::Clamp(W->Size.Y,MinHeight,FMath::Max(MinHeight,ViewSize.Y));Layout(*W);
     if(Save&&Host.IsValid())Host->SavePluginWindowPosition(PanelSizeKey(Id),W->Size);}
}
void SACEPluginDesktop::Raise(const FString& Id)
{if(Id!=BarId)if(auto* W=Windows.Find(Id))W->Slot->SetZOrder(++TopOrder);}
bool SACEPluginDesktop::IsOpen(const FString& Id) const
{const auto* W=Windows.Find(Id);return W&&W->Open;}
bool SACEPluginDesktop::ContainsPoint(const FString& Id,FVector2D Absolute) const
{
    const auto* W=Windows.Find(Id);
    return W&&W->Open&&W->Widget&&W->Widget->GetCachedGeometry().IsUnderLocation(Absolute);
}
void SACEPluginDesktop::Hide(const FString& Id,bool Remember)
{
    if(auto* W=Windows.Find(Id))
    {
        const bool Focused=W->Widget->HasKeyboardFocus()||W->Widget->HasFocusedDescendants();
        if(Host.IsValid()&&Remember)Host->SavePluginWindowVisibility(Id,false);
        W->Open=false;W->Widget->SetVisibility(EVisibility::Collapsed);
        if(Focused)FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
}
void SACEPluginDesktop::Toggle(const FString& Id)
{
    if(!Host.IsValid())return;
    const bool Manager=Id==TEXT("__manager");
    TSharedPtr<FACEClientPlugin> Plugin;
    for(auto P:Host->Plugins)if((P->Id==Id || (Id.StartsWith(TEXT("waypoint."))&&P->Id==TEXT("waypoint")) || (Id==TEXT("ucm.log")&&P->Id==TEXT("ucm")))&&(P->Enabled||Id==TEXT("ucm.log")))Plugin=P;
    if(!Plugin&&!Manager)return;
    FACEPluginStyle::Use(Host.Get());
    if(auto* W=Windows.Find(Id))
    {
        if(W->Open)Hide(Id);
        else {Host->SavePluginWindowVisibility(Id,true);W->Open=true;W->Widget->SetVisibility(EVisibility::Visible);Raise(Id);}
        return;
    }
    FWindow W;W.Id=Id;W.Position=Host->GetPluginWindowPosition(Id,FVector2D(84+24*(Windows.Num()-1),80+24*(Windows.Num()-1)));W.Size=Host->GetPluginWindowPosition(PanelSizeKey(Id),FVector2D(780,700));
    const bool Arrow=Id==TEXT("waypoint.arrow");
    const bool Dungeon=Id==TEXT("waypoint.dungeon");
    W.MapOverlay=Dungeon;
    const bool Ledger=UACEPluginSubsystem::IsLedger(Id);
    if(Ledger){W.LedgerId=Id;W.Size=Host->GetPluginWindowPosition(SizeKey(Id),FVector2D(Id==TEXT("supplyledger")?200:290,0));}
    if(Id==TEXT("looteditor"))W.Size=Host->GetPluginWindowPosition(TEXT("looteditor.compact_size"),FVector2D(720,560));
    if(Id==TEXT("itemtools"))W.Size=Host->GetPluginWindowPosition(TEXT("itemtools.compact_size"),FVector2D(480,380));
    if(Id==TEXT("ucmmicro"))W.Size=Host->GetPluginWindowPosition(PanelSizeKey(Id),FVector2D(360,340));
    if(Id==TEXT("autocraft"))W.Size=Host->GetPluginWindowPosition(PanelSizeKey(Id),FVector2D(520,460));
    if(Id==TEXT("thirdeye"))W.Size=Host->GetPluginWindowPosition(PanelSizeKey(Id),FVector2D(520,480));
    if(Id==TEXT("actiondeck"))W.Size=Host->GetPluginWindowPosition(Id+TEXT(".size"),FVector2D(530,203));
    if(Id==TEXT("ucm.log"))W.Size=Host->GetPluginWindowPosition(PanelSizeKey(Id),FVector2D(620,360));
    if(Manager)W.Size=Host->GetPluginWindowPosition(Id+TEXT(".size"),FACEPluginStyle::IsCodex()?FVector2D(880,640):FVector2D(740,600));
    if(Arrow)W.Size=FVector2D(290,225);
    if(Dungeon)W.Size=Host->GetPluginWindowPosition(Id+TEXT(".size"),FVector2D(420,420));
    W.Widget=SNew(SPluginFrame).Host(Host.Get()).WindowId(Id).Title_Lambda([Id,Manager,Arrow,Plugin](){return Manager?TEXT("Plugin Manager"):Id==TEXT("ucm.log")?TEXT("UCM Log"):Arrow?TEXT("Waypoint · drag when unlocked"):Id==TEXT("waypoint.map")?TEXT("Waypoint Map"):Plugin->DisplayTitle();})
        .Frameless(Arrow||Dungeon)
        .Icon(WindowIcon(Host.Get(),Id))
        .Subtitle_Lambda([H=Host,Manager,Plugin,Ledger]()
        {
            if(Manager&&H.IsValid())return FString::Printf(TEXT("%d installed"),H->Plugins.Num());
            return Plugin&&!Ledger&&!Plugin->Version.IsEmpty()?TEXT("Version ")+Plugin->Version:FString();
        })
        .CompactWhenLocked(Ledger)
        .Locked_Lambda([H=Host,Id]{return H.IsValid()&&H->IsPluginWindowChromeHidden(Id);})
        .CanManipulate([H=Host,Id]{return H.IsValid()&&H->CanManipulatePluginWindow(Id);})
        .Move([this,Id](FVector2D D,bool Save){Move(Id,D,Save);})
        .Resize(Arrow?TFunction<void(FVector2D,bool)>():TFunction<void(FVector2D,bool)>([this,Id](FVector2D D,bool Save){Resize(Id,D,Save);}))
        .Raise([this,Id](){Raise(Id);}).Close([this,Id,Manager,Arrow](){if(Manager&&ManagerOnly){Host->TogglePanel();return;}Hide(Id);if(Arrow)Host->SetWaypointOption(TEXT("arrow"),false);})[Manager?Host->MakePanel():Dungeon?Host->MakeWaypointDungeonOverlay():Arrow?Host->MakeWaypointPanel(false,true):Host->MakePanel(Id)];
    if(Dungeon)W.Widget->SetVisibility(EVisibility::Collapsed); // Tick validates the current cell before showing it.
    if(Arrow)W.Widget->SetVisibility(TAttribute<EVisibility>::CreateLambda([H=Host](){return H.IsValid()&&H->WaypointOption(TEXT("arrow"))?(H->IsWaypointUnlocked()?EVisibility::Visible:EVisibility::HitTestInvisible):EVisibility::Collapsed;}));
    Canvas->AddSlot().Expose(W.Slot).Alignment(FVector2D::ZeroVector).ZOrder(++TopOrder)[W.Widget.ToSharedRef()];Layout(W);Windows.Add(Id,W);
    Host->SavePluginWindowVisibility(Id,true);
}
void SACEPluginDesktop::Refresh()
{
    if(!Host.IsValid()||ManagerOnly)return;
    FACEPluginStyle::Use(Host.Get());
    for(const auto& P:Host->Plugins)if(P->Enabled&&Host->WasPluginWindowOpen(P->Id)&&!Windows.Contains(P->Id))Toggle(P->Id);
    Windows.FindChecked(BarId).Widget->SetVisibility(Host->IsPluginBarVisible()?EVisibility::Visible:EVisibility::Collapsed);
    Layout(Windows.FindChecked(BarId));
    const bool Horizontal=Host->IsPluginBarHorizontal();
    FString Next=(Horizontal?FString(TEXT("horizontal|")):FString(TEXT("vertical|")))+FString::FromInt(Host->GetPluginOrderRevision())+TEXT("|");TSet<FString> Enabled;
    for(auto P:Host->Plugins)if(P->Enabled){Next+=P->Id+(Host->IsPluginOnBar(P->Id)?TEXT("|"):TEXT("~|"));Enabled.Add(P->Id);}
    Next+=FACEPluginStyle::ThemeName();
    if(Host->Plugins.ContainsByPredicate([](const auto& P){return P->Id==TEXT("ucm");}))Enabled.Add(TEXT("ucm.log"));
    for(const TCHAR* Id:{TEXT("ucm.log"),TEXT("waypoint.map")})
        if((Enabled.Contains(Id)||(FString(Id)==TEXT("waypoint.map")&&Enabled.Contains(TEXT("waypoint"))))
            &&Host->WasPluginWindowOpen(Id)&&!Windows.Contains(Id))Toggle(Id);
    if(Enabled.Contains(TEXT("waypoint")))
    {
        Enabled.Add(TEXT("waypoint.map"));Enabled.Add(TEXT("waypoint.arrow"));Enabled.Add(TEXT("waypoint.dungeon"));
        const bool Show=Host->WaypointOption(TEXT("arrow"));
        if(Show && !IsOpen(TEXT("waypoint.arrow")))Toggle(TEXT("waypoint.arrow"));
        else if(!Show && IsOpen(TEXT("waypoint.arrow")))Hide(TEXT("waypoint.arrow"));
        const bool Pin=Host->WaypointOption(TEXT("dungeon_overlay"));
        if(Pin&&!IsOpen(TEXT("waypoint.dungeon")))Toggle(TEXT("waypoint.dungeon"));
        else if(!Pin&&IsOpen(TEXT("waypoint.dungeon")))Hide(TEXT("waypoint.dungeon"));
    }
    if(Next==Signature&&Buttons->NumSlots())return;
    Signature=Next;
    for(auto It=Windows.CreateIterator();It;++It)if(It.Key()!=BarId&&!Enabled.Contains(It.Key()))
    {Hide(It.Key(),false);Canvas->RemoveSlot(It.Value().Widget.ToSharedRef());It.RemoveCurrent();}
    Buttons->ClearChildren();
    Buttons->SetOrientation(Horizontal?Orient_Horizontal:Orient_Vertical);
    BarScroll->SetOrientation(Horizontal?Orient_Horizontal:Orient_Vertical);
    BarScroll->ScrollToStart();
    for(const FString& Id:Host->PluginDisplayOrder())
    {
        TSharedPtr<FACEClientPlugin> P;
        for(const auto& Candidate:Host->Plugins)if(Candidate->Id==Id)P=Candidate;
        const bool Manager=Id==TEXT("__manager");
        if(!Manager&&!(P&&P->Enabled)&&!(Id==TEXT("ucm.log")&&Enabled.Contains(TEXT("ucm")))
            &&!(Id==TEXT("waypoint.map")&&Enabled.Contains(TEXT("waypoint"))))continue;
        // "Show on plugin bar" (Plugin Manager) hides an enabled plugin's button only.
        if(!Manager&&!Host->IsPluginOnBar(Id.StartsWith(TEXT("waypoint."))?FString(TEXT("waypoint")):Id==TEXT("ucm.log")?FString(TEXT("ucm")):Id))continue;
        auto Icon=WindowIcon(Host.Get(),Id);
        Buttons->AddSlot().AutoSize().Padding(1)[SNew(SACEPluginOrderTarget).Host(Host.Get()).Id(Id).LockId(BarId).Horizontal(Horizontal)
            .Tag(FName(*(TEXT("PluginBarOrder.")+Id)))
            [SNew(SOverlay)+SOverlay::Slot()[SNew(SBox).WidthOverride(32).HeightOverride(32)
            [SNew(SACEPluginButton).ContentPadding(FMargin(FACEPluginStyle::IconButtonPadding)).Tag(Manager?FName(TEXT("PluginOptions")):FName(*(TEXT("PluginBarButton.")+Id)))
                .Cursor(EMouseCursor::Default).ButtonStyle(FACEPluginStyle::IsCodex()?&SPluginFrame::TitleControlStyle():&DockButtonStyle()).IsFocusable(false).AllowDragDrop(true)
                .OnSlateButtonDragDetected_Lambda([H=Host,Id](const FGeometry&,const FPointerEvent&){return ACEPluginOrder::BeginDrag(H.Get(),Id,BarId);})
                .ToolTip(FACEPluginStyle::Tooltip(TAttribute<FText>::CreateLambda([H=Host,P,Id,Manager]()
                {
                    FString Tip=Manager?TEXT("Plugin Manager"):P?P->DisplayTitle():Id==TEXT("ucm.log")?TEXT("UCM activity and problem log"):TEXT("Waypoint world / dungeon map");
                    if(P&&P->Id==TEXT("looteditor"))
                    {
                        bool Looting=false;for(const auto& Other:H->Plugins)if(Other->Id==TEXT("ucm"))Other->Profile->TryGetBoolField(TEXT("looting"),Looting);
                        Tip+=TEXT("\nClick to open/close the editor. This does not toggle looting.\nUCM looting: ")+FString(Looting?TEXT("enabled"):TEXT("disabled"));
                    }
                    else if(P)Tip+=TEXT("\nClick to show/hide\n")+P->Status;
                    return FText::FromString(Tip+TEXT("\nDrag to reorder while unlocked. Hold Quick-show if pinned."));
                })))
                .OnClicked_Lambda([this,Id,Manager](){if(Manager)Host->TogglePanel();else Toggle(Id);return FReply::Handled();})[Icon]]]
            // Codex: a 2px gold underline marks windows that are open.
            +SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Center)[SNew(SBox).WidthOverride(18).HeightOverride(2)
                .Visibility_Lambda([this,Id,Manager]{return FACEPluginStyle::IsCodex()&&(Manager?Host.IsValid()&&Host->IsManagerOpen():IsOpen(Id))?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
                [SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush")).ColorAndOpacity(FLinearColor(.65f,.51f,.28f))]]]];
    }
    auto& Bar=Windows.FindChecked(BarId);
    Bar.Size=Horizontal?FVector2D(4+34*Buttons->NumSlots(),52):FVector2D(38,18+34*Buttons->NumSlots());
    Layout(Bar);
}

void UACEPluginSubsystem::UpdateDesktopDock()
{
    auto* GI=GetGameInstance();auto* V=GI->GetGameViewportClient();
    auto* PC=Cast<AACEPlayerController>(GI->GetFirstLocalPlayerController());
    auto* C=GI->GetSubsystem<UACEClientSubsystem>();
    if(!V||!PC||!C||C->GetSessionState()!=EACESessionState::InWorld||PC->IsVRActive())
    {RemoveDesktopDock();return;}
    if(!DesktopDock)
    {
        DesktopDock=SNew(SACEPluginDesktop).Host(this);
        DesktopDock->SetVisibility(TAttribute<EVisibility>::CreateLambda([Weak=TWeakObjectPtr<AACEPlayerController>(PC)]()
            {return Weak.IsValid()&&!Weak->IsDesktopInterfaceHidden()?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed;}));
        V->AddViewportWidgetContent(DesktopDock.ToSharedRef(),1500);
    }
    DesktopDock->Refresh();
    if(!DesktopPanel&&WasPluginWindowOpen(TEXT("__manager")))TogglePanel();
}
void UACEPluginSubsystem::RemoveDesktopDock()
{
    if(DesktopDock)DesktopDock->SaveOverlayLayout();
    if(DesktopDock)if(auto* V=GetGameInstance()->GetGameViewportClient())V->RemoveViewportWidgetContent(DesktopDock.ToSharedRef());
    DesktopDock.Reset();
}
void UACEPluginSubsystem::TogglePluginWindow(const FString& Id)
{UpdateDesktopDock();if(DesktopDock)DesktopDock->Toggle(Id);}
bool UACEPluginSubsystem::IsPluginWindowOpen(const FString& Id) const
{return DesktopDock&&DesktopDock->IsOpen(Id);}
bool UACEPluginSubsystem::IsPointInsidePluginWindow(const FString& Id,FVector2D Absolute) const
{return DesktopDock&&DesktopDock->ContainsPoint(Id,Absolute);}
bool UACEPluginSubsystem::WasPluginWindowOpen(const FString& Id) const
{
    // Preserve ledger preferences from versions before ordinary windows saved
    // visibility. Arrow/pinned-map visibility remains owned by waypoint options.
    if(IsLedger(Id))return LedgerOption(Id,TEXT("visible"));
    const TSharedPtr<FJsonObject>* Visible=nullptr;bool Open=false;
    if(Settings&&Settings->TryGetObjectField(TEXT("_window_visibility"),Visible))(*Visible)->TryGetBoolField(Id,Open);
    return Open;
}
void UACEPluginSubsystem::SavePluginWindowVisibility(const FString& Id,bool Open)
{
    if(!Settings)return;
    if(IsLedger(Id)){SetLedgerOption(Id,TEXT("visible"),Open);return;}
    const TSharedPtr<FJsonObject>* Existing=nullptr;
    auto Visible=Settings->TryGetObjectField(TEXT("_window_visibility"),Existing)?*Existing:MakeShared<FJsonObject>();
    bool Previous=false;if(Visible->TryGetBoolField(Id,Previous)&&Previous==Open)return;
    Visible->SetBoolField(Id,Open);Settings->SetObjectField(TEXT("_window_visibility"),Visible);SaveSettings();
}
bool UACEPluginSubsystem::IsPluginBarVisible() const
{
    bool Visible=true;if(Settings)Settings->TryGetBoolField(TEXT("_show_plugin_bar"),Visible);return Visible;
}
void UACEPluginSubsystem::SetPluginBarVisible(bool Visible)
{
    if(!Settings)return;
    Settings->SetBoolField(TEXT("_show_plugin_bar"),Visible);SaveSettings();
    if(DesktopDock)DesktopDock->Refresh();
}
bool UACEPluginSubsystem::IsPluginBarMinimized() const
{
    bool Minimized=false;if(Settings)Settings->TryGetBoolField(TEXT("_plugin_bar_minimized"),Minimized);return Minimized;
}
void UACEPluginSubsystem::SetPluginBarMinimized(bool Minimized)
{
    if(!Settings)return;
    Settings->SetBoolField(TEXT("_plugin_bar_minimized"),Minimized);SaveSettings();
    if(DesktopDock)DesktopDock->Refresh();
}
bool UACEPluginSubsystem::IsPluginBarHorizontal() const
{
    bool Horizontal=false;if(Settings)Settings->TryGetBoolField(TEXT("_plugin_bar_horizontal"),Horizontal);return Horizontal;
}
void UACEPluginSubsystem::SetPluginBarHorizontal(bool Horizontal)
{
    if(!Settings)return;
    Settings->SetBoolField(TEXT("_plugin_bar_horizontal"),Horizontal);SaveSettings();
    if(DesktopDock)DesktopDock->Refresh();
}
bool UACEPluginSubsystem::IsPluginBarUnlocked() const
{
    const auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const auto* Manager=Client?Client->GetUIElementManager():nullptr;
    return !Manager||!Manager->IsUiLocked();
}
FVector2D UACEPluginSubsystem::GetPluginWindowPosition(const FString& Id,const FVector2D& Default) const
{
    const TSharedPtr<FJsonObject>* Positions=nullptr;
    const TSharedPtr<FJsonObject>* Point=nullptr;double X,Y;
    if(Settings&&Settings->TryGetObjectField(TEXT("_window_positions"),Positions)&&(*Positions)->TryGetObjectField(Id,Point)
        &&(*Point)->TryGetNumberField(TEXT("x"),X)&&(*Point)->TryGetNumberField(TEXT("y"),Y)&&FMath::IsFinite(X)&&FMath::IsFinite(Y))return FVector2D(X,Y);
    return Default;
}
void UACEPluginSubsystem::SavePluginWindowPosition(const FString& Id,const FVector2D& Position)
{
    if(!Settings||!FMath::IsFinite(Position.X)||!FMath::IsFinite(Position.Y))return;
    const TSharedPtr<FJsonObject>* Existing=nullptr;
    auto Positions=Settings->TryGetObjectField(TEXT("_window_positions"),Existing)?*Existing:MakeShared<FJsonObject>();
    auto Point=MakeShared<FJsonObject>();Point->SetNumberField(TEXT("x"),Position.X);Point->SetNumberField(TEXT("y"),Position.Y);
    Positions->SetObjectField(Id,Point);Settings->SetObjectField(TEXT("_window_positions"),Positions);SaveSettings();
}
