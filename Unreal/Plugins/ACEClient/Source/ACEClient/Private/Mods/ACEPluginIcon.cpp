#include "Mods/ACEPluginIcon.h"
#include "Rendering/DrawElements.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "ImageUtils.h"
#include "Engine/Texture2D.h"
#include "UObject/GCObject.h"

/** One atlas shared by every launcher, window title and plugin-manager badge. */
struct FACEPluginIconArtwork : public FGCObject
{
    TObjectPtr<UTexture2D> Texture=nullptr;
    TArray<FSlateBrush> Cells;
    FACEPluginIconArtwork()
    {
        if(!FApp::CanEverRender())return;
        const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("ACEClient"));
        if(!Plugin)return;
        TArray<uint8> Bytes;
        if(!FFileHelper::LoadFileToArray(Bytes,*(Plugin->GetBaseDir()/TEXT("Resources/Icons/PluginIcons.png"))))return;
        Texture=FImageUtils::ImportBufferAsTexture2D(Bytes);
        if(!Texture)return;
        Texture->LODGroup=TEXTUREGROUP_UI;
        Texture->Filter=TF_Bilinear;
        Texture->AddressX=TA_Clamp;Texture->AddressY=TA_Clamp;
        Texture->NeverStream=true;
        Texture->UpdateResource();
        for(int32 I=0;I<20;++I)
        {
            FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;
            Brush.SetResourceObject(Texture);Brush.ImageSize=FVector2D(32,32);
            const int32 X=I%5,Y=I/5;
            Brush.SetUVRegion(FBox2f(FVector2f(X/5.f,Y/4.f),FVector2f((X+1)/5.f,(Y+1)/4.f)));
            Cells.Add(MoveTemp(Brush));
        }
    }
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override {Collector.AddReferencedObject(Texture);}
    virtual FString GetReferencerName() const override {return TEXT("ACEPluginIconArtwork");}
    static TSharedRef<FACEPluginIconArtwork> Shared()
    {
        static TWeakPtr<FACEPluginIconArtwork> Cache;
        if(auto Existing=Cache.Pin())return Existing.ToSharedRef();
        auto Created=MakeShared<FACEPluginIconArtwork>();Cache=Created;return Created;
    }
};

int32 SACEPluginIcon::ArtworkSlot(const FString& Id)
{
    if(Id==TEXT("plugin.grip"))return INDEX_NONE;
    if(Id==TEXT("__manager")||Id==TEXT("mobile.plugins"))return 0;
    if(Id==TEXT("supplyledger.hyssop"))return 19;
    if(Id==TEXT("cosmetics")||Id.Contains(TEXT("wow"))||Id.Contains(TEXT("transmog")))return 17;
    const TCHAR* Ids[]={TEXT("plugin.options"),TEXT("actiondeck"),TEXT("autocraft"),TEXT("derethlens"),TEXT("huntledger"),
        TEXT("itemtools"),TEXT("looteditor"),TEXT("supplyledger"),TEXT("thirdeye"),TEXT("ucm"),
        TEXT("ucmmicro"),TEXT("waypoint"),TEXT("wiki"),TEXT("ucm.log"),TEXT("waypoint.map"),TEXT("treestats"),TEXT("foundry")};
    for(int32 I=0;I<UE_ARRAY_COUNT(Ids);++I)if(Id==Ids[I])return I;
    return 18; // An extension can still supply its own PNG through presentation.icon.
}

