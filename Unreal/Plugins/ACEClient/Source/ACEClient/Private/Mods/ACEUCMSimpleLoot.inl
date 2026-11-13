// Simple pickup goals share the full loot matcher and profile storage.
bool ShowAdvancedLoot=false;
TSet<FString> GoalTypes;
int32 GoalQuality=2,GoalIndex=INDEX_NONE,GoalWieldMin=270,GoalWieldMax=400;
FString GoalWieldMode=TEXT("any"),GoalUsability=TEXT("any"),GoalRatingMode=TEXT("current");
TSet<FString> GoalTargets;
bool ManagingLootTemplates=false;
FACELootTemplate TemplateDraft;
#include "ACEUCMLootTargets.inl"

static const TArray<TPair<FString,FString>>& GoalCategories()
{
    static const TArray<TPair<FString,FString>> Types={
        {TEXT("weapons"),TEXT("All weapons")},{TEXT("armor"),TEXT("Armor")},{TEXT("jewelry"),TEXT("Jewelry")},{TEXT("clothing"),TEXT("Clothing")},
        {TEXT("heavy"),TEXT("Heavy weapons")},{TEXT("light"),TEXT("Light weapons")},{TEXT("finesse"),TEXT("Finesse weapons")},{TEXT("twohand"),TEXT("Two-handed")},
        {TEXT("bow"),TEXT("Bows")},{TEXT("crossbow"),TEXT("Crossbows")},{TEXT("atlatl"),TEXT("Atlatls")},{TEXT("wand"),TEXT("Wands")},
        {TEXT("shield"),TEXT("Shields")},{TEXT("essence"),TEXT("Essences")}};
    return Types;
}
FString GoalTypeText()const
{
    TArray<FString> Labels;for(const auto& Type:GoalCategories())if(GoalTypes.Contains(Type.Key))Labels.Add(Type.Value);
    return Labels.IsEmpty()?TEXT("Choose item types"):FString::Join(Labels,TEXT(", "));
}
FString GoalWieldText()const
{
    if(GoalWieldMode==TEXT("none"))return TEXT("No skill requirement");
    if(GoalWieldMode==TEXT("exact"))return TEXT("Exactly");
    if(GoalWieldMode==TEXT("range"))return TEXT("Between");
    return TEXT("Any wield requirement");
}
void EditLootGoal(int32 Index)
{
    const auto Rules=CurrentLootRules();if(!Rules.IsValidIndex(Index))return;
    const auto R=Rules[Index]->AsObject();GoalIndex=Index;GoalTypes.Empty();
    const TArray<TSharedPtr<FJsonValue>>* Types=nullptr;
    if(R->TryGetArrayField(TEXT("item_categories"),Types))for(const auto& T:*Types)GoalTypes.Add(T->AsString());
    GoalRatingMode=Str(R,TEXT("rating_mode"),TEXT("current"));GoalTargets.Empty();
    if(R->TryGetArrayField(TEXT("rating_targets"),Types))for(const auto& T:*Types)GoalTargets.Add(T->AsString());
    GoalQuality=FMath::Clamp(int32(Num(R,TEXT("min_quality"),2)),0,5);
    GoalUsability=Str(R,TEXT("usability"),TEXT("any"));
    GoalWieldMode=Str(R,TEXT("wield_mode"),TEXT("any"));GoalWieldMin=Num(R,TEXT("wield_min"),270);GoalWieldMax=Num(R,TEXT("wield_max"),400);Rebuild();
}
void SaveLootGoal()
{
    if(GoalTypes.IsEmpty()){Host->Notice=TEXT("Choose at least one item type.");Rebuild();return;}
    if(GoalWieldMode==TEXT("range")&&GoalWieldMin>GoalWieldMax){Host->Notice=TEXT("The first wield value must not exceed the second.");Rebuild();return;}
    if(GoalRatingMode==TEXT("templates")&&GoalTargets.IsEmpty()){Host->Notice=TEXT("Choose at least one recipient template.");Rebuild();return;}
    auto Rules=CurrentLootRules();if(GoalIndex==INDEX_NONE&&Rules.Num()>=2048){Host->Notice=TEXT("Maximum 2048 loot rules.");return;}
    auto R=Rules.IsValidIndex(GoalIndex)?ACEUCMLootPresentation::Clone(Rules[GoalIndex]->AsObject()):MakeShared<FJsonObject>();
    R->SetBoolField(TEXT("quick_goal"),true);R->SetStringField(TEXT("action"),TEXT("keep"));R->SetNumberField(TEXT("min_quality"),GoalQuality);
    TArray<TSharedPtr<FJsonValue>> Types;for(const auto& Type:GoalCategories())if(GoalTypes.Contains(Type.Key))Types.Add(MakeShared<FJsonValueString>(Type.Key));
    R->SetStringField(TEXT("rating_mode"),GoalRatingMode);
    if(GoalRatingMode==TEXT("templates")){TArray<FString> Ids=GoalTargets.Array();Ids.Sort();TArray<TSharedPtr<FJsonValue>> Targets;for(const auto& Id:Ids)Targets.Add(MakeShared<FJsonValueString>(Id));R->SetArrayField(TEXT("rating_targets"),Targets);}
    else R->RemoveField(TEXT("rating_targets"));
    R->SetStringField(TEXT("usability"),GoalRatingMode==TEXT("current")?GoalUsability:FString(TEXT("any")));
    R->SetArrayField(TEXT("item_categories"),Types);R->SetStringField(TEXT("wield_mode"),GoalWieldMode);
    R->SetNumberField(TEXT("wield_min"),GoalWieldMin);R->SetNumberField(TEXT("wield_max"),GoalWieldMax);
    // Older clients treat this retired predicate as false instead of picking up everything.
    auto Guard=MakeShared<FJsonObject>();Guard->SetStringField(TEXT("field"),TEXT("legacy"));Guard->SetNumberField(TEXT("key"),6);
    Guard->SetArrayField(TEXT("args"),{MakeShared<FJsonValueNumber>(0)});
    const TArray<TSharedPtr<FJsonValue>>* OldConditions=nullptr;TArray<TSharedPtr<FJsonValue>> Conditions;
    if(R->TryGetArrayField(TEXT("conditions"),OldConditions))Conditions=*OldConditions;
    if(!Conditions.ContainsByPredicate([](const auto& C){return Str(C->AsObject(),TEXT("field"))==TEXT("legacy")&&Num(C->AsObject(),TEXT("key"))==6;}))Conditions.Insert(MakeShared<FJsonValueObject>(Guard),0);
    R->SetArrayField(TEXT("conditions"),Conditions);
    FString Label=ACELootQuality::Name(static_cast<ACELootScoring::Grade>(GoalQuality))+TEXT("+ ")+GoalTypeText();
    if(GoalWieldMode==TEXT("none"))Label+=TEXT(" · no skill wield");
    else if(GoalWieldMode==TEXT("exact"))Label+=FString::Printf(TEXT(" · wield %d"),GoalWieldMin);
    else if(GoalWieldMode==TEXT("range"))Label+=FString::Printf(TEXT(" · wield %d–%d"),GoalWieldMin,GoalWieldMax);
    if(GoalRatingMode==TEXT("current")&&GoalUsability==TEXT("now"))Label+=TEXT(" · usable now");
    else if(GoalRatingMode==TEXT("current")&&GoalUsability==TEXT("close"))Label+=TEXT(" · usable or close");
    R->SetStringField(TEXT("label"),Label.Left(256));
    if(Rules.IsValidIndex(GoalIndex))Rules[GoalIndex]=MakeShared<FJsonValueObject>(R);else Rules.Insert(MakeShared<FJsonValueObject>(R),0);
    if(WriteLootRules(Rules)){GoalIndex=INDEX_NONE;Rebuild();}
}
void SimpleLootGoals()
{
    if(ManagingLootTemplates){LootTemplateEditor();return;}
    LootRecipientPicker();
    Add(Text(GoalIndex==INDEX_NONE?TEXT("Pick up"):TEXT("Edit pickup goal"),18));
    Add(SNew(SACEPluginComboButton).Tag(TEXT("UCMGoalQuality")).ComboButtonStyle(&FACEPluginStyle::Current()->Combo)
        .ButtonContent()[SNew(SACEPluginText).Text_Lambda([this]{return FText::FromString(ACELootQuality::Name(static_cast<ACELootScoring::Grade>(GoalQuality))+TEXT(" or better"));})
            .Font(FACEPluginStyle::NativeFont("Regular",16)).ColorAndOpacity_Lambda([this]{return ACELootQuality::Color(static_cast<ACELootScoring::Grade>(GoalQuality));})]
        .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>{auto Menu=SNew(SVerticalBox);for(int32 Q=0;Q<6;++Q){
            Menu->AddSlot().AutoHeight()[MenuButton(ACELootQuality::Name(static_cast<ACELootScoring::Grade>(Q))+TEXT(" or better"),[this,Q]{GoalQuality=Q;FSlateApplication::Get().DismissAllMenus();})];}return Menu;}));
    Add(SNew(SACEPluginComboButton).Tag(TEXT("UCMGoalTypes")).ComboButtonStyle(&FACEPluginStyle::Current()->Combo)
        .ButtonContent()[SNew(SACEPluginText).Text_Lambda([this]{return FText::FromString(GoalTypeText());}).Font(FACEPluginStyle::NativeFont("Regular",16)).ColorAndOpacity(Ink).AutoWrapText(true)]
        .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>{auto Menu=SNew(SVerticalBox);for(const auto& Type:GoalCategories())
            Menu->AddSlot().AutoHeight().Padding(5,2)[SNew(SCheckBox).Style(&FACEPluginStyle::Current()->Check).IsChecked_Lambda([this,Key=Type.Key]{return GoalTypes.Contains(Key)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this,Key=Type.Key](ECheckBoxState State){if(State==ECheckBoxState::Checked)GoalTypes.Add(Key);else GoalTypes.Remove(Key);})[Text(Type.Value,15)]];
            Menu->AddSlot().AutoHeight()[MenuButton(TEXT("Done"),[]{FSlateApplication::Get().DismissAllMenus();})];return Menu;}));
    if(GoalRatingMode==TEXT("current"))Add(SNew(SACEPluginComboButton).Tag(TEXT("UCMGoalUsability")).Visibility_Lambda([this]{return GoalRatingMode==TEXT("current")?EVisibility::Visible:EVisibility::Collapsed;}).ComboButtonStyle(&FACEPluginStyle::Current()->Combo)
        .ToolTipText(FText::FromString(TEXT("Close: at most 25 skill/Lore points, 10 attribute points or 5 levels short. Training and other gates must already be met. Quality stays independent.")))
        .ButtonContent()[LiveText([this]{return GoalUsability==TEXT("now")?FString(TEXT("Usable now")):GoalUsability==TEXT("close")?FString(TEXT("Usable now or close")):FString(TEXT("Any character requirements"));})]
        .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>{auto Menu=SNew(SVerticalBox);
            for(const FString Mode:{TEXT("any"),TEXT("now"),TEXT("close")}){
                const FString Label=Mode==TEXT("now")?TEXT("Usable now"):Mode==TEXT("close")?TEXT("Usable now or close"):TEXT("Any character requirements");
                Menu->AddSlot().AutoHeight()[MenuButton(Label,[this,Mode]{GoalUsability=Mode;FSlateApplication::Get().DismissAllMenus();})];}return Menu;}));
    Add(Text(TEXT("Wield"),14,Muted));
    Add(SNew(SACEPluginComboButton).Tag(TEXT("UCMGoalWield")).ComboButtonStyle(&FACEPluginStyle::Current()->Combo)
        .ToolTipText(FText::FromString(TEXT("Base skill wield requirement. Arcane Lore and character usability are separate.")))
        .ButtonContent()[LiveText([this]{return GoalWieldText();})]
        .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>{auto Menu=SNew(SVerticalBox);
            for(const FString Mode:{TEXT("any"),TEXT("none"),TEXT("exact"),TEXT("range")}){
                const FString Label=Mode==TEXT("any")?TEXT("Any wield requirement"):Mode==TEXT("none")?TEXT("No skill requirement"):Mode==TEXT("exact")?TEXT("Exactly"):TEXT("Between");
                Menu->AddSlot().AutoHeight()[MenuButton(Label,[this,Mode]{GoalWieldMode=Mode;FSlateApplication::Get().DismissAllMenus();Rebuild();})];}return Menu;}));
    if(GoalWieldMode==TEXT("exact")||GoalWieldMode==TEXT("range"))
    {
        auto Values=SNew(SHorizontalBox);
        Values->AddSlot().FillWidth(1)[SNew(SACEPluginSpinBox<int32>).Tag(TEXT("UCMGoalWieldMin")).Style(&LootSpinStyle).MinValue(0).MaxValue(10000).Delta(1).Value(GoalWieldMin).OnValueChanged_Lambda([this](int32 V){GoalWieldMin=V;})];
        if(GoalWieldMode==TEXT("range")){
            Values->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8,0)[Text(TEXT("to"),14,Muted)];
            Values->AddSlot().FillWidth(1)[SNew(SACEPluginSpinBox<int32>).Style(&LootSpinStyle).MinValue(0).MaxValue(10000).Delta(1).Value(GoalWieldMax).OnValueChanged_Lambda([this](int32 V){GoalWieldMax=V;})];}
        Add(Values);
    }
    Add(LootTool(GoalIndex==INDEX_NONE?TEXT("Add goal"):TEXT("Save goal"),[this]{SaveLootGoal();}));
    if(GoalIndex!=INDEX_NONE)Add(LootTool(TEXT("Cancel edit"),[this]{GoalIndex=INDEX_NONE;Rebuild();}));
    Add(Text(TEXT("Saved goals"),18));
    const auto Rules=CurrentLootRules();int32 Count=0,Advanced=0;
    for(int32 I=0;I<Rules.Num();++I){const auto R=Rules[I]->AsObject();if(!Bool(R,TEXT("quick_goal"))){++Advanced;continue;}++Count;
        auto Row=SNew(SHorizontalBox);
        Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)[SNew(SCheckBox).Style(&FACEPluginStyle::Current()->Check).IsChecked(Bool(R,TEXT("enabled"),true)?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this,I](ECheckBoxState State){auto All=CurrentLootRules();if(!All.IsValidIndex(I))return;auto Copy=ACEUCMLootPresentation::Clone(All[I]->AsObject());Copy->SetBoolField(TEXT("enabled"),State==ECheckBoxState::Checked);All[I]=MakeShared<FJsonValueObject>(Copy);WriteLootRules(All);})[Text(Str(R,TEXT("label"))+TEXT(" · ")+RecipientText(R),14)]];
        Row->AddSlot().AutoWidth()[LootTool(TEXT("Edit"),[this,I]{EditLootGoal(I);})];
        Row->AddSlot().AutoWidth()[LootTool(TEXT("Remove"),[this,I]{auto All=CurrentLootRules();if(All.IsValidIndex(I)){All.RemoveAt(I);GoalIndex=INDEX_NONE;WriteLootRules(All);}})];Add(Row);
    }
    if(!Count)Add(Text(TEXT("No pickup goals yet."),14,Muted));
    Add(Text(TEXT("Rates each selected recipient separately. Saving stops UCM; turn Looting on when ready."),12,Muted));
    Add(LootTool(Advanced?FString::Printf(TEXT("Advanced rules (%d)"),Advanced):TEXT("Advanced rules"),[this]{ShowAdvancedLoot=true;Rebuild();}));
}
