#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACELedger.h"
#include "ACEClientSubsystem.h"
#include "ACEHoverTooltipWidget.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatTextureResolver.h"
#include "UI/ACEUIFontStyles.h"
#include "UI/ACEUIResourceResolver.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Layout/SBox.h"
#include "ACELobbyArt.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
    TWeakPtr<FACEPluginStyle> CurrentStyle;
    FString GTheme=TEXT("codex");
    bool GMatchWorld=true;
}
void FACEPluginStyle::SetTheme(const FString& Name){GTheme=Name.Equals(TEXT("original"),ESearchCase::IgnoreCase)?TEXT("original"):TEXT("codex");}
const FString& FACEPluginStyle::ThemeName(){return GTheme;}
bool FACEPluginStyle::IsCodex(){return GTheme!=TEXT("original");}
void FACEPluginStyle::SetMatchWorldColor(bool bMatch){GMatchWorld=bMatch;}
bool FACEPluginStyle::MatchWorldColor(){return GMatchWorld;}
int32 FACEPluginStyle::WindowTint(){return GMatchWorld?ACELobbyArt::WorldTint():0;}
double FACEPluginStyle::HeaderHeight(){return IsCodex()?42.:24.;}
FLinearColor FACEPluginStyle::Gold(float Alpha){FLinearColor C(FColor(242,169,51));C.A=Alpha;return C;}
FLinearColor FACEPluginStyle::Cream(){return FLinearColor(FColor(239,227,196));}
FLinearColor FACEPluginStyle::Muted(){return FLinearColor(FColor(191,174,136));}
double ACELedger::ChromeHeaderHeight(){return FACEPluginStyle::HeaderHeight();}
namespace
{
    class SACEPluginTooltip : public SToolTip, public FGCObject
    {
    public:
        SLATE_BEGIN_ARGS(SACEPluginTooltip) {} SLATE_ATTRIBUTE(FText,Text)
            SLATE_ARGUMENT(UACEUIResourceResolver*,Resources)
        SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        {
            Text=Args._Text;Resources=Args._Resources;
            SToolTip::Construct(SToolTip::FArguments().TextMargin(0).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))[SNew(SBox)]);
        }
        virtual void AddReferencedObjects(FReferenceCollector& Collector) override
        {Collector.AddReferencedObject(Resources);Collector.AddReferencedObject(Tip);}
        virtual FString GetReferencerName() const override {return TEXT("SACEPluginTooltip");}
        virtual bool IsEmpty() const override {return Text.Get().IsEmpty();}
        virtual void OnOpening() override {Refresh(true);}
        virtual TSharedRef<SWidget> AsWidget() override {Refresh();return SToolTip::AsWidget();}
        virtual TSharedRef<SWidget> GetContentWidget() override {Refresh();return SToolTip::GetContentWidget();}
        virtual void Tick(const FGeometry& G,double Time,float Delta) override
        {SToolTip::Tick(G,Time,Delta);Refresh();}
    private:
        TAttribute<FText> Text;
        TObjectPtr<UACEUIResourceResolver> Resources=nullptr;
        TObjectPtr<UACEHoverTooltipWidget> Tip=nullptr;
        void Refresh(bool Opening=false)
        {
            const FText Value=Text.Get();
            if(!Tip)
            {
                if(Value.IsEmpty())return;
                Tip=NewObject<UACEHoverTooltipWidget>();Tip->Initialize();Tip->SetResources(Resources);
                Tip->SetTooltipText(Value.ToString(),UACEHoverTooltipWidget::ObjectTemplate);
                SetContentWidget(Tip->TakeWidget());
            }
            else
            {
                // DAT resources may have finished loading since the last hover.
                if(Opening)Tip->SetResources(Resources);
                Tip->SetTooltipText(Value.ToString(),UACEHoverTooltipWidget::ObjectTemplate);
            }
        }
    };
}
TSharedRef<IToolTip> FACEPluginStyle::Tooltip(TAttribute<FText> Text)
{
    return SNew(SACEPluginTooltip).Text(Text).Resources(Current()->Resolver);
}
TSharedRef<FACEPluginStyle> FACEPluginStyle::Use(UACEPluginSubsystem* Host,const FString& WindowId)
{
    auto& Style=WindowId.IsEmpty()?Host->RetailPluginStyle:Host->WindowPluginStyles.FindOrAdd(WindowId);
    if(!Style)
    {
        auto* C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
        Style=MakeShared<FACEPluginStyle>(C?C->GetUIResourceResolver():nullptr);
    }
    Style->Refresh();
    if(!WindowId.IsEmpty())Style->SetWindowAppearance(Host->GetPluginWindowOpacity(WindowId),Host->IsPluginWindowChromeHidden(WindowId));
    CurrentStyle=Style;return Style.ToSharedRef();
}
TSharedRef<FACEPluginStyle> FACEPluginStyle::Current()
{
    if(auto S=CurrentStyle.Pin())return S.ToSharedRef();
    static TSharedRef<FACEPluginStyle> Fallback=MakeShared<FACEPluginStyle>(nullptr);return Fallback;
}
FSlateFontInfo FACEPluginStyle::NativeFont(FName Typeface,int32 Size)
{
    // Codex maps requested sizes onto its own type scale in SACEPluginText; keep them distinct here.
    if(IsCodex())return FCoreStyle::GetDefaultFontStyle(Typeface,Size);
    const int32 ConsistentSize=Size==13?12:Size>=14&&Size<=16?15:Size>=17&&Size<=20?18:Size;
    return FCoreStyle::GetDefaultFontStyle(Typeface,ConsistentSize);
}
void FACEPluginStyle::AddReferencedObjects(FReferenceCollector& C)
{C.AddReferencedObject(Resolver);C.AddReferencedObject(Atlas);for(auto& Pair:Fonts)C.AddReferencedObject(Pair.Value->Atlas);for(auto& Texture:ButtonTextures)C.AddReferencedObject(Texture);}
const FSlateBrush* FACEPluginStyle::Image(uint32 Id)
{
    auto& B=Images.FindOrAdd(Id);if(!B)B=MakeShared<FSlateBrush>();
    if(B->GetResourceObject())return B.Get();
    B->DrawAs=ESlateBrushDrawType::Image;
    if(Resolver)if(auto* T=Resolver->ResolveTexture(Id)){B->SetResourceObject(T);B->ImageSize=FVector2D(T->GetSizeX(),T->GetSizeY());}
    return B.Get();
}
const FSlateBrush* FACEPluginStyle::SpellImage(uint32 SpellId)
{
    auto& B=ItemImages.FindOrAdd(FString::Printf(TEXT("spell.%u"),SpellId));
    if(!B)B=MakeShared<FSlateBrush>();
    if(!B->GetResourceObject()&&Resolver)B->SetResourceObject(Resolver->ResolveSpellIcon(SpellId));
    B->DrawAs=B->GetResourceObject()?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType;
    B->ImageSize=FVector2D(32,32);
    return B.Get();
}
const FSlateBrush* FACEPluginStyle::ItemImage(const FACESupplyEntry& Item,bool Foreground)
{
    const FString Key=Foreground?FString::Printf(TEXT("fg.%d.%d.%d"),Item.Icon,Item.Overlay,Item.Effects)
        :FString::Printf(TEXT("bg.%d.%d"),Item.ItemType,Item.Underlay);
    auto& B=ItemImages.FindOrAdd(Key);if(!B)B=MakeShared<FSlateBrush>();
    if(!B->GetResourceObject()&&Resolver)
    {
        B->SetResourceObject(Foreground?Resolver->ResolveItemForeground(Item.Icon,Item.Overlay,Item.Effects)
            :Resolver->ResolveItemBackground(Item.ItemType,Item.Underlay));
        B->DrawAs=ESlateBrushDrawType::Image;B->ImageSize=FVector2D(32,32);
    }
    return B.Get();
}
FACEPluginStyle::FACEPluginStyle(UACEUIResourceResolver* InResolver):Resolver(InResolver) { InitializeArtwork(); }
void FACEPluginStyle::Refresh()
{
    // A font atlas is not proof that checkbox/slider images have resolved.
    // The login manager can be constructed before asynchronous DAT loading ends.
    if(!DatArtworkLoaded&&Resolver&&Resolver->GetDatSubsystem()&&Resolver->GetDatSubsystem()->IsDatReady())InitializeArtwork();
}
void FACEPluginStyle::InitializeArtwork()
{
    DatArtworkLoaded=Resolver&&Resolver->GetDatSubsystem()&&Resolver->GetDatSubsystem()->IsDatReady();
    const FLinearColor Gold(.65f,.43f,.15f),Ivory(1.f,.98f,.92f);
    Frame=FSlateRoundedBoxBrush(FLinearColor(.008f,.006f,.003f,.96f),0.f,Gold,2.f);
    const FLinearColor TitleColor(.025f,.012f,.004f,1);
    TitleBackground=FSlateColorBrush(TitleColor);
    SectionFrame=FSlateRoundedBoxBrush(TitleColor,0.f,Gold,2.f);
    SectionHeader=FSlateColorBrush(FLinearColor(Gold.R,Gold.G,Gold.B,.25f));
    Background=*Image(0x06004CC2);
    if(!Background.GetResourceObject())Background=FSlateRoundedBoxBrush(FLinearColor(.005f,.004f,.002f,.96f),0.f);
    Button=FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
    Button.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.19f,.002f,.001f),0.f,Gold,2.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.29f,.006f,.002f),0.f,FLinearColor(1,.75f,.25f),2.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.075f,.001f,0),0.f,Gold,2.f))
        .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.035f,.02f,.01f),0.f,FLinearColor(.25f,.18f,.08f),1.f))
        .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0))
        .SetNormalForeground(Ivory).SetHoveredForeground(FLinearColor(1,.85f,.35f)).SetPressedForeground(Ivory);
    // Preserve the authored bevel while stretching the center.
    auto Beveled=[this](uint32 Id)
    {
        auto B=*Image(Id);
        B.DrawAs=ESlateBrushDrawType::Box;
        B.Margin=FMargin(2.f/FMath::Max(1.f,float(B.ImageSize.X)),2.f/FMath::Max(1.f,float(B.ImageSize.Y)));
        return B;
    };
    // Button_RedBar (layout 0x21000040) is retail's resizable action button.
    // Join its three strips once, then let Slate preserve the gold bevel.
    auto RedBar=[this](uint32 Left,uint32 Middle,uint32 Right,FSlateBrush& Out)
    {
        auto* Dat=Resolver?Resolver->GetDatSubsystem():nullptr;
        auto* Textures=Dat?Dat->GetTextureResolver():nullptr;
        if(!Textures)return false;
        FACEDatDecodedSurface Parts[3];const uint32 Ids[]={Left,Middle,Right};
        int32 Width=0,Height=0;
        for(int32 I=0;I<3;++I)
        {
            FACEDatTexture Raw;
            if(!Textures->LoadTextureForUi(Ids[I],Raw)||!Textures->DecodeTextureForUi(Raw,Parts[I])||!Parts[I].bHasPixels)return false;
            if(I&&Height!=Parts[I].Height)return false;
            Width+=Parts[I].Width;Height=Parts[I].Height;
        }
        TArray<FColor> Pixels;Pixels.SetNumUninitialized(Width*Height);int32 Offset=0;
        for(const auto& Part:Parts)
        {
            for(int32 Y=0;Y<Height;++Y)
                FMemory::Memcpy(Pixels.GetData()+Y*Width+Offset,Part.Pixels.GetData()+Y*Part.Width,Part.Width*sizeof(FColor));
            Offset+=Part.Width;
        }
        auto* Texture=FACEDatTextureResolver::CreateTransientRgbaUi(Width,Height,Pixels);
        if(!Texture)return false;
        ButtonTextures.Add(Texture);Out.SetResourceObject(Texture);Out.ImageSize=FVector2D(Width,Height);
        Out.DrawAs=ESlateBrushDrawType::Box;Out.Margin=FMargin(4.f/Width,4.f/Height);return true;
    };
    FSlateBrush Up,Down,Disabled;
    if(RedBar(0x06004C96,0x06004C99,0x06004C9C,Up)&&RedBar(0x06004C95,0x06004C98,0x06004C9B,Down)
        &&RedBar(0x06004C94,0x06004C97,0x06004C9A,Disabled))
        Button.SetNormal(Up).SetHovered(Up).SetPressed(Down).SetDisabled(Disabled);
    DockButton=Button;
    // Small frame/icon controls cannot fit the action button's four-pixel
    // bevel. Keep their one-pixel border in every interaction state.
    DockButton.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.008f,.005f,.002f,.9f),0.f,FLinearColor(.4f,.26f,.09f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.055f,.039f,.018f),0.f,FLinearColor(.6f,.43f,.2f),1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.025f,.016f,.007f),0.f,FLinearColor(.9f,.66f,.25f),1.f))
        .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.008f,.005f,.002f,.6f),0.f,FLinearColor(.25f,.18f,.08f),1.f));
    Tab=Button;
    Tab.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.012f,.01f,.007f),0.f,FLinearColor(.24f,.18f,.1f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.055f,.039f,.018f),0.f,FLinearColor(.6f,.43f,.2f),1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.16f,.095f,.025f),0.f,FLinearColor(.9f,.66f,.25f),1.f))
        .SetPressedPadding(FMargin(0));
    Check=FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");
    auto Small=[this](uint32 Id){auto B=*Image(Id);B.ImageSize=FVector2D(14,14);return B;};
    // Keep Slate's distinct states until each retail image is available. Empty
    // image brushes paint identical squares for both enabled and disabled states.
    const auto Unchecked=Small(0x0600191F),Checked=Small(0x0600191A);
    if(Unchecked.GetResourceObject())Check.SetUncheckedImage(Unchecked).SetUncheckedHoveredImage(Unchecked).SetUncheckedPressedImage(Unchecked);
    if(Checked.GetResourceObject())Check.SetCheckedImage(Checked).SetCheckedHoveredImage(Checked).SetCheckedPressedImage(Checked);
    auto Handle=*Image(0x06001286);Handle.ImageSize=FVector2D(12,20);
    Slider=FCoreStyle::Get().GetWidgetStyle<FSliderStyle>("Slider");Slider.SetBarThickness(4);
    if(const auto* Bar=Image(0x06001285);Bar->GetResourceObject())Slider.SetNormalBarImage(*Bar).SetHoveredBarImage(*Bar);
    if(Handle.GetResourceObject())Slider.SetNormalThumbImage(Handle).SetHoveredThumbImage(Handle);
    Edit=FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
    FSlateRoundedBoxBrush Well(FLinearColor(.012f,.008f,.004f),0.f,Gold,1.f);
    Edit.SetBackgroundImageNormal(Well).SetBackgroundImageHovered(Well).SetBackgroundImageFocused(Well).SetBackgroundImageReadOnly(Well).SetForegroundColor(Ivory);
    // Config's green selector and row highlight are distinct from red actions.
    auto Green=Beveled(0x060012B3),SelectedGreen=Beveled(0x060012B4);
    if(!Green.GetResourceObject())Green=FSlateRoundedBoxBrush(FLinearColor(.015f,.065f,.012f),0.f,Gold,1.f);
    if(!SelectedGreen.GetResourceObject())SelectedGreen=FSlateRoundedBoxBrush(FLinearColor(.04f,.14f,.025f),0.f,Gold,1.f);
    MenuItem=Button;
    MenuItem.SetNormal(Green).SetHovered(SelectedGreen).SetPressed(SelectedGreen)
        .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
    Combo=FCoreStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton");
    Combo.SetButtonStyle(MenuItem).SetContentPadding(FMargin(ButtonPadding)).SetMenuBorderBrush(Green).SetMenuBorderPadding(FMargin(2));
    if(const auto* Arrow=Image(0x060012B1);Arrow->GetResourceObject())
        Combo.SetDownArrowImage(*Arrow).SetDownArrowPadding(FMargin(6,0,0,0));
    ComboBox=FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>("ComboBox");
    ComboBox.SetComboButtonStyle(Combo).SetContentPadding(FMargin(ButtonPadding));
    ComboRow=FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
    ComboRow.SetEvenRowBackgroundBrush(Green).SetOddRowBackgroundBrush(Green)
        .SetEvenRowBackgroundHoveredBrush(SelectedGreen).SetOddRowBackgroundHoveredBrush(SelectedGreen)
        .SetActiveBrush(SelectedGreen).SetActiveHoveredBrush(SelectedGreen)
        .SetInactiveBrush(SelectedGreen).SetInactiveHoveredBrush(SelectedGreen)
        .SetSelectorFocusedBrush(SelectedGreen).SetTextColor(Ivory).SetSelectedTextColor(FLinearColor::White);
    Spin=FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox");Spin.SetBackgroundBrush(Well).SetHoveredBackgroundBrush(Well)
        .SetActiveFillBrush(Well).SetInactiveFillBrush(Well).SetForegroundColor(Ivory);
    RailItem=Tab;QuietRow=Tab;QuietRowSelected=Tab;Switch=Check;
    Codex=FACEPluginStyle::IsCodex();
    if(Codex)InitializeCodex();
    if(Resolver&&Resolver->ResolveFont(0x40000000,Font))Atlas=Resolver->ResolveFontAtlas(Font.ForegroundSurfaceDataID,TintAtlas);
    OriginalFrame=Frame;OriginalBackground=Background;OriginalTitle=TitleBackground;
    OriginalSectionFrame=SectionFrame;OriginalSectionHeader=SectionHeader;
    const float Previous=BackgroundOpacity;BackgroundOpacity=-1;
    if(Previous>=0)SetWindowAppearance(Previous,HideFrame);
}
namespace
{
    /** Anti-aliased procedural pixels for the Codex controls (drawn at 2x, displayed at half size). */
    FLinearColor Over(const FLinearColor& Dst,const FLinearColor& Src,float Coverage)
    {
        const float A=FMath::Clamp(Src.A*Coverage,0.f,1.f);if(A<=0)return Dst;
        const float OutA=A+Dst.A*(1-A);
        auto Mix=[&](float S,float T){return OutA>0?(S*A+T*Dst.A*(1-A))/OutA:0.f;};
        return FLinearColor(Mix(Src.R,Dst.R),Mix(Src.G,Dst.G),Mix(Src.B,Dst.B),OutA);
    }
    float Edge(float Distance){return FMath::Clamp(.5f-Distance,0.f,1.f);}
    float RoundRectDistance(float X,float Y,float W,float H,float R)
    {
        const float QX=FMath::Abs(X-W*.5f)-(W*.5f-R),QY=FMath::Abs(Y-H*.5f)-(H*.5f-R);
        return FMath::Sqrt(FMath::Square(FMath::Max(QX,0.f))+FMath::Square(FMath::Max(QY,0.f)))+FMath::Min(FMath::Max(QX,QY),0.f)-R;
    }
    float SegmentDistance(FVector2f P,FVector2f A,FVector2f B)
    {
        const FVector2f AB=B-A;const float T=FMath::Clamp(FVector2f::DotProduct(P-A,AB)/AB.SizeSquared(),0.f,1.f);
        return (P-(A+AB*T)).Size();
    }
    // Lobby palette (srgb bytes) converted once.
    FLinearColor Lin(uint8 R,uint8 G,uint8 B,float A=1.f){FLinearColor C(FColor(R,G,B));C.A=A;return C;}
}
void FACEPluginStyle::InitializeCodex()
{
    const FLinearColor GoldC=Gold(),Navy=Lin(6,13,30,.92f),NavyDeep=Lin(4,9,22,.96f),Red=Lin(150,28,18),RedHot=Lin(178,40,24);
    auto Texture=[this](int32 W,int32 H,TFunctionRef<FLinearColor(float,float)> Pixel,FVector2D Display)
    {
        TArray<FColor> Pixels;Pixels.SetNumUninitialized(W*H);
        for(int32 Y=0;Y<H;++Y)for(int32 X=0;X<W;++X)Pixels[Y*W+X]=Pixel(X+.5f,Y+.5f).ToFColor(true);
        FSlateBrush B;B.DrawAs=ESlateBrushDrawType::Image;B.ImageSize=Display;
        if(auto* T=FACEDatTextureResolver::CreateTransientRgbaUi(W,H,Pixels)){ButtonTextures.Add(T);B.SetResourceObject(T);}
        else B.DrawAs=ESlateBrushDrawType::NoDrawType;
        return B;
    };
    // Window chrome: SPluginFrame paints the retail body art, wash and border itself in Codex.
    Frame=FSlateRoundedBoxBrush(FLinearColor(FColor(4,9,22,199)),2.f,Gold(.55f),1.f);
    Background=FSlateColorBrush(FLinearColor::Transparent);
    TitleBackground=FSlateColorBrush(FLinearColor::Transparent);
    SectionFrame=FSlateRoundedBoxBrush(FLinearColor(0.f,0.f,0.f,.16f),2.f,Gold(.24f),1.f);
    SectionHeader=FSlateColorBrush(FLinearColor::Transparent);
    // Red retail plates (0x06004C9E/9F/A0) shared with the lobby.
    UACEDatSubsystem* Dat=Resolver?Resolver->GetDatSubsystem():nullptr;
    const FACELobbyArt* Art=Dat?ACELobbyArt::Load(Dat):ACELobbyArt::Get();
    if(Art)
    {
        FSlateBrush Disabled=Art->SmallButton[0];Disabled.TintColor=FLinearColor(.42f,.4f,.4f,.75f);
        Button.SetNormal(Art->SmallButton[0]).SetHovered(Art->SmallButton[1]).SetPressed(Art->SmallButton[2]).SetDisabled(Disabled);
    }
    Button.SetNormalForeground(ACELobbyArt::CaptionText).SetHoveredForeground(ACELobbyArt::Highlight).SetPressedForeground(ACELobbyArt::CaptionText)
        .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
    // 21px hairline icon buttons (title bar, plugin bar, grips).
    DockButton=Button;
    DockButton.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0,0,0,0),2.f,Gold(.3f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(Gold(.08f),2.f,Gold(.7f),1.f))
        .SetPressed(FSlateRoundedBoxBrush(Gold(.16f),2.f,Gold(.95f),1.f))
        .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0,0,0,0),2.f,Gold(.15f),1.f))
        .SetNormalForeground(Cream()).SetHoveredForeground(GoldC).SetPressedForeground(GoldC);
    // Segmented choices: hairline navy cells, red when selected (SetAppearPressed).
    Tab=DockButton;
    Tab.SetNormal(FSlateRoundedBoxBrush(Navy,2.f,Gold(.35f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(Lin(10,20,42,.95f),2.f,Gold(.65f),1.f))
        .SetPressed(FSlateRoundedBoxBrush(Red,2.f,Gold(.85f),1.f));
    // Vertical rail: flat, a soft wash when selected (the 2px bar is painted by SACEPluginTab).
    RailItem=DockButton;
    RailItem.SetNormal(FSlateColorBrush(FLinearColor::Transparent)).SetHovered(FSlateColorBrush(Gold(.06f)))
        .SetPressed(FSlateColorBrush(Gold(.11f))).SetDisabled(FSlateColorBrush(FLinearColor::Transparent));
    // Slim list rows: thin gold hairline; brighter on hover and when selected.
    QuietRow=DockButton;
    QuietRow.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.18f),2.f,Gold(.24f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.02f,.03f,.06f,.35f),2.f,Gold(.45f),1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.02f,.03f,.06f,.35f),2.f,Gold(.6f),1.f));
    QuietRowSelected=QuietRow;
    QuietRowSelected.SetNormal(FSlateRoundedBoxBrush(Gold(.09f),2.f,Gold(.6f),1.f)).SetHovered(FSlateRoundedBoxBrush(Gold(.12f),2.f,Gold(.75f),1.f));
    // Fields and dropdowns: navy well with a gold hairline and a gold triangle.
    const FSlateRoundedBoxBrush Field(NavyDeep,2.f,Gold(.6f),1.f),FieldHot(NavyDeep,2.f,Gold(.9f),1.f);
    MenuItem=Tab;
    MenuItem.SetNormal(Field).SetHovered(FieldHot).SetPressed(FieldHot).SetDisabled(FSlateRoundedBoxBrush(NavyDeep,2.f,Gold(.25f),1.f));
    Edit.SetBackgroundImageNormal(Field).SetBackgroundImageHovered(FieldHot).SetBackgroundImageFocused(FieldHot).SetBackgroundImageReadOnly(Field)
        .SetForegroundColor(Cream()).SetPadding(FMargin(8,5));
    const FSlateBrush Arrow=Texture(18,10,[&](float X,float Y)
    {
        // Downward triangle, 2x supersampled edge.
        const float D=FMath::Max(Y-9.f,FMath::Abs(X-9.f)-(9.f-Y));
        return Over(FLinearColor::Transparent,GoldC,Edge(D));
    },FVector2D(9,5));
    Combo.SetButtonStyle(MenuItem).SetContentPadding(FMargin(8,4)).SetMenuBorderBrush(FSlateRoundedBoxBrush(Lin(5,11,26,.98f),2.f,Gold(.6f),1.f))
        .SetMenuBorderPadding(FMargin(2)).SetDownArrowImage(Arrow).SetDownArrowPadding(FMargin(8,0,2,0));
    ComboBox.SetComboButtonStyle(Combo).SetContentPadding(FMargin(8,4));
    const FSlateColorBrush RowIdle(FLinearColor::Transparent),RowHot(Gold(.14f));
    ComboRow.SetEvenRowBackgroundBrush(RowIdle).SetOddRowBackgroundBrush(RowIdle)
        .SetEvenRowBackgroundHoveredBrush(RowHot).SetOddRowBackgroundHoveredBrush(RowHot)
        .SetActiveBrush(RowHot).SetActiveHoveredBrush(RowHot).SetInactiveBrush(RowHot).SetInactiveHoveredBrush(RowHot)
        .SetSelectorFocusedBrush(RowHot).SetTextColor(Cream()).SetSelectedTextColor(GoldC);
    Spin.SetBackgroundBrush(Field).SetHoveredBackgroundBrush(FieldHot).SetActiveFillBrush(FSlateColorBrush(Gold(.25f)))
        .SetInactiveFillBrush(FSlateColorBrush(Gold(.12f))).SetForegroundColor(Cream());
    // 15px checkbox: navy box, gold hairline, gold check.
    auto Box=[&](bool Checked,bool Hot)
    {
        return Texture(30,30,[&](float X,float Y)
        {
            const float D=RoundRectDistance(X,Y,30,30,4);
            FLinearColor C=Over(FLinearColor::Transparent,NavyDeep,Edge(D+1.f));
            C=Over(C,Gold(Hot?.95f:.6f),Edge(FMath::Abs(D+1.f)-1.f));
            if(Checked)
            {
                const FVector2f P(X,Y);
                const float S=FMath::Min(SegmentDistance(P,FVector2f(8,15.5f),FVector2f(13,20.5f)),SegmentDistance(P,FVector2f(13,20.5f),FVector2f(22.5f,9.5f)));
                C=Over(C,GoldC,Edge(S-1.6f));
            }
            return C;
        },FVector2D(15,15));
    };
    Check.SetUncheckedImage(Box(false,false)).SetUncheckedHoveredImage(Box(false,true)).SetUncheckedPressedImage(Box(false,true))
        .SetCheckedImage(Box(true,false)).SetCheckedHoveredImage(Box(true,true)).SetCheckedPressedImage(Box(true,true))
        .SetForegroundColor(Cream()).SetHoveredForegroundColor(Cream()).SetPadding(FMargin(8,0,0,0));
    // 32x16 toggle: navy pill with a muted knob, red with a gold knob when on.
    auto Pill=[&](bool On,bool Hot)
    {
        return Texture(64,32,[&](float X,float Y)
        {
            const float D=RoundRectDistance(X,Y,64,32,16);
            FLinearColor C=Over(FLinearColor::Transparent,On?(Hot?RedHot:Red):NavyDeep,Edge(D+1.f));
            C=Over(C,Gold(On?.85f:(Hot?.8f:.45f)),Edge(FMath::Abs(D+1.f)-1.f));
            const FVector2f Knob(On?48.f:16.f,16.f);
            const float K=(FVector2f(X,Y)-Knob).Size()-10.f;
            C=Over(C,On?GoldC:Muted(),Edge(K));
            return C;
        },FVector2D(32,16));
    };
    Switch=Check;
    Switch.SetUncheckedImage(Pill(false,false)).SetUncheckedHoveredImage(Pill(false,true)).SetUncheckedPressedImage(Pill(false,true))
        .SetCheckedImage(Pill(true,false)).SetCheckedHoveredImage(Pill(true,true)).SetCheckedPressedImage(Pill(true,true)).SetPadding(FMargin(0));
    // Slider: 3px track (the fill is painted by SACEPluginSlider) and an 11px gold diamond.
    auto Diamond=[&](bool Hot)
    {
        return Texture(26,26,[&](float X,float Y)
        {
            const float D=(FMath::Abs(X-13.f)+FMath::Abs(Y-13.f)-11.f)*.7071f;
            FLinearColor C=Over(FLinearColor::Transparent,Lin(60,38,10),Edge(D));
            return Over(C,Hot?ACELobbyArt::Highlight:GoldC,Edge(D+1.6f));
        },FVector2D(13,13));
    };
    const FSlateColorBrush Track(Gold(.28f));
    Slider.SetNormalBarImage(Track).SetHoveredBarImage(Track).SetDisabledBarImage(FSlateColorBrush(Gold(.12f)))
        .SetNormalThumbImage(Diamond(false)).SetHoveredThumbImage(Diamond(true)).SetDisabledThumbImage(Diamond(false)).SetBarThickness(3.f);
}
bool FACEPluginStyle::SetWindowAppearance(float Opacity,bool Frameless)
{
    Opacity=FMath::Clamp(Opacity,0.f,1.f);
    if(BackgroundOpacity==Opacity&&HideFrame==Frameless)return false;
    BackgroundOpacity=Opacity;HideFrame=Frameless;
    auto Fade=[](FSlateBrush& To,const FSlateBrush& From,float Alpha)
    {
        To=From;auto Color=From.TintColor.GetSpecifiedColor();Color.A*=Alpha;To.TintColor=Color;
        Color=From.OutlineSettings.Color.GetSpecifiedColor();Color.A*=Alpha;To.OutlineSettings.Color=Color;
    };
    // Pinning hides chrome, not the backdrop behind the window's contents.
    Fade(Frame,OriginalFrame,Opacity);
    if(Frameless)
    {
        auto Outline=Frame.OutlineSettings.Color.GetSpecifiedColor();Outline.A=0.f;
        Frame.OutlineSettings.Color=Outline;
    }
    Fade(Background,OriginalBackground,Opacity);
    Fade(TitleBackground,OriginalTitle,Opacity);
    Fade(SectionFrame,OriginalSectionFrame,Opacity);
    Fade(SectionHeader,OriginalSectionHeader,Opacity);
    return true;
}
TSharedPtr<FACEPluginFont> FACEPluginStyle::Typeface(int32 Size,bool Fancy)
{
    // Resolve the nearest authored size instead of enlarging the smallest atlas.
    int32 Pixels=FMath::Clamp(FMath::RoundToInt(Size*1.333333f/2)*2,12,24);
    // Fancy (title) faces exist at 24/30/36/48px.
    if(Fancy){const float Want=Size*1.333333f;Pixels=Want<27?24:Want<33?30:Want<42?36:48;}
    auto& Face=Fonts.FindOrAdd(Fancy?1000+Pixels:Pixels);if(!Face)Face=MakeShared<FACEPluginFont>();
    if(!Face->Atlas&&Resolver&&Resolver->ResolveFont(ACEUIFontStyles::FontIdForStyleName((Fancy?FString::Printf(TEXT("fancyfont%d"),Pixels):FString::Printf(TEXT("basefont%d"),Pixels))),Face->Font))
        Face->Atlas=Resolver->ResolveFontAtlas(Face->Font.ForegroundSurfaceDataID,Face->Tint);
    return Face;
}
namespace
{
    float ButtonBorderWidth(const FButtonStyle& Style)
    {
        float Width=0;
        for(const auto* Brush:{&Style.Normal,&Style.Hovered,&Style.Pressed,&Style.Disabled})
        {
            if(Brush->DrawAs==ESlateBrushDrawType::Box||Brush->DrawAs==ESlateBrushDrawType::Border)
                Width=FMath::Max(Width,FMath::Max(FMath::Max(Brush->Margin.Left,Brush->Margin.Right)*float(Brush->ImageSize.X),
                    FMath::Max(Brush->Margin.Top,Brush->Margin.Bottom)*float(Brush->ImageSize.Y)));
            else if(Brush->DrawAs==ESlateBrushDrawType::RoundedBox)
                Width=FMath::Max(Width,Brush->OutlineSettings.Width);
        }
        return Width;
    }
    float ButtonInteriorPadding(const FButtonStyle& Style)
    {
        const float Border=ButtonBorderWidth(Style);
        // Keep a clear gap inside the artwork, including compact title controls.
        // Borderless inventory images retain their exact icon dimensions.
        return Border>0?Border+(Border>1?2.f:1.f):0.f;
    }
    void CenterButtonLabels(const TSharedRef<SWidget>& Widget)
    {
        if(Widget->GetType()==TEXT("SACEPluginText"))
            StaticCastSharedRef<SACEPluginText>(Widget)->SetJustification(ETextJustify::Center);
        else if(Widget->GetType()==TEXT("STextBlock"))
            StaticCastSharedRef<STextBlock>(Widget)->SetJustification(ETextJustify::Center);
        // Button content can include a label and icon or a multiline caption.
        // This runs once when constructed, never during paint or plugin ticks.
        auto* Children=Widget->GetChildren();
        for(int32 I=0;I<Children->Num();++I)CenterButtonLabels(Children->GetChildAt(I));
    }
}
void SACEPluginButton::Construct(const FArguments& A)
{
    // Reserve the full scaled retail cursor at the source, before Slate places
    // the first tooltip frame (the popup's later Tick cannot protect that frame).
    EnableToolTipForceField(true);
    SetToolTipForceFieldExpansion(TAttribute<TOptional<FSlateRect>>::CreateLambda([]
    {
        return TOptional<FSlateRect>(UACEHoverTooltipWidget::GetCursorExclusionRect(FSlateApplication::Get().GetCursorPos()));
    }));
    auto Args=A;ContentStyle=A._ButtonStyle;
    const auto Padding=A._ContentPadding;
    if(FlatRail){SButton::Construct(Args);return;}
    Args.HAlign(HAlign_Center).VAlign(VAlign_Center);
    // Evaluate the style here as DAT artwork may load after the widget is built.
    Args.ContentPadding_Lambda([this,Padding]
    {
        const auto P=Padding.Get();
        // Codex red plates carry a soft 7px shadow inside their artwork; pad the caption
        // like the lobby buttons instead of by the full 9-slice margin.
        if(FACEPluginStyle::IsCodex()&&ButtonBorderWidth(*ContentStyle)>8.f)
        {
            const float Base=FMath::Max(FMath::Max(P.Left,P.Right),FMath::Max(P.Top,P.Bottom));
            return FMargin(FMath::Max(14.f,Base+8.f),FMath::Max(6.f,Base+1.f));
        }
        return FMargin(FMath::Max(ButtonInteriorPadding(*ContentStyle),FMath::Max(FMath::Max(P.Left,P.Right),FMath::Max(P.Top,P.Bottom))));
    });
    SButton::Construct(Args);
    CenterButtonLabels(GetContent());
}
FVector2D SACEPluginButton::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
    auto Size=SButton::ComputeDesiredSize(LayoutScaleMultiplier);
    if(FlatRail)return Size;
    // A thick bevel needs a usable interior even for a single '+' or '-' glyph.
    // Thin frame controls and borderless inventory icons stay compact.
    if(ContentStyle&&FACEPluginStyle::IsCodex()&&ButtonBorderWidth(*ContentStyle)>8.f)
    {Size.X=FMath::Max(Size.X,40.);Size.Y=FMath::Max(Size.Y,30.);return Size;}
    if(ContentStyle&&ButtonBorderWidth(*ContentStyle)>1.f)
    {
        const float Minimum=16.f+2.f*ButtonInteriorPadding(*ContentStyle);
        Size.X=FMath::Max(Size.X,double(Minimum));Size.Y=FMath::Max(Size.Y,double(Minimum));
    }
    return Size;
}
void SACEPluginComboButton::Construct(const FArguments& A)
{
    auto Args=A;
    Args.HAlign(HAlign_Fill).VAlign(VAlign_Center).ContentPadding(FMargin(FACEPluginStyle::ButtonPadding));
    CenterButtonLabels(A._ButtonContent.Widget);
    SComboButton::Construct(Args);
}
void SACEPluginTab::Construct(const FArguments& A)
{
    Retail=FACEPluginStyle::Current();Selected=A._Selected;Rail=A._Rail&&Retail->Codex;Dot=A._Dot;
    TSharedRef<SWidget> Content=A._Content.Widget;
    if(Content==SNullWidget::NullWidget)
    {
        if(Rail)
            Content=SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SACEPluginText).Text_Lambda([T=A._Text]{return FText::FromString(T.Get().ToString().ToUpper());})
                    .Font([]{FSlateFontInfo F=FCoreStyle::GetDefaultFontStyle("Bold",10);F.LetterSpacing=140;return F;}())
                    .ColorAndOpacity_Lambda([this]{return IsSelected()?FSlateColor(FACEPluginStyle::Gold()):FSlateColor(FACEPluginStyle::Cream());})];
        else
            Content=SNew(SACEPluginText).Text(A._Text).Justification(ETextJustify::Center)
                .Font_Lambda([Size=A._FontSize]{return FCoreStyle::GetDefaultFontStyle("Regular",FMath::Clamp(Size.Get(15),10,36));})
                .ColorAndOpacity(Retail->Codex?FACEPluginStyle::Cream():FLinearColor(1.f,.98f,.92f));
    }
    FlatRail=Rail;
    SACEPluginButton::Construct(SButton::FArguments().ButtonStyle(Rail?&Retail->RailItem:&Retail->Tab).IsFocusable(false)
        .ContentPadding(Rail?TAttribute<FMargin>(FMargin(26,5,10,5)):A._ContentPadding).HAlign(Rail?HAlign_Fill:HAlign_Center).VAlign(VAlign_Center).OnClicked(A._OnClicked)[Content]);
    // Slate tracks this attribute and repaints retained tabs when selection
    // changes, without polling, reconstructing controls or shifting their size.
    SetAppearPressed(Selected);
}
int32 SACEPluginTab::OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& WidgetStyle,bool Enabled) const
{
    const int32 Top=SButton::OnPaint(A,G,Clip,Out,Layer,WidgetStyle,Enabled);
    const auto Size=G.GetLocalSize();
    const auto Effect=ShouldBeEnabled(Enabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect;
    const FSlateBrush* White=FCoreStyle::Get().GetBrush("WhiteBrush");
    if(Rail)
    {
        // Codex rail: small diamond, 2px gold bar on the left when selected, optional status dot.
        const FLinearColor Tint=WidgetStyle.GetColorAndOpacityTint();
        const bool On=IsSelected();
        const FVector2D C(14,Size.Y*.5);const float R=On?3.5f:3.f;
        const TArray<FVector2D> Diamond={C+FVector2D(0,-R),C+FVector2D(R,0),C+FVector2D(0,R),C+FVector2D(-R,0),C+FVector2D(0,-R)};
        FSlateDrawElement::MakeLines(Out,Top+1,G.ToPaintGeometry(),Diamond,Effect,FACEPluginStyle::Gold(On?1.f:.45f)*Tint,true,1.5f);
        if(On)FSlateDrawElement::MakeBox(Out,Top+1,G.ToPaintGeometry(FVector2D(2,Size.Y),FSlateLayoutTransform()),White,Effect,FACEPluginStyle::Gold()*Tint);
        const FLinearColor DotColor=Dot.Get(FLinearColor::Transparent);
        if(DotColor.A>0)
        {
            static const FSlateRoundedBoxBrush Round(FLinearColor::White,3.f);
            FSlateDrawElement::MakeBox(Out,Top+1,G.ToPaintGeometry(FVector2D(6,6),FSlateLayoutTransform(FVector2D(Size.X-14,Size.Y*.5-3))),&Round,Effect,DotColor*Tint);
        }
        return Top+1;
    }
    if(!IsSelected()||Retail->Codex)return Top;
    // Keep an unmistakable shape cue even with untinted retail bitmap fonts,
    // a hovered sibling, transparent window backgrounds or hidden window chrome.
    FSlateDrawElement::MakeBox(Out,Top+1,G.ToPaintGeometry(FVector2D(FMath::Max(0.,Size.X-4),3),FSlateLayoutTransform(FVector2D(2,Size.Y-3))),
        White,Effect,FLinearColor(1,.78f,.3f)*WidgetStyle.GetColorAndOpacityTint());
    return Top+1;
}
int32 SACEPluginSlider::OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& WidgetStyle,bool Enabled) const
{
    if(FACEPluginStyle::IsCodex()&&Style&&Orientation==Orient_Horizontal)
    {
        // Gold fill from the start of the track to the thumb (the track itself is the style's bar).
        const FVector2D Size=G.GetLocalSize(),Thumb=Style->NormalThumbImage.ImageSize;
        const float Fraction=FMath::Clamp(GetNormalizedValue(),0.f,1.f);
        const double Half=Thumb.X*.5,X=Half+(Size.X-Thumb.X)*Fraction,Bar=Style->BarThickness;
        // Drawn first: the translucent track then lays a faint sheen over it and the thumb stays on top.
        FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(FMath::Max(0.,X-Half),Bar),FSlateLayoutTransform(FVector2D(Half,(Size.Y-Bar)*.5))),
            FCoreStyle::Get().GetBrush("WhiteBrush"),ShouldBeEnabled(Enabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect,
            FACEPluginStyle::Gold(.9f)*WidgetStyle.GetColorAndOpacityTint());
        const int32 Base=SSlider::OnPaint(A,G,Clip,Out,Layer,WidgetStyle,Enabled);
        return Base;
    }
    return SSlider::OnPaint(A,G,Clip,Out,Layer,WidgetStyle,Enabled);
}
void SACEPluginText::Construct(const FArguments& A)
{
    Retail=FACEPluginStyle::Current();Wrap=A._AutoWrapText;WrapAt=A._WrapTextAt;Justification=A._Justification;
    if(!Retail->Codex){STextBlock::Construct(A);}
    else
    {
        // Codex sets body copy in compact Roboto (labels ~10pt, page copy ~12pt); classic plugin
        // pages ask for 13-22pt DAT sizes, so scale those down. Fancy titles keep their size.
        auto Args=A;
        auto Compact=[](FSlateFontInfo F)
        {
            // One Codex type scale: sublabel 9 (requested 11-12), label/value 10 (13-16),
            // emphasis 11 (17-20), large 13 (21+). Captions ask for 9-10 directly; Fancy titles keep their size.
            if(F.TypefaceFontName!=FName(TEXT("Fancy"))&&F.Size>=11.f)F.Size=F.Size<=12.f?9.f:F.Size<=16.f?10.f:F.Size<=20.f?11.f:13.f;
            return F;
        };
        if(A._Font.IsBound())Args._Font=TAttribute<FSlateFontInfo>::CreateLambda([Font=A._Font,Compact]{return Compact(Font.Get());});
        else if(A._Font.IsSet())Args._Font=Compact(A._Font.Get());
        STextBlock::Construct(Args);
    }
    // Match the bitmap path when a label uses characters absent from the DAT.
    // Never turn a narrow plugin title, setting or button into stacked fragments.
    SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
    SetClipping(EWidgetClipping::ClipToBounds);
}
void SACEPluginText::SetJustification(TAttribute<ETextJustify::Type> InJustification)
{
    Justification=InJustification;
    STextBlock::SetJustification(InJustification);
    Invalidate(EInvalidateWidgetReason::Paint);
}
float SACEPluginText::BitmapScale() const
{
    // Match Slate's point-to-pixel conversion, including sizes between DAT fonts.
    return FMath::Max(1.f,float(GetFont().Size))*1.333333f/FMath::Max(1u,Face->Font.MaxCharHeight);
}
void SACEPluginText::Layout(float Available) const
{
    const float Limit=WrapAt.Get(0)>0?WrapAt.Get():Wrap.Get(false)?Available:0;
    const int32 W=Limit>0?FMath::Max(1,FMath::FloorToInt(Limit/BitmapScale())):MAX_int32;
    const FString Text=GetText().ToString();
    if(Text!=CachedText||W!=CachedWidth)
    {
        CachedText=Text;CachedWidth=W;
        // Plugin labels wrap at word boundaries. Oversized words retain their
        // natural width and clip to the widget until the user widens the window.
        Lines=ACEDatText::Layout(Face->Font,Text,W,false,/*bBreakLongWords=*/false);
    }
}
bool SACEPluginText::CanDrawNative() const
{
    // Bound font attributes are evaluated after Construct, and can change while
    // a Lua control keeps its Slate identity. Refresh both glyphs and wrapping.
    const int32 Size=GetFont().Size;
    // Codex titles request the DAT "fancy" face through the font's typeface name.
    const bool Fancy=GetFont().TypefaceFontName==FName(TEXT("Fancy"));
    // Codex body text is set in the lobby's sans (Roboto); only Fancy titles use DAT glyphs.
    if(Retail->Codex&&!Fancy)return false;
    const int32 Key=Fancy?1000+Size:Size;
    if(!Face||FaceSize!=Key||!Face->Atlas)
    {Face=Retail->Typeface(Size,Fancy);FaceSize=Key;CachedWidth=-1;CheckedGlyphs.Reset();NativeGlyphs=true;}
    if(!Face->Atlas)return false;
    const FString Text=GetText().ToString();
    if(Text!=CheckedGlyphs)
    {
        CheckedGlyphs=Text;NativeGlyphs=true;
        for(TCHAR C:Text)if(C>32&&!Face->Font.IndexByUnicode.Contains(C)){NativeGlyphs=false;break;}
    }
    // Keep international plugin labels and symbols readable when absent from the retail atlas.
    return NativeGlyphs;
}
FVector2D SACEPluginText::ComputeDesiredSize(float S) const
{
    if(!CanDrawNative())return STextBlock::ComputeDesiredSize(S);
    const auto M=GetMargin();Layout(FMath::Max(0.f,Width-M.Left-M.Right));int32 W=0;
    for(const auto& L:Lines)W=FMath::Max(W,L.Width);
    return FVector2D(W,Lines.Num()*Face->Font.MaxCharHeight)*BitmapScale()+FVector2D(M.Left+M.Right,M.Top+M.Bottom);
}
int32 SACEPluginText::OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& E,int32 Layer,const FWidgetStyle& Style,bool Enabled) const
{
    if(!CanDrawNative())return STextBlock::OnPaint(A,G,Clip,E,Layer,Style,Enabled);
    // STextBlock is non-ticking. Use the allotted width to reserve wrapped line height
    // on the next prepass without enabling per-frame work on every label.
    if(Wrap.Get(false)&&!FMath::IsNearlyEqual(Width,float(G.GetLocalSize().X)))
    {Width=G.GetLocalSize().X;const_cast<SACEPluginText*>(this)->Invalidate(EInvalidateWidgetReason::Layout);}
    const auto M=GetMargin();const float Available=G.GetLocalSize().X-M.Left-M.Right;Layout(Available);
    const float Scale=BitmapScale();auto* Atlas=Face->Atlas.Get();const auto& DatFont=Face->Font;
    FSlateBrush B;B.DrawAs=ESlateBrushDrawType::Image;B.SetResourceObject(Atlas);
    const FLinearColor Color=(Face->Tint?GetColorAndOpacity().GetColor(Style):FLinearColor::White)*Style.GetColorAndOpacityTint();
    // Rounding each scaled glyph quad independently can squeeze a thin stem
    // out of its sample footprint (notably 'l' at 16pt). Preserve fractional
    // geometry and let the atlas's bilinear sampler retain its coverage.
    // Native/integer pixel sizes still snap for crisp authored bitmap text.
    const float RasterScale=Scale*G.GetAccumulatedLayoutTransform().GetScale();
    const auto Effects=(ShouldBeEnabled(Enabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect)
        | (FMath::IsNearlyEqual(RasterScale,FMath::RoundToFloat(RasterScale),1.e-4f)?ESlateDrawEffect::None:ESlateDrawEffect::NoPixelSnapping);
    E.PushClip(FSlateClippingZone(G.GetLayoutBoundingRect().IntersectionWith(Clip)));
    for(int32 N=0;N<Lines.Num();++N)
    {
        const auto& L=Lines[N];float X=M.Left;const float Spare=FMath::Max(0.f,Available-L.Width*Scale);
        if(Justification.Get()==ETextJustify::Center)X+=Spare*.5f;else if(Justification.Get()==ETextJustify::Right)X+=Spare;
        const float Y=M.Top+N*DatFont.MaxCharHeight*Scale;
        for(int32 I=L.Begin;I<L.End;++I)if(const auto* C=ACEDatText::FindChar(DatFont,CachedText[I]))
        {
            X+=int8(C->HorizontalOffsetBefore)*Scale;
            if(C->Width&&C->Height)
            {
                const FVector2D UV(C->OffsetX,C->OffsetY),Size(C->Width,C->Height),AS(Atlas->GetSizeX(),Atlas->GetSizeY());
                B.SetUVRegion(FBox2f(FVector2f(UV/AS),FVector2f((UV+Size)/AS)));
                FSlateDrawElement::MakeBox(E,Layer,G.ToPaintGeometry(Size*Scale,FSlateLayoutTransform(FVector2D(X,Y+int8(C->VerticalOffsetBefore)*Scale))),&B,Effects,Color);
            }
            X+=(C->Width+int8(C->HorizontalOffsetAfter))*Scale;
        }
    }
    E.PopClip();return Layer;
}
