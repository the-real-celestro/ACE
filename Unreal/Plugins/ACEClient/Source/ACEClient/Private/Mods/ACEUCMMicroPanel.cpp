#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "Engine/GameInstance.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
    class SMicroDot : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SMicroDot) {} SLATE_ATTRIBUTE(bool, On) SLATE_END_ARGS()
        void Construct(const FArguments& Args) { On=Args._On; }
        virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16,16); }
        virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool) const override
        {
            static const FLinearColor Green(.02f,1.f,.12f);
            static const FSlateRoundedBoxBrush Dot(Green);
            TArray<FVector2D> Ring;
            for(int32 I=0;I<=32;++I)
            {
                const float Angle=2.f*PI*I/32.f;
                Ring.Add(FVector2D(8+6.5f*FMath::Cos(Angle),8+6.5f*FMath::Sin(Angle)));
            }
            FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),Ring,ESlateDrawEffect::None,
                On.Get(false)?Green:FLinearColor(.72f,.75f,.68f),true,1.5f);
            if(On.Get(false))FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(8,8),FSlateLayoutTransform(FVector2D(4,4))),&Dot,ESlateDrawEffect::None,Green);
            return Layer+1;
        }
    private:
        TAttribute<bool> On;
    };
    // Independent checkbox semantics, with a round indicator drawn at native resolution.
    const FCheckBoxStyle* MicroIndicatorStyle()
    {
        static const FCheckBoxStyle Style=[]
        {
            FCheckBoxStyle S;
            S.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
                .SetUncheckedImage(FSlateNoResource()).SetUncheckedHoveredImage(FSlateNoResource()).SetUncheckedPressedImage(FSlateNoResource())
                .SetCheckedImage(FSlateNoResource()).SetCheckedHoveredImage(FSlateNoResource()).SetCheckedPressedImage(FSlateNoResource())
                .SetPadding(FMargin(0));
            return S;
        }();
        return &Style;
    }
    TSharedRef<SWidget> MicroLabel(const TCHAR* Label,TFunction<bool()> Read)
    {
        return SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)[SNew(SMicroDot).On_Lambda([Read](){return Read();})]
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SACEPluginText).Text(FText::FromString(Label)).AutoWrapText(true).Font(FACEPluginStyle::NativeFont("Regular",14))];
    }

    class SMicroActivities : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SMicroActivities) {} SLATE_ATTRIBUTE(bool, Condensed) SLATE_ATTRIBUTE(float, AvailableWidth) SLATE_END_ARGS()
        void Construct(const FArguments& Args)
        { Condensed=Args._Condensed; AvailableWidth=Args._AvailableWidth; ChildSlot[SAssignNew(Grid,SUniformGridPanel).SlotPadding(FMargin(2))]; }
        bool IsSingleColumn() const { return Columns==1; }
        bool IsCondensed() const { return IsSingleColumn()&&Condensed.Get(false); }
        void Add(const TSharedRef<SWidget>& Widget)
        { Activities.Add(Widget); Reflow(); }
        virtual void Tick(const FGeometry& Geometry,double Time,float Delta) override
        {
            SCompoundWidget::Tick(Geometry,Time,Delta);
            // Base columns on the panel width, so a scrollbar appearing/disappearing
            // cannot change compact mode or disagree with the native window height.
            const float PanelWidth=AvailableWidth.Get(0);
            const int32 Wanted=(PanelWidth>0?PanelWidth:Geometry.GetLocalSize().X)<300.f?1:2;
            if (Wanted!=Columns || LastCondensed!=(Wanted==1&&Condensed.Get(false))) { Columns=Wanted; Reflow(); }
        }
    private:
        void Reflow()
        {
            Grid->ClearChildren(); LastCondensed=IsCondensed();
            const int32 Count=LastCondensed?FMath::Min(6,Activities.Num()):Activities.Num();
            for (int32 I=0;I<Count;++I)
                Grid->AddSlot(I%Columns,I/Columns).VAlign(VAlign_Center)[Activities[I]];
        }
        int32 Columns=2;
        bool LastCondensed=false;
        TAttribute<bool> Condensed;
        TAttribute<float> AvailableWidth;
        TSharedPtr<SUniformGridPanel> Grid;
        TArray<TSharedRef<SWidget>> Activities;
    };
}

