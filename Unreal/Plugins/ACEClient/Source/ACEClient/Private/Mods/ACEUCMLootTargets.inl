// Members of the simple-goal editor. Saved with the shared Loot profile.
TArray<TSharedPtr<FJsonValue>> LootTemplates()const
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    return P()->TryGetArrayField(TEXT("loot_rating_templates"),Values)?*Values:TArray<TSharedPtr<FJsonValue>>{};
}
bool WriteLootTemplates(const TArray<TSharedPtr<FJsonValue>>& Values)
{
    auto Updated=MakeShared<FJsonObject>();Updated->Values=P()->Values;
    Updated->SetArrayField(TEXT("loot_rating_templates"),Values);Updated->SetBoolField(TEXT("loot_modified"),true);
    FString Json;FJsonSerializer::Serialize(Updated,TJsonWriterFactory<>::Create(&Json));
    if(!Host->SaveProfile(Plugin->Id,Plugin->ProfileName,Json))return false;
    LootSourceProfile=P();Rebuild();return true;
}
FString TargetSkillName(int32 Id)const
{
    if(Id<1)return TEXT("Choose a skill");
    FString Name;uint32 Icon=0;if(Host->GetGameInstance()->GetSubsystem<UACEDatSubsystem>()->TryGetSkillInfo(Id,Name,Icon))return Name;
    return FString::Printf(TEXT("Skill %d"),Id);
}
FString RecipientText(const TSharedPtr<FJsonObject>& Rule)const
{
    const FString Mode=Str(Rule,TEXT("rating_mode"),TEXT("current"));
    if(Mode==TEXT("universal"))return TEXT("Universal / trade loot");
    if(Mode==TEXT("current"))return TEXT("Current character");
    const TArray<TSharedPtr<FJsonValue>>* Targets=nullptr;TArray<FString> Names;
    if(Rule->TryGetArrayField(TEXT("rating_targets"),Targets))for(const auto& Id:*Targets)
    {FACELootTemplate T;Names.Add(ACELootTargets::Resolve(Id->AsString(),P(),T)?T.Name:TEXT("Missing template"));}
    return Names.IsEmpty()?FString(TEXT("Choose templates")):FString::Join(Names,TEXT(", "));
}
FString GoalRecipientText()const
{
    auto Rule=MakeShared<FJsonObject>();Rule->SetStringField(TEXT("rating_mode"),GoalRatingMode);
    TArray<FString> Ids=GoalTargets.Array();Ids.Sort();TArray<TSharedPtr<FJsonValue>> Targets;for(const auto& Id:Ids)Targets.Add(MakeShared<FJsonValueString>(Id));
    Rule->SetArrayField(TEXT("rating_targets"),Targets);return RecipientText(Rule);
}
void LootRecipientPicker()
{
    Add(Text(TEXT("Rate for"),14,Muted));
    Add(SNew(SACEPluginComboButton).Tag(TEXT("UCMGoalRecipients")).ComboButtonStyle(&FACEPluginStyle::Current()->Combo)
        .ButtonContent()[LiveText([this]{return GoalRecipientText();})]
        .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>
        {
            auto Menu=SNew(SVerticalBox);
            for(const FString Mode:{TEXT("current"),TEXT("universal")})Menu->AddSlot().AutoHeight()[MenuButton(Mode==TEXT("current")?TEXT("Current character"):TEXT("Universal / trade loot"),[this,Mode]{GoalRatingMode=Mode;FSlateApplication::Get().DismissAllMenus();Rebuild();})];
            Menu->AddSlot().AutoHeight()[MenuButton(TEXT("Manage named templates"),[this]{ManagingLootTemplates=true;if(TemplateDraft.Skills.IsEmpty())TemplateDraft=ACELootTargets::Presets()[0];TemplateDraft.Id.Empty();TemplateDraft.Name.Empty();FSlateApplication::Get().DismissAllMenus();Rebuild();})];
            Menu->AddSlot().AutoHeight().Padding(5,5)[Text(TEXT("Or choose one or more templates"),14,Muted)];
            auto Choice=[&](const FACELootTemplate& T)
            {
                Menu->AddSlot().AutoHeight().Padding(5,2)[SNew(SCheckBox).Style(&FACEPluginStyle::Current()->Check)
                    .IsChecked_Lambda([this,Id=T.Id]{return GoalRatingMode==TEXT("templates")&&GoalTargets.Contains(Id)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([this,Id=T.Id](ECheckBoxState State){if(GoalRatingMode!=TEXT("templates"))GoalTargets.Empty();GoalRatingMode=TEXT("templates");if(State==ECheckBoxState::Checked)GoalTargets.Add(Id);else GoalTargets.Remove(Id);})[Text(T.Name,15)]];
            };
            for(const auto& V:LootTemplates()){FACELootTemplate T;if(ACELootTargets::Resolve(Str(V->AsObject(),TEXT("id")),P(),T))Choice(T);}
            for(const auto& T:ACELootTargets::Presets())Choice(T);
            Menu->AddSlot().AutoHeight()[MenuButton(TEXT("Done"),[this]{FSlateApplication::Get().DismissAllMenus();Rebuild();})];
            return SNew(SBox).MaxDesiredHeight(350)[SNew(SScrollBox)+SScrollBox::Slot()[Menu]];
        }));
    if(GoalRatingMode==TEXT("universal"))Add(Text(TEXT("Keeps good loot for any supported build, independent of your skills."),12,Muted));
    if(GoalRatingMode==TEXT("templates"))Add(Text(TEXT("Keep if it qualifies for any selected template. Your wield limits do not apply."),12,Muted));
}
void SaveLootTemplate()
{
    TemplateDraft.Name=TemplateDraft.Name.TrimStartAndEnd();
    if(TemplateDraft.Name.IsEmpty()||TemplateDraft.Name.Len()>64||TemplateDraft.Skills.IsEmpty()||!TemplateDraft.Skills.Contains(TemplateDraft.Primary))
    {Host->Notice=TEXT("Enter a name and choose a main trained skill.");Rebuild();return;}
    auto Values=LootTemplates();int32 Index=INDEX_NONE;
    for(int32 I=0;I<Values.Num();++I)
    {
        const auto T=Values[I]->AsObject();
        if(Str(T,TEXT("id"))==TemplateDraft.Id)Index=I;
        else if(Str(T,TEXT("name")).Equals(TemplateDraft.Name,ESearchCase::IgnoreCase)){Host->Notice=TEXT("That template name is already used.");Rebuild();return;}
    }
    if(Index==INDEX_NONE&&Values.Num()>=16){Host->Notice=TEXT("Maximum 16 named templates.");Rebuild();return;}
    if(Index==INDEX_NONE)TemplateDraft.Id=TEXT("custom:")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const auto Value=MakeShared<FJsonValueObject>(ACELootTargets::ToJson(TemplateDraft));
    if(Index==INDEX_NONE)Values.Add(Value);else Values[Index]=Value;
    if(WriteLootTemplates(Values)){if(GoalRatingMode!=TEXT("templates"))GoalTargets.Empty();GoalRatingMode=TEXT("templates");GoalTargets.Add(TemplateDraft.Id);ManagingLootTemplates=false;Rebuild();}
}
void LootTemplateEditor()
{
    Add(LootTool(TEXT("Back to pickup goals"),[this]{ManagingLootTemplates=false;Rebuild();}));
    Add(Text(TEXT("Named loot templates"),18));
    Add(Text(TEXT("Use these for friends, alts, or a different build. Shared with this Loot profile."),12,Muted));
    for(const auto& V:LootTemplates())
    {
        FACELootTemplate T;if(!ACELootTargets::Resolve(Str(V->AsObject(),TEXT("id")),P(),T))continue;
        auto Row=SNew(SHorizontalBox);Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Text(T.Name,15)];
        Row->AddSlot().AutoWidth()[LootTool(TEXT("Edit"),[this,T]{TemplateDraft=T;Rebuild();})];
        Row->AddSlot().AutoWidth()[LootTool(TEXT("Delete"),[this,Id=T.Id]
        {
            for(const auto& R:CurrentLootRules())
            {const TArray<TSharedPtr<FJsonValue>>* Targets=nullptr;if(R->AsObject()->TryGetArrayField(TEXT("rating_targets"),Targets))for(const auto& Target:*Targets)if(Target->AsString()==Id){Host->Notice=TEXT("This template is used by a pickup goal. Change that goal first.");Rebuild();return;}}
            auto Values=LootTemplates();Values.RemoveAll([&](const auto& V){return Str(V->AsObject(),TEXT("id"))==Id;});
            if(WriteLootTemplates(Values)){GoalTargets.Remove(Id);TemplateDraft=ACELootTargets::Presets()[0];TemplateDraft.Id.Empty();TemplateDraft.Name.Empty();Rebuild();}
        })];Add(Row);
    }
    Add(LootTool(TEXT("New template"),[this]{TemplateDraft=ACELootTargets::Presets()[0];TemplateDraft.Id.Empty();TemplateDraft.Name.Empty();Rebuild();}));
    Add(Text(TEXT("Template name"),14,Muted));
    Add(SNew(SACEPluginEditableTextBox).Tag(TEXT("UCMLootTemplateName")).Style(&LootEditStyle).HintText(FText::FromString(TEXT("Friend's mage, Archer alt…")))
        .Text_Lambda([this]{return FText::FromString(TemplateDraft.Name);}).OnTextChanged_Lambda([this](const FText& Text){TemplateDraft.Name=Text.ToString();}));
    Add(SNew(SACEPluginComboButton).ComboButtonStyle(&FACEPluginStyle::Current()->Combo).ButtonContent()[Text(TEXT("Start from a preset…"),15)]
        .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>{auto Menu=SNew(SVerticalBox);for(const auto& T:ACELootTargets::Presets())Menu->AddSlot().AutoHeight()[MenuButton(T.Name,[this,T]{TemplateDraft.Primary=T.Primary;TemplateDraft.Skills=T.Skills;FSlateApplication::Get().DismissAllMenus();Rebuild();})];return SNew(SBox).MaxDesiredHeight(300)[SNew(SScrollBox)+SScrollBox::Slot()[Menu]];}));
    Add(LootTool(TEXT("Copy my trained skills"),[this]
    {
        const auto& C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>()->GetPlayerVitalsView();
        if(!C.bValid){Host->Notice=TEXT("Enter the world to copy this character's trained skills.");Rebuild();return;}
        TemplateDraft.Skills.Empty();TemplateDraft.Primary=0;int32 Best=-1;
        for(const auto& S:C.Skills)if(S.AdvancementClass>=2&&S.SkillId>=1&&S.SkillId<=54)
        {TemplateDraft.Skills.AddUnique(S.SkillId);if(TArray<int32>{44,45,46,41,47,34,43,54}.Contains(S.SkillId)&&S.Base>Best){Best=S.Base;TemplateDraft.Primary=S.SkillId;}}
        if(!TemplateDraft.Primary&&!TemplateDraft.Skills.IsEmpty())TemplateDraft.Primary=TemplateDraft.Skills[0];
        if(TemplateDraft.Name.IsEmpty())TemplateDraft.Name=TEXT("My current template");Rebuild();
    }));
    Add(SNew(SACEPluginComboButton).Tag(TEXT("UCMLootTemplateSkills")).ComboButtonStyle(&FACEPluginStyle::Current()->Combo)
        .ButtonContent()[LiveText([this]{return FString::Printf(TEXT("Trained skills (%d)"),TemplateDraft.Skills.Num());})]
        .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>
        {
            auto Menu=SNew(SVerticalBox);auto* Dat=Host->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
            for(int32 Id=1;Id<=54;++Id){FString Name;uint32 Icon=0;if(!Dat->TryGetSkillInfo(Id,Name,Icon))continue;
                Menu->AddSlot().AutoHeight().Padding(5,2)[SNew(SCheckBox).Style(&FACEPluginStyle::Current()->Check).IsChecked_Lambda([this,Id]{return TemplateDraft.Skills.Contains(Id)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([this,Id](ECheckBoxState State){if(State==ECheckBoxState::Checked)TemplateDraft.Skills.AddUnique(Id);else TemplateDraft.Skills.Remove(Id);if(!TemplateDraft.Skills.Contains(TemplateDraft.Primary))TemplateDraft.Primary=TemplateDraft.Skills.IsEmpty()?0:TemplateDraft.Skills[0];})[Text(Name,15)]];}
            Menu->AddSlot().AutoHeight()[MenuButton(TEXT("Done"),[this]{FSlateApplication::Get().DismissAllMenus();Rebuild();})];return SNew(SBox).MaxDesiredHeight(330)[SNew(SScrollBox)+SScrollBox::Slot()[Menu]];
        }));
    Add(SNew(SACEPluginComboButton).ComboButtonStyle(&FACEPluginStyle::Current()->Combo).ButtonContent()[LiveText([this]{return TEXT("Main skill: ")+TargetSkillName(TemplateDraft.Primary);})]
        .OnGetMenuContent_Lambda([this]()->TSharedRef<SWidget>{auto Menu=SNew(SVerticalBox);for(int32 Id:TemplateDraft.Skills)Menu->AddSlot().AutoHeight()[MenuButton(TargetSkillName(Id),[this,Id]{TemplateDraft.Primary=Id;FSlateApplication::Get().DismissAllMenus();})];return SNew(SBox).MaxDesiredHeight(300)[SNew(SScrollBox)+SScrollBox::Slot()[Menu]];}));
    Add(LootTool(TEXT("Save template"),[this]{SaveLootTemplate();}));
}