FLinearColor SACEPluginIcon::Accent(const FString& Id)
{
    // Color reinforces identity; every plugin also has a different silhouette.
    if(Id==TEXT("plugin.options")||Id==TEXT("__manager"))return FLinearColor(FColor(186,205,230));
    if(Id==TEXT("actiondeck"))return FLinearColor(FColor(255,207,87));
    if(Id==TEXT("autocraft"))return FLinearColor(FColor(102,229,170));
    if(Id==TEXT("derethlens"))return FLinearColor(FColor(105,191,255));
    if(Id==TEXT("huntledger"))return FLinearColor(FColor(255,177,93));
    if(Id==TEXT("itemtools"))return FLinearColor(FColor(108,216,232));
    if(Id==TEXT("looteditor"))return FLinearColor(FColor(217,164,255));
    if(Id==TEXT("supplyledger"))return FLinearColor(FColor(255,140,168));
    if(Id==TEXT("thirdeye"))return FLinearColor(FColor(157,176,255));
    if(Id==TEXT("ucm"))return FLinearColor(FColor(255,132,115));
    if(Id==TEXT("ucmmicro"))return FLinearColor(FColor(105,221,207));
    if(Id==TEXT("waypoint"))return FLinearColor(FColor(134,222,112));
    if(Id==TEXT("waypoint.map"))return FLinearColor(FColor(101,206,255));
    if(Id==TEXT("ucm.log"))return FLinearColor(FColor(242,208,150));
    if(Id==TEXT("wiki"))return FLinearColor(FColor(244,218,151));
    if(Id==TEXT("treestats"))return FLinearColor(FColor(111,222,177));
    if(Id==TEXT("foundry"))return FLinearColor(FColor(255,176,109));
    return FLinearColor(FColor(177,197,255));
}