bool UACEPluginSubsystem::IsUCMFollowEnabled() const
{
    auto P=Find(TEXT("ucm"));if(!P||!P->Profile)return false;
    bool Navigation=false;FString Name;double Id=0;
    P->Profile->TryGetBoolField(TEXT("navigation"),Navigation);
    P->Profile->TryGetStringField(TEXT("follow_name"),Name);P->Profile->TryGetNumberField(TEXT("follow_id"),Id);
    return Navigation&&(!Name.IsEmpty()||Id!=0);
}
bool UACEPluginSubsystem::SetUCMFollowSelected(bool Enabled)
{
    auto P=Find(TEXT("ucm"));if(!P||!P->Profile)return false;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject Target;
    if(Enabled&&(!C->GetWorldObject(C->GetSelectedObject().Guid,Target)||!Target.bIsPlayer
        ||Target.Guid==C->GetPlayerGuid()||!Target.bHasPosition))
    {P->Status=TEXT("Select another player to follow");return false;}
    const auto Previous=MakeShared<FJsonObject>(*P->Profile);
    P->Profile->SetStringField(TEXT("follow_name"),Enabled?Target.Name:FString());
    P->Profile->SetNumberField(TEXT("follow_id"),Enabled?uint32(Target.Guid):0);
    // Stopping Follow must not unexpectedly begin walking an old recorded route.
    P->Profile->SetBoolField(TEXT("navigation"),Enabled);
    if(!SaveCurrentProfile(P->Id,false))
    {P->Profile=Previous;P->Status=TEXT("Could not save follow settings: ")+Notice;return false;}
    FollowMovementSamples.Reset();FollowMovementTarget=0;bFollowMovementOverflow=false;
    if(MovementOwner==P->Id){MovementOwner.Empty();MoveExpires=0;FaceHeading.Reset();}
    P->NextDecision=0;
    P->Status=Enabled?(P->Running?TEXT("Following: "):TEXT("Start UCM to follow: "))+Target.Name:TEXT("Follow stopped; navigation disabled");
    ScheduleDecisionWake(FPlatformTime::Seconds());
    return true;
}
void UACEPluginSubsystem::ObserveFollowPosition(int32 Guid,const FACEPosition& Position)
{
    auto P=Find(TEXT("ucm"));bool Corners=false;
    if(!P||!P->Running||!IsUCMFollowEnabled()||!P->Profile->TryGetBoolField(TEXT("follow_corners"),Corners)||!Corners)return;
    FString Name;double Id=0;P->Profile->TryGetStringField(TEXT("follow_name"),Name);P->Profile->TryGetNumberField(TEXT("follow_id"),Id);
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject Target;
    if(!C->GetWorldObject(Guid,Target)||!Target.bIsPlayer||(!Name.IsEmpty()?Target.Name!=Name:uint32(Guid)!=uint32(Id)))return;
    if(FollowMovementTarget!=Guid||FollowMovementTeleport!=C->GetTeleportSeq())
    {FollowMovementSamples.Reset();bFollowMovementOverflow=false;}
    FollowMovementTarget=Guid;FollowMovementTeleport=C->GetTeleportSeq();
    if(!FollowMovementSamples.IsEmpty()&&FVector::DistSquared(FollowMovementSamples.Last().ToUnrealLocation(),Position.ToUnrealLocation())<FMath::Square(9.6))return;
    if(FollowMovementSamples.Num()>=512){bFollowMovementOverflow=true;return;}
    FollowMovementSamples.Add(Position);
}
FString UACEPluginSubsystem::UCMCurrentStatus() const
{
    auto P=Find(TEXT("ucm"));if(!P)return TEXT("UCM unavailable");
    if(!P->Running)return P->Status;
    for(const TCHAR* Id:{TEXT("itemtools"),TEXT("autocraft")})if(auto Other=Find(Id);Other&&Other->Running)
        return TEXT("Paused while ")+Other->Name+TEXT(" runs");
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto S=C->GetSession();
    const auto Elapsed=[](double Start){return FString::Printf(TEXT(" (%ds)"),FMath::Max(0,int32(FPlatformTime::Seconds()-Start)));};
    if(S&&S->GetPendingEquipmentGuid())
    {
        FACEWorldObject Item;C->GetWorldObject(S->GetPendingEquipmentGuid(),Item);
        return TEXT("Switching equipment: ")+Item.Name;
    }
    if(InventoryInteractionPending())return TEXT("Waiting for inventory transfer");
    if(PendingSpell)
    {
        FString Name;uint32 Icon=0;GetGameInstance()->GetSubsystem<UACEDatSubsystem>()->TryGetSpellInfo(PendingSpell,Name,Icon);
        if(Name.IsEmpty())Name=FString::Printf(TEXT("spell %d"),PendingSpell);
        return (PendingSpellActivity==TEXT("buffs")||PendingSpellActivity==TEXT("buff_others")?FString(TEXT("Buffing: ")):FString(TEXT("Casting: ")))
            +Name+TEXT("; awaiting result")+Elapsed(PendingSpellAt);
    }
    if(ConfirmedBuffAckAt&&DeferredBuffIntent)return TEXT("Waiting for final buff acknowledgment")+Elapsed(ConfirmedBuffAckAt);
    if(P->CombatTransitionIntent)return FString(P->WaitingAttackCancel?TEXT("Waiting for attack to finish"):TEXT("Waiting for combat stance"))+Elapsed(P->CombatTransitionStarted);
    if(C->IsUseBusy())return TEXT("Waiting for game action acknowledgment");
    if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
    {
        if(PC->IsWorldTransitionActive())return TEXT("Waiting for world transition");
        if(PC->IsUseApproachActive())return TEXT("Approaching interaction target");
    }
    return P->Status;
}

