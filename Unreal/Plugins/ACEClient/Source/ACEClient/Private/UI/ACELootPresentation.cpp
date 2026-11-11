#include "UI/ACELootPresentation.h"
#include "UI/ACELootIconOverlay.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEHoverTooltipWidget.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

class SACEQualityIcon : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SACEQualityIcon){}SLATE_END_ARGS()
    void Construct(const FArguments&){}
    bool Rated=false,Unavailable=false;FLinearColor Tint;FString Mark;FVector2D MarkAt=FVector2D::ZeroVector;
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(32,32);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,
        int32 L,const FWidgetStyle&,bool)const override
    {
        const auto* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));const FVector2D Size=G.GetLocalSize();
        auto Box=[&](FVector2D P,FVector2D S,FLinearColor C)
        {FSlateDrawElement::MakeBox(E,L,G.ToPaintGeometry(S,FSlateLayoutTransform(P)),White,ESlateDrawEffect::None,C);};
        if(Rated)
        {
            const float Scale=FMath::Min(Size.X,Size.Y)/32.f;
            // A beveled slot frame: dark outer rim, quality-colored metal,
            // light on the upper/left faces and a recessed inner seam.
            // Nothing is painted over the interior artwork or its background.
            auto Loop=[&](float Inset,float Corner)
            {
                const float A=Inset*Scale,B=Corner*Scale;
                return TArray<FVector2D>{{A+B,A},{Size.X-A-B,A},{Size.X-A,A+B},
                    {Size.X-A,Size.Y-A-B},{Size.X-A-B,Size.Y-A},{A+B,Size.Y-A},
                    {A,Size.Y-A-B},{A,A+B},{A+B,A}};
            };
            auto Line=[&](const TArray<FVector2D>& Points,float Width,FLinearColor Color)
            {FSlateDrawElement::MakeLines(E,L,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width*Scale);};
            E.PushClip(FSlateClippingZone(G.GetLayoutBoundingRect()));
            Line(Loop(2,1.5f),4,Tint.CopyWithNewOpacity(.14f));
            Line(Loop(1.1f,1.8f),2.1f,FLinearColor(.004f,.006f,.009f,.98f));
            Line(Loop(1.35f,1.6f),1.1f,(Tint*.72f).CopyWithNewOpacity(.98f));
            Line(Loop(2.1f,1.1f),1.2f,Tint.CopyWithNewOpacity(.96f));
            const FLinearColor Light=(Tint+FLinearColor(.12f,.12f,.12f,0)).CopyWithNewOpacity(.92f);
            const FLinearColor Shade=(Tint*.32f).CopyWithNewOpacity(.9f);
            Line({{1.4f*Scale,Size.Y-3.2f*Scale},{1.4f*Scale,3.2f*Scale},
                {3.2f*Scale,1.4f*Scale},{Size.X-3.2f*Scale,1.4f*Scale}},.85f,Light);
            Line({{Size.X-1.25f*Scale,3.3f*Scale},{Size.X-1.25f*Scale,Size.Y-3.3f*Scale},
                {Size.X-3.3f*Scale,Size.Y-1.25f*Scale},{3.3f*Scale,Size.Y-1.25f*Scale}},.85f,Shade);
            Line(Loop(3.15f,.8f),.65f,FLinearColor(.008f,.01f,.015f,.7f));
            E.PopClip();
        }
        // A small corner marker replaces the unusable background tint on imbued
        // items. Keep the quality edge and authored effect artwork intact.
        if(Unavailable)
        {
            const float Scale=FMath::Min(Size.X,Size.Y)/32.f;
            Box(Size-FVector2D(8,8)*Scale,FVector2D(4,2)*Scale,FLinearColor(1,.35,.7));
            Box(Size-FVector2D(6,8)*Scale,FVector2D(2,4)*Scale,FLinearColor(1,.35,.7));
        }
        if(!Mark.IsEmpty())
        {
            const FVector2D Scale=Size/32.;const bool Two=Mark==TEXT("2H");
            const FVector2D P=MarkAt*Scale;
            Box(P,FVector2D(Two?10:7,Two?7:9)*Scale,FLinearColor(.015,.02,.03,.94));
            const FVector2D MarkSize=FVector2D(Two?10:7,Two?7:9)*Scale;
            E.PushClip(FSlateClippingZone(G.MakeChild(MarkSize,FSlateLayoutTransform(P)).GetLayoutBoundingRect()));
            FSlateDrawElement::MakeText(E,L+1,G.ToPaintGeometry(Size,FSlateLayoutTransform(P+FVector2D(1,0)*Scale)),Mark,
                FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),Two?5:6),ESlateDrawEffect::None,FLinearColor(.95,.92,.80));
            E.PopClip();
        }
        return L+1;
    }
};
void UACELootIconOverlay::SetReview(const FACEQualityReview& Review,UTexture2D* Foreground,bool PreserveBackground)
{
    TakeWidget();Painted->Rated=Review.bRated;Painted->Tint=ACELootQuality::Color(Review.Grade);Painted->Mark.Empty();
    Painted->Unavailable=PreserveBackground&&Review.bCannotWield;
    // Test actual composited foreground coverage. Never cover the weapon to
    // force a class badge onto a small inventory icon.
    if(!Review.ClassMark.IsEmpty()&&Foreground&&Foreground->GetPlatformData()&&Foreground->GetPlatformData()->Mips.Num())
    {
        const auto* Platform=Foreground->GetPlatformData();
        if(Platform->SizeX==32&&Platform->SizeY==32&&Platform->PixelFormat==PF_B8G8R8A8)
        {
            auto& Mip=Foreground->GetPlatformData()->Mips[0];const auto* Pixels=static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
            if(Pixels)
            {
                const int32 W=Review.ClassMark==TEXT("2H")?10:7,H=Review.ClassMark==TEXT("2H")?7:9;
                for(const FIntPoint P:{FIntPoint(4,4),FIntPoint(4,28-H)})
                {
                    bool Clear=true;for(int32 Y=P.Y;Y<P.Y+H&&Clear;++Y)for(int32 X=P.X;X<P.X+W;++X)
                        if(Pixels[Y*32+X].A>16){Clear=false;break;}
                    if(Clear){Painted->Mark=Review.ClassMark;Painted->MarkAt=FVector2D(P);break;}
                }
            }
            Mip.BulkData.Unlock();
        }
    }
    Painted->Invalidate(EInvalidateWidgetReason::Paint);
}
TSharedRef<SWidget> UACELootIconOverlay::RebuildWidget(){return SAssignNew(Painted,SACEQualityIcon);}
void UACELootIconOverlay::ReleaseSlateResources(bool Children){Super::ReleaseSlateResources(Children);Painted.Reset();}