void SACEPluginIcon::Construct(const FArguments& Args)
{
    SetCanTick(false);SetVisibility(EVisibility::HitTestInvisible);
    Paths.Reset();
    const FString& Id=Args._Id;
    ArtworkIndex=ArtworkSlot(Id);
    if(ArtworkIndex!=INDEX_NONE)Artwork=FACEPluginIconArtwork::Shared();
    const FLinearColor Color=Accent(Id),White(FColor(244,248,255));
    auto Line=[&](std::initializer_list<FVector2D> Points,FLinearColor Ink)
    {FPath P;P.Color=Ink;for(auto V:Points)P.Points.Add(V);Paths.Add(MoveTemp(P));};
    auto Fill=[&](std::initializer_list<FVector2D> Points,FLinearColor Ink)
    {Line(Points,Ink);Paths.Last().Filled=true;};
    auto Circle=[&](FVector2D Center,double Radius,FLinearColor Ink)
    {
        FPath P;P.Color=Ink;
        for(int32 I=0;I<=32;++I){const double A=I*PI/16;P.Points.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius);}
        Paths.Add(MoveTemp(P));
    };
    if(Id==TEXT("plugin.options")) // Familiar gear, with an uncluttered center.
    {
        FPath Gear;Gear.Color=Color;
        for(int32 I=0;I<=32;++I)
        {
            const double A=(I-.5)*PI/16,R=(I%4<2)?13.:10.;
            Gear.Points.Add(FVector2D(16+R*FMath::Cos(A),16+R*FMath::Sin(A)));
        }
        Paths.Add(MoveTemp(Gear));Circle({16,16},4,White);
    }
    else if(Id==TEXT("plugin.grip"))
    {
        for(int X:{10,22})for(int Y:{6,16,26})Fill({{X-2.,Y-2.},{X+2.,Y-2.},{X+2.,Y+2.},{X-2.,Y+2.}},Color);
    }
    else if(Id==TEXT("actiondeck")) // Four large hotbar keys; no dense 3x3 lattice.
    {
        for(int Y:{4,18})for(int X:{4,18})
            Line({{double(X),double(Y)},{X+10.,double(Y)},{X+10.,Y+10.},{double(X),Y+10.},{double(X),double(Y)}},Color);
        Fill({{21,20},{28,24},{21,28}},White);
    }
    else if(Id==TEXT("autocraft")) // Mortar and pestle, with one strong bowl silhouette.
    {
        Fill({{4,15},{28,15},{24,25},{20,28},{12,28},{8,25}},Color);
        Line({{4,14},{28,14}},White);
        Fill({{15,13},{23,3},{28,6},{21,13}},White);
        Line({{10,30},{22,30}},Color);
    }
    else if(Id==TEXT("derethlens")) // Large magnifying glass over a mountain.
    {
        Circle({13,13},10,Color);Line({{21,21},{29,29}},White);
        Line({{6,17},{12,8},{16,14},{19,11}},White);
    }
    else if(Id==TEXT("huntledger")) // Trophy for hunting gains.
    {
        Line({{8,4},{24,4},{23,16},{20,21},{12,21},{9,16},{8,4}},Color);
        Line({{8,7},{3,7},{3,12},{6,16},{10,16}},White);
        Line({{24,7},{29,7},{29,12},{26,16},{22,16}},White);
        Line({{16,21},{16,28},{9,28},{23,28}},Color);
    }
    else if(Id==TEXT("itemtools")) // Package, distinct from the loot chest.
    {
        Line({{4,9},{16,3},{28,9},{28,24},{16,30},{4,24},{4,9},{16,15},{28,9}},Color);
        Line({{16,15},{16,29}},White);Line({{10,6},{22,12},{22,18}},White);
    }
    else if(Id==TEXT("looteditor")) // Treasure chest with an unmistakable lock.
    {
        Line({{3,15},{3,9},{7,5},{25,5},{29,9},{29,15},{3,15},{3,28},{29,28},{29,15}},Color);
        Fill({{13,12},{19,12},{19,21},{13,21}},White);
    }
    else if(Id==TEXT("treestats")) // Allegiance branches and an upward update arrow.
    {
        const FLinearColor Gold=Color,Teal=Color,Paper=White;
        Line({{16,28},{16,7},{7,7},{7,14}},Gold);
        Line({{16,7},{25,7},{25,14}},Gold);
        Line({{7,14},{3,14},{3,21}},Teal);Line({{7,14},{11,14},{11,21}},Teal);
        Fill({{13,3},{19,3},{19,9},{13,9}},Paper);
        Fill({{22,12},{28,12},{28,18},{22,18}},Paper);
        Line({{23,28},{23,21},{19,25},{23,21},{27,25}},Teal);
    }
    else if(Id==TEXT("supplyledger")) // Prismatic Taper fallback, even if the painted atlas is missing.
    {
        Fill({{15,2},{18,7},{21,16},{24,29},{7,29},{11,16},{14,7}},White);
        Fill({{11,15},{20,13},{21,18},{10,20}},FLinearColor(FColor(88,180,255)));
        Fill({{10,20},{21,18},{22,23},{9,24}},FLinearColor(FColor(102,223,125)));
        Fill({{9,24},{22,23},{23,26},{8,27}},FLinearColor(FColor(255,218,88)));
        Fill({{8,27},{23,26},{24,29},{7,29}},FLinearColor(FColor(246,117,135)));
    }
    else if(Id==TEXT("thirdeye")) // Eye: a single white pupil against a colored iris.
    {
        Line({{2,16},{8,9},{16,6},{24,9},{30,16},{24,23},{16,26},{8,23},{2,16}},Color);
        Circle({16,16},6,Color);Fill({{14,13},{18,13},{18,19},{14,19}},White);
    }
    else if(Id==TEXT("ucm")) // Bold crossed swords, no central rune obscuring them.
    {
        Fill({{3,2},{9,5},{26,24},{23,27},{6,8}},Color);
        Fill({{29,2},{23,5},{6,24},{9,27},{26,8}},White);
        Line({{3,21},{11,29}},White);Line({{21,29},{29,21}},Color);
        Line({{6,26},{3,29}},White);Line({{26,26},{29,29}},Color);
    }
    else if(Id==TEXT("ucmmicro")) // Three high-contrast sliders.
    {
        for(int32 I=0;I<3;++I)
        {
            const double Y=6+I*10;Line({{3,Y},{29,Y}},Color);
            const double X=I==1?10:22;Fill({{X-3,Y-3},{X+3,Y-3},{X+3,Y+3},{X-3,Y+3}},White);
        }
    }
    else if(Id==TEXT("waypoint")) // Compass needle, not a tiny jagged trail.
    {
        Fill({{16,2},{27,29},{16,23}},Color);Fill({{16,2},{5,29},{16,23}},White);
    }
    else if(Id==TEXT("waypoint.map")) // Folded map.
    {
        Line({{3,6},{11,3},{21,7},{29,4},{29,26},{21,29},{11,25},{3,28},{3,6}},Color);
        Line({{11,4},{11,24}},White);Line({{21,8},{21,28}},White);
    }
    else if(Id==TEXT("ucm.log")) // Clipboard: separate silhouette from the encyclopedia.
    {
        Line({{10,5},{5,5},{5,29},{27,29},{27,5},{22,5}},Color);
        Line({{11,3},{21,3},{21,8},{11,8},{11,3}},White);
        Line({{10,14},{22,14}},White);Line({{10,20},{22,20}},White);Line({{10,25},{17,25}},Color);
    }
    else if(Id==TEXT("wiki")) // Open reference book; ACPedia previously used the generic diamond.
    {
        Line({{16,7},{11,4},{3,4},{3,26},{11,26},{16,29},{21,26},{29,26},{29,4},{21,4},{16,7},{16,29}},Color);
        Line({{7,10},{11,10}},White);Line({{7,16},{11,16}},White);
        Line({{21,10},{25,10}},White);Line({{21,16},{25,16}},White);
    }
    else if(Id==TEXT("foundry")) // Solid anvil.
    {
        Fill({{2,11},{30,11},{25,18},{7,18}},Color);
        Fill({{12,18},{20,18},{20,24},{12,24}},Color);
        Fill({{12,24},{20,24},{26,28},{6,28}},Color);
        Line({{7,7},{25,7}},White);
    }
    else if(Id.Contains(TEXT("wow"))||Id.Contains(TEXT("transmog")))
    {
        Line({{3,7},{16,4},{29,7},{25,23},{16,29},{7,23},{3,7}},Color);
        Line({{7,12},{12,15}},White);Line({{25,12},{20,15}},White);
    }
    else // A plug for extensions without custom artwork.
    {
        Line({{10,3},{10,10}},White);Line({{22,3},{22,10}},White);
        Line({{6,10},{26,10},{26,18},{21,24},{11,24},{6,18},{6,10}},Color);
        Line({{16,24},{16,30}},White);
    }
}
FSlateLayoutTransform SACEPluginIcon::Fit(const FVector2D& Size)
{
    const float Side=FMath::Max(0.,FMath::Min(Size.X,Size.Y));
    return FSlateLayoutTransform(Side/32.f,(Size-FVector2D(Side,Side))*.5);
}
const FSlateBrush* SACEPluginIcon::PaintedBrush() const
{
    return Artwork&&Artwork->Cells.IsValidIndex(ArtworkIndex)?&Artwork->Cells[ArtworkIndex]:nullptr;
}
int32 SACEPluginIcon::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const
{
    const auto Art=G.MakeChild(FVector2D(32,32),Fit(G.GetLocalSize()));
    const auto Effect=ShouldBeEnabled(Enabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect;
    if(const FSlateBrush* Brush=PaintedBrush())
    {
        FSlateDrawElement::MakeBox(Out,Layer,Art.ToPaintGeometry(),Brush,Effect,Style.GetColorAndOpacityTint());
        return Layer;
    }
    for(const auto& P:Paths)
    {
        const auto Color=P.Color*Style.GetColorAndOpacityTint();
        if(!P.Filled)FSlateDrawElement::MakeLines(Out,Layer,Art.ToPaintGeometry(),P.Points,Effect,Color,true,2.3f);
        else
        {
            TArray<FSlateVertex,TInlineAllocator<8>> Vertices;TArray<SlateIndex,TInlineAllocator<18>> Indices;
            for(auto V:P.Points)Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Art.GetAccumulatedRenderTransform(),FVector2f(V),FVector2f::ZeroVector,Color.ToFColor(true)));
            for(int32 I=1;I<P.Points.Num()-1;++I){Indices.Add(0);Indices.Add(I);Indices.Add(I+1);}
            FSlateDrawElement::MakeCustomVerts(Out,Layer,FSlateResourceHandle(),TArray<FSlateVertex>(Vertices),TArray<SlateIndex>(Indices),nullptr,0,0,Effect);
        }
    }
    return Layer;
}