TSharedRef<SWidget> UACEPluginSubsystem::MakeUCMMicroPanel()
{
    FACEPluginStyle::Use(this,TEXT("ucmmicro"));
    const TWeakObjectPtr<UACEPluginSubsystem> Host(this);
    const auto UCM=[Host]() -> TSharedPtr<FACEClientPlugin>
    {if(Host.IsValid())for(auto P:Host->Plugins)if(P->Id==TEXT("ucm"))return P;return nullptr;};
    // The shared plugin window owns the backing and its opacity.
    auto Root=SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush")).Padding(6);
    auto Rows=SNew(SVerticalBox);
    auto Toggles=SNew(SMicroActivities).AvailableWidth_Lambda([Root=TWeakPtr<SBorder>(Root)](){auto Panel=Root.Pin();return Panel?float(Panel->GetCachedGeometry().GetLocalSize().X)-12.f:0.f;}).Condensed_Lambda([Host](){return Host.IsValid()&&Host->LedgerOption(TEXT("ucmmicro"),TEXT("condensed"));});
    const auto Row=[Toggles](const TCHAR* Label,TFunction<bool()> Read,TFunction<void(bool)> Write)
    {
        Toggles->Add(SNew(SCheckBox).Tag(FName(*(FString(TEXT("MicroToggle."))+Label))).Style(MicroIndicatorStyle()).IsFocusable(false).ClickMethod(EButtonClickMethod::MouseDown)
            .IsChecked_Lambda([Read](){return Read()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([Write](ECheckBoxState State){Write(State==ECheckBoxState::Checked);})
            [MicroLabel(Label,Read)]);
    };
    auto Header=SNew(SHorizontalBox);
    Header->AddSlot().FillWidth(1).VAlign(VAlign_Center)[SNew(SCheckBox).Tag(TEXT("MicroRun")).Style(MicroIndicatorStyle()).IsFocusable(false).ClickMethod(EButtonClickMethod::MouseDown)
        .ToolTip(FACEPluginStyle::Tooltip(TAttribute<FText>::CreateLambda([Host](){return FText::FromString(TEXT("UCM on/off. ")+(Host.IsValid()?Host->UCMCurrentStatus():FString()));})))
        .IsChecked_Lambda([UCM](){auto P=UCM();return P&&P->Running?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
        .OnCheckStateChanged_Lambda([Host,UCM](ECheckBoxState State)
        {
            if(Host.IsValid())if(auto P=UCM())
            {
                if(State==ECheckBoxState::Checked&&!P->Running)Host->Start(P->Id);
                else if(State==ECheckBoxState::Unchecked&&P->Running)Host->Stop(P->Id,TEXT("Stopped"),true);
            }
        })[MicroLabel(TEXT("On"),[UCM](){auto P=UCM();return P&&P->Running;})]];
    Header->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SACEPluginButton).Tag(TEXT("MicroCompact"))
        .Visibility_Lambda([Toggles](){return Toggles->IsSingleColumn()?EVisibility::Visible:EVisibility::Collapsed;})
        .ButtonStyle(&FACEPluginStyle::Current()->DockButton).ContentPadding(FMargin(4,1)).IsFocusable(false)
        .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Less: show six primary activities and shrink the window. More: restore all controls. Hidden activities keep their current settings."))))
        .OnClicked_Lambda([Host](){if(Host.IsValid())Host->SetLedgerOption(TEXT("ucmmicro"),TEXT("condensed"),!Host->LedgerOption(TEXT("ucmmicro"),TEXT("condensed")));return FReply::Handled();})
        [SNew(SACEPluginText).Font(FACEPluginStyle::NativeFont("Regular",13)).Text_Lambda([Toggles](){return FText::FromString(Toggles->IsCondensed()?TEXT("More"):TEXT("Less"));})]];
    Rows->AddSlot().AutoHeight().Padding(2,2)[Header];
    auto Quick=SNew(SVerticalBox).Tag(TEXT("MicroQuickControls"));
    const auto QuickCheck=[&](const TCHAR* Key,const TCHAR* Label,const TCHAR* Short,const TCHAR* Help,TFunction<bool()> Read,TFunction<void(bool)> Write,TAttribute<bool> Enabled)
    {
        Quick->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0,1)[SNew(SCheckBox).Tag(FName(*(FString(TEXT("MicroQuick."))+Key)))
            .Style(&FACEPluginStyle::Current()->Check).IsFocusable(false).IsEnabled(Enabled)
            .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(Help)))
            .IsChecked_Lambda([Read]{return Read()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([Write](ECheckBoxState V){Write(V==ECheckBoxState::Checked);})
            [SNew(SACEPluginText).Font(FACEPluginStyle::NativeFont("Regular",13)).AutoWrapText(false)
                .Text_Lambda([Root=TWeakPtr<SBorder>(Root),Label,Short]{auto P=Root.Pin();return FText::FromString(P&&P->GetCachedGeometry().GetLocalSize().X>=330?Label:Short);})]];
    };
    QuickCheck(TEXT("ForceBuff"),TEXT("Force Buff"),TEXT("F"),TEXT("Force Buff: check to refresh enabled buffs. Uncheck to cancel after the current cast."),
        [Host]{return Host.IsValid()&&Host->IsForceBuffRequested();},
        [Host](bool On){if(Host.IsValid()){if(On)Host->RequestForceBuff();else Host->CancelForceBuff();}},true);
    QuickCheck(TEXT("FollowCharacter"),TEXT("Follow Character"),TEXT("FC"),TEXT("Follow Character: select another player, check Follow, then turn On."),
        [Host]{return Host.IsValid()&&Host->IsUCMFollowEnabled();},
        [Host](bool On){if(Host.IsValid())Host->SetUCMFollowSelected(On);},true);
    QuickCheck(TEXT("CancelForce"),TEXT("Cancel Force"),TEXT("CF"),TEXT("Check to cancel forced buffing after the current cast. This action resets immediately."),
        []{return false;},[Host](bool On){if(On&&Host.IsValid())Host->CancelForceBuff();},
        TAttribute<bool>::CreateLambda([Host]{return Host.IsValid()&&Host->IsForceBuffRequested();}));
    QuickCheck(TEXT("Corners"),TEXT("Corners"),TEXT("C"),TEXT("Follow the player's observed footsteps rather than heading directly toward them."),
        [UCM]{auto P=UCM();bool On=false;if(P)P->Profile->TryGetBoolField(TEXT("follow_corners"),On);return On;},
        [Host,UCM](bool On){if(auto P=UCM();P&&Host.IsValid()){P->Profile->SetBoolField(TEXT("follow_corners"),On);Host->SaveCurrentProfile(P->Id,false);}},true);
    Rows->AddSlot().AutoHeight()[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Top)[Toggles]
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(4,0,0,0)[SNew(SBox)
            .WidthOverride_Lambda([Root=TWeakPtr<SBorder>(Root)]{auto P=Root.Pin();return P&&P->GetCachedGeometry().GetLocalSize().X>=330?140.f:44.f;})[Quick]]];
    for(const auto& Entry:TArray<TPair<FString,FString>>{
        {TEXT("meta_enabled"),TEXT("Metas")},{TEXT("buffing"),TEXT("Buffing")},
        {TEXT("combat"),TEXT("Combat")},{TEXT("looting"),TEXT("Looting")},
        {TEXT("navigation"),TEXT("Navigation")},{TEXT("summon_pets"),TEXT("Summoning")},
        {TEXT("recovery"),TEXT("Vital recovery")},{TEXT("buff_others"),TEXT("Buff others")},
        {TEXT("vendor_restock"),TEXT("Vendor restocking")},{TEXT("idle_peace"),TEXT("Peace when idle")}})
    {
        const FString Key=Entry.Key;
        Row(*Entry.Value,[UCM,Key]()
        {
            auto P=UCM();if(!P)return false;
            if(Key==TEXT("combat")){FString Mode;P->Profile->TryGetStringField(Key,Mode);return !Mode.IsEmpty()&&Mode!=TEXT("off");}
            bool Value=Key==TEXT("recovery")||Key==TEXT("meta_enabled");P->Profile->TryGetBoolField(Key,Value);return Value;
        },[Host,UCM,Key](bool On)
        {
            if(!Host.IsValid())return;auto P=UCM();if(!P)return;
            if(Key==TEXT("combat")){P->Profile->SetStringField(Key,On?TEXT("auto"):TEXT("off"));P->Profile->SetBoolField(TEXT("manual_combat"),false);}
            else P->Profile->SetBoolField(Key,On);
            Host->SaveCurrentProfile(P->Id,false);
        });
    }
    Root->SetContent(SNew(SScrollBox).ScrollBarVisibility(EVisibility::Collapsed)+SScrollBox::Slot()[Rows]);
    return Root;
}
