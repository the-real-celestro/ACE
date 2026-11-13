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
#include "Widgets/Text/SRichTextBlock.h"
#include "Styling/SlateStyle.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "String/LexFromString.h"
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
struct FLootTooltipState
{
    FString Name;float Width=0;bool bHasArt=false;
    TWeakObjectPtr<UTexture2D> Background,Foreground;
    FLinearColor DamageColor;
    FACEQualityReview Review;
    bool Matches(const FLootTooltipState& Other) const
    {
        const auto& A=Review;const auto& B=Other.Review;
        if(Name!=Other.Name||Width!=Other.Width||bHasArt!=Other.bHasArt||Background!=Other.Background||Foreground!=Other.Foreground||DamageColor!=Other.DamageColor
            ||A.bRated!=B.bRated||A.bIdentified!=B.bIdentified||A.bOriginalKnown!=B.bOriginalKnown||A.bCannotWield!=B.bCannotWield
            ||A.bActivationBlocked!=B.bActivationBlocked||A.Class!=B.Class||A.DamageType!=B.DamageType||A.Grade!=B.Grade||A.Points!=B.Points
            ||A.bEstimated!=B.bEstimated||A.EstimateLow!=B.EstimateLow||A.EstimateHigh!=B.EstimateHigh||A.Reason!=B.Reason
            ||A.Tinkers!=B.Tinkers||A.Requirements!=B.Requirements||A.Activation!=B.Activation||A.Properties!=B.Properties||A.SpellNames!=B.SpellNames
            ||A.CurrentStats.Num()!=B.CurrentStats.Num()||A.Stats.Num()!=B.Stats.Num()||A.SpellGrades.Num()!=B.SpellGrades.Num()||A.Checks.Num()!=B.Checks.Num())return false;
        for(const auto& P:A.SpellGrades){const auto* Grade=B.SpellGrades.Find(P.Key);if(!Grade||*Grade!=P.Value)return false;}
        for(int32 I=0;I<A.Checks.Num();++I)
        {const auto& X=A.Checks[I];const auto& Y=B.Checks[I];if(X.Label!=Y.Label||X.Have!=Y.Have||X.bMet!=Y.bMet||X.bKnown!=Y.bKnown)return false;}
        for(int32 I=0;I<A.CurrentStats.Num();++I)
        {
            const auto& X=A.CurrentStats[I];const auto& Y=B.CurrentStats[I];
            if(X.Key!=Y.Key||X.Label!=Y.Label||X.Value!=Y.Value||X.SpellContribution!=Y.SpellContribution||X.Color!=Y.Color||X.bSpellModified!=Y.bSpellModified)return false;
        }
        for(int32 I=0;I<A.Stats.Num();++I)
        {
            const auto& X=A.Stats[I];const auto& Y=B.Stats[I];
            if(X.Key!=Y.Key||X.Original!=Y.Original||X.Permanent!=Y.Permanent||X.WithCantrip!=Y.WithCantrip||X.Bounds.Low!=Y.Bounds.Low||X.Bounds.High!=Y.Bounds.High
                ||X.bOriginalKnown!=Y.bOriginalKnown||X.bOriginalEstimated!=Y.bOriginalEstimated||X.bPermanentKnown!=Y.bPermanentKnown||X.bPercent!=Y.bPercent||X.bLowerBetter!=Y.bLowerBetter||X.bPotential!=Y.bPotential||X.CantripPreview!=Y.CantripPreview)return false;
        }
        return true;
    }
};
class SACELootTooltip : public SBox
{
public:
    SLATE_BEGIN_ARGS(SACELootTooltip){}SLATE_END_ARGS()
    void Construct(const FArguments&){SBox::Construct(SBox::FArguments());}
    FLootTooltipState State;
};
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
TSharedRef<STextBlock> Text(const FString& Value,int32 Size,FLinearColor Color,float Wrap,ETextJustify::Type Align=ETextJustify::Left,FName Tag=NAME_None)
{
    return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size))
        .ColorAndOpacity(Color).AutoWrapText(false).WrapTextAt(Wrap).Justification(Align).WrappingPolicy(ETextWrappingPolicy::DefaultWrapping).Tag(Tag);
}
const FSlateStyleSet& SpellStyles()
{
    static const TSharedRef<FSlateStyleSet> Styles=[]
    {
        auto S=MakeShared<FSlateStyleSet>(TEXT("ACELootSpellUsefulness"));
        const FTextBlockStyle Base=FTextBlockStyle().SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).SetColorAndOpacity(FLinearColor(.9,.91,.93));
        S->Set(TEXT("Body"),Base);
        for(int32 I=0;I<6;++I)S->Set(FName(*FString::Printf(TEXT("q%d"),I)),FTextBlockStyle(Base).SetColorAndOpacity(ACELootQuality::Color(static_cast<ACELootScoring::Grade>(I))));
        return S;
    }();return Styles.Get();
}
FString SpellMarkup(const FACEQualityReview& R)
{
    TArray<FString> Runs;
    for(const auto& Name:R.SpellNames)
    {
        FString Safe=Name;Safe.ReplaceInline(TEXT("&"),TEXT("&amp;"));Safe.ReplaceInline(TEXT("<"),TEXT("&lt;"));Safe.ReplaceInline(TEXT(">"),TEXT("&gt;"));Safe.ReplaceInline(TEXT("\""),TEXT("&quot;"));
        const auto* Grade=R.SpellGrades.Find(Name);
        Runs.Add(Grade?FString::Printf(TEXT("<q%d>%s</>"),int32(*Grade),*Safe):Safe);
    }
    return FString::Join(Runs,TEXT(" · "));
}
}
void ACELootPresentation::Tooltip(UWidget* Owner,const FString& Name,const FACEWorldObject& Item,
    const FACEQualityReview& R,UACEUIResourceResolver* Resources)
{
    if(!R.bEquipment){UACEHoverTooltipWidget::SetWidgetTooltip(Owner,FText::FromString(Name),UACEHoverTooltipWidget::ObjectTemplate);return;}
    const FVector2D View=Owner&&Owner->GetWorld()?UWidgetLayoutLibrary::GetViewportSize(Owner):FVector2D::ZeroVector;
    const float Dpi=Owner&&Owner->GetWorld()?UWidgetLayoutLibrary::GetViewportScale(Owner):1.f;
    const float MaxWidth=View.X>1?FMath::Clamp(float(View.X/FMath::Max(Dpi,.1f)-24),220.f,380.f):380.f;
    const FLinearColor BaseColor(.8,.81,.83),TinkerColor(1,.68,.25),CantripColor(.45,.78,1);
    const uint32 Effect=R.DamageType==TEXT("Cold")?0x80:R.DamageType==TEXT("Fire")?0x20:
        R.DamageType==TEXT("Acid")?0x100:R.DamageType==TEXT("Electric")?0x40:
        R.DamageType==TEXT("Slashing")?0x400:R.DamageType==TEXT("Piercing")?0x800:
        R.DamageType==TEXT("Bludgeoning")?0x200:R.DamageType==TEXT("Nether")?0x1000:0;
    FLootTooltipState State;State.Name=Name;State.Width=MaxWidth;State.bHasArt=Resources!=nullptr;State.Review=R;
    State.Background=Resources?Resources->ResolveItemBackground(Item.ItemType,Item.IconUnderlayId):nullptr;
    State.Foreground=Resources?Resources->ResolveItemForeground(Item.IconId,Item.IconOverlayId,Item.UiEffects,false,Item.Structure,Item.MaxStructure):nullptr;
    State.DamageColor=Resources?Resources->ResolveItemEffectColor(Effect):CantripColor;
    FString Class=R.Class.Replace(TEXT(" Weapons"),TEXT("")).Replace(TEXT(" / Elemental caster"),TEXT(""))
        .Replace(TEXT(" / combat spell package"),TEXT("")).Replace(TEXT(" / spell package"),TEXT("")).Replace(TEXT(" / set package"),TEXT(""));
    if(Item.ItemType&ACEItemType::Caster)Class=TEXT("Caster · ")+Class;
    auto Lines=R.CurrentStats;
    Lines.Insert({TEXT("ItemClass"),TEXT("Type"),Class},0);
    if(!R.DamageType.IsEmpty())Lines.Insert({TEXT("DamageType"),TEXT("Damage type"),R.DamageType},1);
    TSharedPtr<SACELootTooltip> Panel;
    if(auto Existing=UACEHoverTooltipWidget::GetWidgetContentTooltip(Owner))
        if(Existing->GetType()==FName(TEXT("SACELootTooltip")))Panel=StaticCastSharedPtr<SACELootTooltip>(Existing);
    if(Panel&&Panel->State.Matches(State))return;
    if(!Panel)Panel=SNew(SACELootTooltip);
    struct FDisplayStat
    {
        const FACEItemStatLine* Line=nullptr;
        FACEQualityStat Comparison;
        bool bHasComparison=false;
        FString Value;
    };
    TArray<FDisplayStat> DisplayStats;
    auto Measure=[](const FString& Value,int32 Size=11)->float
    {
        if(FSlateApplication::IsInitialized()&&FSlateApplication::Get().GetRenderer())
            return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Value,FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).X;
        return Value.Len()*Size*.55f;
    };
    float LabelWidth=0,ValueWidth=0;
    for(const auto& Line:Lines)
    {
        // Zero bonuses are noise; zero speed and zero variance remain meaningful.
        if(Line.Key!=TEXT("Variance")&&Line.Value.EndsWith(TEXT("%")))
        {
            double Value=0;
            if(LexTryParseString(Value,*Line.Value.LeftChop(1))&&FMath::IsNearlyZero(Value))continue;
        }
        auto& Display=DisplayStats.AddDefaulted_GetRef();Display.Line=&Line;
        const auto* S=R.Stats.FindByPredicate([&](const FACEQualityStat& Stat){return Stat.Key==(Line.Key==TEXT("ElementPvP")?TEXT("Element"):Line.Key);});
        if(S)
        {
            Display.bHasComparison=true;Display.Comparison=*S;S=&Display.Comparison;
            if(Line.Key==TEXT("ElementPvP"))
            {
                auto& PvP=Display.Comparison;
                PvP.Original*=.5;PvP.Permanent*=.5;PvP.WithCantrip*=.5;
                PvP.Bounds.Low*=.5;PvP.Bounds.High*=.5;
            }
        }
        Display.Value=Line.Value+Line.SpellContribution;
        // Natural roll limits are not caps on a tinkered or buffed item.
        if(S&&S->Bounds.Variable()&&(!Line.Key.StartsWith(TEXT("Armor"))||Line.Key==TEXT("Armor")))
        {
            const double Best=S->bLowerBetter?S->Bounds.Low:S->Bounds.High;
            FString Limit=ACELootQuality::FormatStat(Best,S->bPercent);
            if(S->bPercent&&!S->bLowerBetter&&Best>=0)Limit=TEXT("+")+Limit;
            if(Line.Key==TEXT("Multiplier"))Limit=(Best>=1?FString(TEXT("+")):FString())+ACELootQuality::FormatStat(Best-1,true);
            Display.Value+=ACELootQuality::IsSummoningEssence(Item)?FString::Printf(TEXT(" (max %s)"),*Limit):
                FString::Printf(TEXT(" (base %s %s)"),S->bLowerBetter?TEXT("best"):TEXT("max"),*Limit);
        }
        LabelWidth=FMath::Max(LabelWidth,Measure(Line.Label+TEXT(":")));
        ValueWidth=FMath::Max(ValueWidth,Measure(Display.Value));
    }
    // Allow the label's extra pixel and font-layout rounding before fixing the
    // panel width, so a fitting value cannot wrap its final word needlessly.
    const FString Tier=R.bRated?ACELootQuality::Name(R.Grade):R.bIdentified?TEXT("Unrated"):TEXT("Identify");
    const float BadgeWidth=FMath::Max(74.f,Measure(Tier,12)+24);
    const FLinearColor BadgeFill=R.bRated?ACELootQuality::Color(R.Grade):FLinearColor(.55,.58,.63);
    const FLinearColor BadgeInk=BadgeFill.GetLuminance()>.179f?FLinearColor(.006,.008,.012):FLinearColor::White;
    float NaturalWidth=FMath::Max(Measure(Name,15)+(Resources?40:0)+BadgeWidth+8,LabelWidth+12+ValueWidth);
    const float Width=FMath::Clamp(NaturalWidth+18,220.f,MaxWidth),ContentWidth=Width-18;
    LabelWidth=FMath::Min(LabelWidth+1,ContentWidth-FMath::Clamp(ValueWidth+8,40.f,ContentWidth*.65f));
    auto T=[&](const FString& Value,int32 Size=11,FLinearColor Color=FLinearColor(.9,.91,.93))
        {return Text(Value,Size,Color,ContentWidth);};
    TSharedRef<SVerticalBox> Rows=SNew(SVerticalBox);
    TSharedRef<SHorizontalBox> Header=SNew(SHorizontalBox);
    if(Resources)Header->AddSlot().AutoWidth().Padding(0,0,8,0)
        [SNew(SBox).WidthOverride(32).HeightOverride(32)[SNew(SACELootArt)
        .Background(State.Background.Get()).Foreground(State.Foreground.Get())]];
    Header->AddSlot().FillWidth(1).Padding(0,0,8,0)[Text(Name,15,FLinearColor(.95,.92,.85),ContentWidth-(Resources?40:0)-BadgeWidth-8,ETextJustify::Left,TEXT("ACELoot.Name"))];
    Header->AddSlot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(BadgeWidth)
        [SNew(SBorder).Tag(TEXT("ACELoot.QualityBadge")).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(BadgeFill).Padding(FMargin(8,4))
        [SNew(STextBlock).Tag(TEXT("ACELoot.QualityBadgeText")).Text(FText::FromString(Tier)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),12))
            .ColorAndOpacity(BadgeInk).Justification(ETextJustify::Center)]]];
    Rows->AddSlot().AutoHeight()[Header];
    for(const auto& Display:DisplayStats)
    {
        const auto& Line=*Display.Line;
        const auto* S=Display.bHasComparison?&Display.Comparison:nullptr;
        const bool InlineResistance=Line.Key.StartsWith(TEXT("Armor"))&&Line.Key!=TEXT("Armor");
        const float RowLabelWidth=InlineResistance?Measure(Line.Label+TEXT(":"))+1:LabelWidth;
        const float Gap=InlineResistance?4.f:8.f;
        TSharedRef<SHorizontalBox> Row=SNew(SHorizontalBox);
        Row->AddSlot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(RowLabelWidth)
            [Text(Line.Label+TEXT(":"),11,Line.bSpellModified?Line.Color:FLinearColor(.82,.83,.86),RowLabelWidth,ETextJustify::Left,FName(*(TEXT("ACELoot.Label.")+Line.Key)))]];
        Row->AddSlot().FillWidth(1).VAlign(VAlign_Top).Padding(Gap,0,0,0)
            [Text(Display.Value,11,Line.Key==TEXT("DamageType")?State.DamageColor:Line.bSpellModified?Line.Color:FLinearColor(.96,.95,.9),ContentWidth-RowLabelWidth-Gap,InlineResistance?ETextJustify::Left:ETextJustify::Right,FName(*(TEXT("ACELoot.Value.")+Line.Key)))];
        Rows->AddSlot().AutoHeight().Padding(0,.5f)[Row];
        if(!S)continue;
        auto Value=[&](double V){return ACELootQuality::FormatStat(V,S->bPercent);};
        if(S->bPermanentKnown&&(S->bOriginalKnown||S->bOriginalEstimated)&&FMath::Abs(S->Original-S->Permanent)>1.e-5)
        {
            const double Delta=S->Permanent-S->Original;
            const FString Change=S->bPercent?FString::Printf(TEXT("%+.1f pp"),Delta*100):FString::Printf(TEXT("%+.1f"),Delta);
            const FString Base=Line.Key.StartsWith(TEXT("Armor"))&&Line.Key!=TEXT("Armor")?TEXT("  Base resistance ×"):TEXT("  Base ");
            Rows->AddSlot().AutoHeight()[T(Base+(S->bOriginalEstimated?TEXT("≈ "):TEXT(""))+Value(S->Original)+TEXT(" · Tinkering ")+(S->bOriginalEstimated?TEXT("≈ "):TEXT(""))+Change,9,TinkerColor)];
        }
        if(!S->CantripPreview.IsEmpty())Rows->AddSlot().AutoHeight()[T(S->CantripPreview,9,CantripColor)];
        else if(S->bPermanentKnown&&!(Line.Key.StartsWith(TEXT("Armor"))&&Line.Key!=TEXT("Armor"))&&S->bPotential&&FMath::Abs(S->WithCantrip-S->Permanent)>1.e-5)
        {
            const double Delta=S->WithCantrip-S->Permanent;
            Rows->AddSlot().AutoHeight()[T(FString(TEXT("  Cantrip "))+(Delta>=0?TEXT("+"):TEXT(""))+Value(Delta),9,CantripColor)];
        }
    }
    if(!R.Properties.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,4,0,0)[T(FString::Join(R.Properties,TEXT(" · ")))];
    if(!R.SpellNames.IsEmpty())
    {
        Rows->AddSlot().AutoHeight().Padding(0,4,0,0)[T(TEXT("Spells:"))];
        Rows->AddSlot().AutoHeight().Padding(12,0,0,0)
            [SNew(SRichTextBlock).Tag(TEXT("ACELoot.Spells")).Text(FText::FromString(SpellMarkup(R)))
            .TextStyle(&SpellStyles().GetWidgetStyle<FTextBlockStyle>(TEXT("Body"))).DecoratorStyleSet(&SpellStyles())
            .WrapTextAt(ContentWidth-12).AutoWrapText(false).WrappingPolicy(ETextWrappingPolicy::DefaultWrapping)];
    }
    if(!R.Checks.IsEmpty())
    {
        Rows->AddSlot().AutoHeight().Padding(0,5,0,1)[T(TEXT("Requirements"),10,BaseColor)];
        float HaveWidth=56;
        for(const auto& Check:R.Checks)HaveWidth=FMath::Max(HaveWidth,Measure(Check.bKnown?TEXT("You: ")+Check.Have+TEXT(" ✓"):Check.Have,10));
        HaveWidth=FMath::Min(HaveWidth+1,ContentWidth*.44f);
        for(const auto& Check:R.Checks)
        {
            const FLinearColor Color=!Check.bKnown?FLinearColor(.9,.76,.48):Check.bMet?FLinearColor(.7,.86,.72):FLinearColor(1,.55,.55);
            auto Row=SNew(SHorizontalBox);
            Row->AddSlot().FillWidth(1)[Text(Check.Label,10,BaseColor,ContentWidth-HaveWidth-6)];
            Row->AddSlot().AutoWidth().Padding(6,0,0,0)[SNew(SBox).WidthOverride(HaveWidth)
                [Text(Check.bKnown?TEXT("You: ")+Check.Have+(Check.bMet?TEXT(" ✓"):TEXT(" ✕")):Check.Have,10,Color,HaveWidth,ETextJustify::Right)]];
            Rows->AddSlot().AutoHeight()[Row];
        }
    }
    else
    {
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
    FString Requirements=Checks.IsEmpty()?FString():FString(ACELootQuality::IsSummoningEssence(Item)?TEXT("Use: "):TEXT("Wield: "))+FString::Join(Checks,TEXT(" · "));
    if(!R.Activation.IsEmpty()&&!R.Activation.StartsWith(TEXT("No Arcane Lore")))
    {
        if(!Requirements.IsEmpty())Requirements+=TEXT(" · ");
        Requirements+=R.Activation;
    }
    if(!Requirements.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,4,0,0)[T(Requirements,10,
        R.bCannotWield||R.bActivationBlocked?FLinearColor(1,.55,.55):FLinearColor(.7,.82,.7))];
    }
    if(R.bIdentified&&!R.bRated)Rows->AddSlot().AutoHeight().Padding(0,4,0,0)[T(ACELootQuality::UnratedReason(R),10,BaseColor)];
    const FString Basis=R.bEstimated?TEXT("Estimated score"):R.Class.Contains(TEXT("set package"))?TEXT("Set + spell score"):
        R.Class.Contains(TEXT("spell package"))&&R.Class.StartsWith(TEXT("Armor"))?TEXT("Spell package score"):TEXT("Quality score");
    auto Footer=SNew(SHorizontalBox);
    Footer->AddSlot().FillWidth(.6f)[Text(Basis,10,BaseColor,ContentWidth*.6f)];
    Footer->AddSlot().FillWidth(.4f)[Text(R.bRated?FString::Printf(TEXT("%.1f"),R.Points):FString(TEXT("—")),11,R.bRated?ACELootQuality::Color(R.Grade):BaseColor,ContentWidth*.4f,ETextJustify::Right,TEXT("ACELoot.Score"))];
    Rows->AddSlot().AutoHeight().Padding(0,6,0,0)[Footer];
    auto Content=SNew(SBox).WidthOverride(Width)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(.35,.38,.43)).Padding(1)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.018,.022,.033,.98)).Padding(8)[Rows]]];
    Content->SlatePrepass();Panel->State=MoveTemp(State);Panel->SetContent(Content);
    UACEHoverTooltipWidget::SetWidgetContentTooltip(Owner,FText::FromString(Name),Panel.ToSharedRef());
}