namespace
{
class SACELootArt : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SACELootArt){}SLATE_ARGUMENT(UTexture2D*,Background)SLATE_ARGUMENT(UTexture2D*,Foreground)SLATE_END_ARGS()
    void Construct(const FArguments& A)
    {Bg.SetResourceObject(A._Background);Fg.SetResourceObject(A._Foreground);Bg.DrawAs=Fg.DrawAs=ESlateBrushDrawType::Image;Bg.ImageSize=Fg.ImageSize=FVector2D(32,32);}
    virtual FVector2D ComputeDesiredSize(float)const override{return {32,32};}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,int32 L,const FWidgetStyle&,bool)const override
    {FSlateDrawElement::MakeBox(E,L,G.ToPaintGeometry(),&Bg);FSlateDrawElement::MakeBox(E,L+1,G.ToPaintGeometry(),&Fg);return L+1;}
    FSlateBrush Bg,Fg;
};
TSharedRef<STextBlock> Text(const FString& Value,int32 Size,FLinearColor Color,float Wrap)
{
    return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size))
        .ColorAndOpacity(Color).AutoWrapText(false).WrapTextAt(Wrap);
}
}
void ACELootPresentation::Tooltip(UWidget* Owner,const FString& Name,const FACEWorldObject& Item,
    const FACEQualityReview& R,UACEUIResourceResolver* Resources)
{
    if(!R.bEquipment){UACEHoverTooltipWidget::SetWidgetTooltip(Owner,FText::FromString(Name),UACEHoverTooltipWidget::ObjectTemplate);return;}
    const FVector2D View=Owner&&Owner->GetWorld()?UWidgetLayoutLibrary::GetViewportSize(Owner):FVector2D::ZeroVector;
    const float Dpi=Owner&&Owner->GetWorld()?UWidgetLayoutLibrary::GetViewportScale(Owner):1.f;
    const float Width=View.X>1?FMath::Clamp(float(View.X/FMath::Max(Dpi,.1f)-24),220.f,380.f):380.f;
    const float ContentWidth=Width-20;
    const FLinearColor BaseColor(.8,.81,.83),TinkerColor(1,.68,.25),CantripColor(.45,.78,1);
    auto T=[&](const FString& Value,int32 Size=11,FLinearColor Color=FLinearColor(.9,.91,.93))
        {return Text(Value,Size,Color,ContentWidth);};
    TSharedRef<SVerticalBox> Rows=SNew(SVerticalBox);
    TSharedRef<SHorizontalBox> Header=SNew(SHorizontalBox);
    if(Resources)Header->AddSlot().AutoWidth().Padding(0,0,8,0)
        [SNew(SBox).WidthOverride(32).HeightOverride(32)[SNew(SACELootArt)
        .Background(Resources->ResolveItemBackground(Item.ItemType,Item.IconUnderlayId))
        .Foreground(Resources->ResolveItemForeground(Item.IconId,Item.IconOverlayId,Item.UiEffects,false,Item.Structure,Item.MaxStructure))]];
    Header->AddSlot().FillWidth(1)[Text(Name,15,R.bRated?ACELootQuality::Color(R.Grade):FLinearColor(.95,.92,.8),ContentWidth-(Resources?40:0))];
    Rows->AddSlot().AutoHeight()[Header];
    FString Class=R.Class.Replace(TEXT(" Weapons"),TEXT("")).Replace(TEXT(" / Elemental caster"),TEXT(""))
        .Replace(TEXT(" / combat spell package"),TEXT("")).Replace(TEXT(" / spell package"),TEXT(""));
    TSharedRef<SHorizontalBox> TypeLine=SNew(SHorizontalBox);
    TypeLine->AddSlot().FillWidth(1)[Text(Class,10,FLinearColor(.8,.82,.87),ContentWidth-90)];
    if(!R.DamageType.IsEmpty())
    {
        const uint32 Effect=R.DamageType==TEXT("Cold")?0x80:R.DamageType==TEXT("Fire")?0x20:
            R.DamageType==TEXT("Acid")?0x100:R.DamageType==TEXT("Electric")?0x40:
            R.DamageType==TEXT("Slashing")?0x400:R.DamageType==TEXT("Piercing")?0x800:
            R.DamageType==TEXT("Bludgeoning")?0x200:R.DamageType==TEXT("Nether")?0x1000:0;
        TypeLine->AddSlot().AutoWidth()[Text(R.DamageType,10,Resources?Resources->ResolveItemEffectColor(Effect):CantripColor,85)];
    }
    Rows->AddSlot().AutoHeight().Padding(0,3)[TypeLine];
    FString Status=R.bRated?FString::Printf(TEXT("%s · %.1f"),*ACELootQuality::Name(R.Grade),R.Points):
        !R.bIdentified?TEXT("Identify"):R.Tinkers>0&&!R.bOriginalKnown?TEXT("Original not recorded"):TEXT("");
    if(R.bRated&&R.Class.Contains(TEXT("spell package")))Status+=TEXT(" · spells");
    if(!Status.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,1,0,5)[T(Status,11,R.bRated?ACELootQuality::Color(R.Grade):BaseColor)];
    for(const auto& Line:R.CurrentStats)
    {
        const auto* S=R.Stats.FindByPredicate([&](const FACEQualityStat& Stat){return Stat.Key==(Line.Key==TEXT("ElementPvP")?TEXT("Element"):Line.Key);});
        FACEQualityStat PvP;
        if(S&&Line.Key==TEXT("ElementPvP"))
        {
            PvP=*S;PvP.Original*=.5;PvP.Permanent*=.5;PvP.WithCantrip*=.5;
            PvP.Bounds.Low*=.5;PvP.Bounds.High*=.5;S=&PvP;
        }
        FString ValueText=Line.Value;
        // These are natural roll limits, not caps on a tinkered or buffed item.
        // Armor resistance rows display effective AL rather than multipliers.
        if(S&&S->Bounds.Variable()&&(!Line.Key.StartsWith(TEXT("Armor"))||Line.Key==TEXT("Armor")))
        {
            const double Best=S->bLowerBetter?S->Bounds.Low:S->Bounds.High;
            FString Limit=ACELootQuality::FormatStat(Best,S->bPercent);
            if(S->bPercent&&!S->bLowerBetter&&Best>=0)Limit=TEXT("+")+Limit;
            if(Line.Key==TEXT("Multiplier"))Limit=TEXT("×")+Limit;
            ValueText+=FString::Printf(TEXT(" (base %s %s)"),S->bLowerBetter?TEXT("best"):TEXT("max"),*Limit);
        }
        TSharedRef<SHorizontalBox> Row=SNew(SHorizontalBox);
        Row->AddSlot().FillWidth(.43f)[Text(Line.Label+TEXT(":"),11,FLinearColor(.82,.83,.86),ContentWidth*.43f)];
        Row->AddSlot().FillWidth(.57f)[Text(ValueText,11,FLinearColor(.96,.95,.9),ContentWidth*.57f)];
        Rows->AddSlot().AutoHeight().Padding(0,1)[Row];
        if(!S||!S->bPermanentKnown)continue;
        auto Value=[&](double V){return ACELootQuality::FormatStat(V,S->bPercent);};
        if(S->bOriginalKnown&&FMath::Abs(S->Original-S->Permanent)>1.e-5)
        {
            const double Delta=S->Permanent-S->Original;
            const FString Change=S->bPercent?FString::Printf(TEXT("%+.1f pp"),Delta*100):FString::Printf(TEXT("%+.1f"),Delta);
            const FString Base=Line.Key.StartsWith(TEXT("Armor"))&&Line.Key!=TEXT("Armor")?TEXT("  Base resistance ×"):TEXT("  Base ");
            Rows->AddSlot().AutoHeight()[T(Base+Value(S->Original)+TEXT(" · Tinkering ")+Change,9,TinkerColor)];
        }
        if(FMath::Abs(S->WithCantrip-S->Permanent)>1.e-5)
        {
            const double Delta=S->WithCantrip-S->Permanent;
            Rows->AddSlot().AutoHeight()[T(FString(TEXT("  Cantrip "))+(Delta>=0?TEXT("+"):TEXT(""))+Value(Delta),9,CantripColor)];
        }
    }
    if(!R.Properties.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,5,0,0)[T(FString::Join(R.Properties,TEXT(" · ")),10)];
    if(!R.SpellNames.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,5,0,0)[T(TEXT("Spells: ")+FString::Join(R.SpellNames,TEXT(" · ")),10,CantripColor)];
    TArray<FString> Checks;R.Requirements.ParseIntoArrayLines(Checks,true);
    if(Checks.Num()>1&&Checks[0].Contains(TEXT("skill ")))Checks.RemoveAt(0);
    for(auto& Check:Checks)
    {
        if(Check==TEXT("Wield requirements met")){Check.Empty();continue;}
        Check=Check.Replace(TEXT("Wield requirements: server check needed"),TEXT("Server check"))
            .Replace(TEXT("Heavy Weapons"),TEXT("Heavy")).Replace(TEXT("Light Weapons"),TEXT("Light"))
            .Replace(TEXT("Finesse Weapons"),TEXT("Finesse")).Replace(TEXT("Two Handed Combat"),TEXT("Two-handed"));
    }
    Checks.RemoveAll([](const FString& S){return S.IsEmpty();});
    FString Requirements=Checks.IsEmpty()?FString():TEXT("Wield: ")+FString::Join(Checks,TEXT(" · "));
    if(!R.Activation.IsEmpty()&&!R.Activation.StartsWith(TEXT("No Arcane Lore")))
    {
        if(!Requirements.IsEmpty())Requirements+=TEXT(" · ");
        Requirements+=R.Activation;
    }
    if(!Requirements.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,5,0,0)[T(Requirements,10,
        R.bCannotWield||R.bActivationBlocked?FLinearColor(1,.55,.55):FLinearColor(.7,.82,.7))];
    UACEHoverTooltipWidget::SetWidgetContentTooltip(Owner,FText::FromString(Name),
        SNew(SBox).WidthOverride(Width)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(R.bRated?ACELootQuality::Color(R.Grade):FLinearColor(.35,.35,.4)).Padding(1)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.018,.022,.033,.98)).Padding(9)[Rows]]]);
}
