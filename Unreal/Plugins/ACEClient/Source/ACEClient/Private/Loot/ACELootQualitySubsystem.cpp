#include "ACELootQualitySubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Engine/GameInstance.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"

void UACELootQualitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UACEClientSubsystem>();
    Collection.InitializeDependency<UACEDatSubsystem>();
    auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    Client->OnAppraisalObserved.AddDynamic(this,&UACELootQualitySubsystem::ObserveAppraisal);
    Client->OnVitalsUpdated.AddDynamic(this,&UACELootQualitySubsystem::ObserveVitals);
    Client->OnItemRefreshed.AddDynamic(this,&UACELootQualitySubsystem::ObserveItemRefresh);
    Client->OnEnchantmentsChanged.AddDynamic(this,&UACELootQualitySubsystem::ObserveEnchantments);
    bInitialized=true;
}
void UACELootQualitySubsystem::Deinitialize()
{
    bInitialized=false;
    SaveOriginals();
    if(auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>())
    {
        C->OnAppraisalObserved.RemoveDynamic(this,&UACELootQualitySubsystem::ObserveAppraisal);
        C->OnVitalsUpdated.RemoveDynamic(this,&UACELootQualitySubsystem::ObserveVitals);
        C->OnItemRefreshed.RemoveDynamic(this,&UACELootQualitySubsystem::ObserveItemRefresh);
        C->OnEnchantmentsChanged.RemoveDynamic(this,&UACELootQualitySubsystem::ObserveEnchantments);
    }
    Super::Deinitialize();
}
UWorld* UACELootQualitySubsystem::GetTickableGameObjectWorld() const
{return GetGameInstance()?GetGameInstance()->GetWorld():nullptr;}
FString UACELootQualitySubsystem::CachePath() const
{
    FString Root=FPaths::ProjectSavedDir();
    if(auto* P=GetGameInstance()->GetSubsystem<UACEPluginSubsystem>())Root=P->UserDirectory();
    return Root/TEXT("LootOriginals")/(FMD5::HashAnsiString(*Server)+FString::Printf(TEXT(".%08X.json"),uint32(Player)));
}
void UACELootQualitySubsystem::CheckCharacter()
{
    const auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(!C)return;
    const auto S=C->GetSession();const int32 Id=C->GetPlayerGuid();
    const FString Name=S?S->GetServerName():FString();
    if(Id==Player&&Name==Server)return;
    SaveOriginals();Player=Id;Server=Name;Appraisals.Empty();Originals.Empty();Reviews.Empty();Items.Empty();Requested.Empty();Queue.Empty();
    bDirty=false;RefreshPending.Empty();++Revision;if(Player)LoadOriginals();
}
void UACELootQualitySubsystem::ObserveItemRefresh(int32 Guid)
{
    CheckCharacter();
    if(RefreshPending.Num()>=1024)RefreshPending.Empty();
    RefreshPending.Add(Guid);Appraisals.Remove(Guid);Reviews.Remove(Guid);Requested.Remove(Guid);
    if(Items.Contains(Guid)&&!Queue.Contains(Guid)&&Queue.Num()<128)Queue.Add(Guid);
    ++Revision;
}
void UACELootQualitySubsystem::ObserveVitals(const FACEPlayerVitals& V)
{
    uint64 H=GetTypeHash(V.Level);
    for(const auto& S:V.Skills)
    {
        H=HashCombine(H,GetTypeHash(S.SkillId));H=HashCombine(H,GetTypeHash(S.Base));
        H=HashCombine(H,GetTypeHash(S.Current));H=HashCombine(H,GetTypeHash(S.AdvancementClass));
    }
    for(int32 Id=1;Id<=6;++Id){H=HashCombine(H,GetTypeHash(V.GetAttributeBase(Id)));H=HashCombine(H,GetTypeHash(V.GetAttributeCurrent(Id)));}
    for(const auto& P:V.StatQualityInts){H=HashCombine(H,GetTypeHash(P.Key));H=HashCombine(H,GetTypeHash(P.Value));}
    for(const auto& P:V.QualityBools){H=HashCombine(H,GetTypeHash(P.Key));H=HashCombine(H,GetTypeHash(P.Value));}
    for(const auto& P:V.QualityInt64s){H=HashCombine(H,GetTypeHash(P.Key));H=HashCombine(H,GetTypeHash(P.Value));}
    H=HashCombine(H,GetTypeHash(V.HeritageGroup));
    if(auto S=GetGameInstance()->GetSubsystem<UACEClientSubsystem>()->GetSession())
    {
        TArray<FACEActiveEnchantment> Active;S->GetActiveEnchantments(Active);
        for(const auto& E:Active){H=HashCombine(H,GetTypeHash(E.SpellId));H=HashCombine(H,GetTypeHash(E.CasterGuid));}
    }
    if(H==ProfileFingerprint)return;ProfileFingerprint=H;Reviews.Empty();++Revision;
}
void UACELootQualitySubsystem::ObserveEnchantments()
{
    CheckCharacter();
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    // Ask the server for fresh reflected totals when equipped-item auras change.
    // Keep the last panel attached while its replacement appraisal is in flight.
    if(C&&Player)for(const auto& P:Items)
    {
        FACEWorldObject Item=P.Value,Fresh;
        if(C->GetWorldObject(P.Key,Fresh))Item=Fresh;
        if(Item.WielderId!=Player)continue;
        Requested.Remove(P.Key);
        if(!Queue.Contains(P.Key)&&Queue.Num()<128)Queue.Add(P.Key);
    }
    Reviews.Empty();++Revision;
}
void UACELootQualitySubsystem::ObserveAppraisal(const FACEAppraisalInfo& A)
{
    CheckCharacter();
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject Item;
    if(!C->GetWorldObject(A.ObjectGuid,Item))
    {const auto* Listed=Items.Find(A.ObjectGuid);if(!Listed)return;Item=*Listed;}
    if(Appraisals.Num()>=1024&&!Appraisals.Contains(A.ObjectGuid))
    {Appraisals.Empty();Items.Empty();Reviews.Empty();}
    Appraisals.Add(A.ObjectGuid,A);RefreshPending.Remove(A.ObjectGuid);Items.Add(Item.Guid,Item);Reviews.Empty();++Revision;
    if(!A.bSuccess)return;
    TArray<FACEActiveEnchantment> Active;if(auto S=C->GetSession())S->GetActiveEnchantments(Active);
    if(!Originals.Contains(Item.Guid))
    {
        FACEQualityOriginal Original;
        if(ACELootQuality::CaptureOriginal(Item,A,Active,GetGameInstance()->GetSubsystem<UACEDatSubsystem>(),Original))
        {
            if(Originals.Num()>=512)Originals.Remove(Originals.CreateConstIterator().Key());
            Originals.Add(Item.Guid,MoveTemp(Original));bDirty=true;
        }
    }
}
const FACEQualityReview& UACELootQualitySubsystem::Review(const FACEWorldObject& Item)
{
    CheckCharacter();
    if(Items.Num()>=1024&&!Items.Contains(Item.Guid)){Items.Empty();Reviews.Empty();Queue.Empty();Requested.Empty();}
    if(const auto* Old=Items.Find(Item.Guid);Old&&(Old->WeenieClassId!=Item.WeenieClassId||Old->Name!=Item.Name
        ||Old->SetupId!=Item.SetupId||Old->IconId!=Item.IconId))
    {Reviews.Remove(Item.Guid);Appraisals.Remove(Item.Guid);Originals.Remove(Item.Guid);bDirty=true;}
    Items.Add(Item.Guid,Item);
    if(const auto* Result=Reviews.Find(Item.Guid))return *Result;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    FACEAppraisalInfo Empty;Empty.ObjectGuid=Item.Guid;
    const auto* A=Appraisals.Find(Item.Guid);
    if(!A&&!RefreshPending.Contains(Item.Guid))
    {
        if(auto* P=GetGameInstance()->GetSubsystem<UACEPluginSubsystem>())
            if(const auto* Fresh=P->GetPluginAppraisals().Find(Item.Guid))
            {Appraisals.Add(Item.Guid,*Fresh);A=Appraisals.Find(Item.Guid);}
    }
    const bool Equipment=(Item.ItemType&(ACEItemType::MeleeWeapon|ACEItemType::MissileWeapon|ACEItemType::Caster))
        ||(Item.ValidLocations&&!(Item.ItemType&ACEItemType::Container));
    if(Equipment&&!A&&!Queue.Contains(Item.Guid)&&Queue.Num()<128)Queue.Add(Item.Guid);
    TArray<FACEActiveEnchantment> Active;if(auto S=C->GetSession())S->GetActiveEnchantments(Active);
    return Reviews.Add(Item.Guid,ACELootQuality::Evaluate(Item,A?*A:Empty,C->GetPlayerVitalsView(),Active,
        GetGameInstance()->GetSubsystem<UACEDatSubsystem>(),Originals.Find(Item.Guid)));
}
void UACELootQualitySubsystem::Tick(float)
{
    if(!bInitialized)return;
    CheckCharacter();const double Now=FPlatformTime::Seconds();
    if(bDirty&&Now>=NextSave){SaveOriginals();NextSave=Now+5;}
    if(!Player||Queue.IsEmpty()||Now<NextRequest)return;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(!C||!C->GetSession()||C->GetSession()->GetState()!=EACESessionState::InWorld)return;
    // Share the existing background appraisal path. One outstanding maintenance
    // request, no manual examination window, and no private-server connection.
    if(!C->BackgroundAppraisals.IsEmpty())return;
    const int32 Guid=Queue[0];Queue.RemoveAt(0);
    FACEWorldObject Item;if(!C->GetWorldObject(Guid,Item))
    {const auto* Listed=Items.Find(Guid);if(!Listed)return;Item=*Listed;}
    if(const auto* Last=Requested.Find(Guid);Last&&Now-*Last<30)return;
    Requested.Add(Guid,Now);NextRequest=Now+.5;C->RequestBackgroundAppraisal(Guid);
}
void UACELootQualitySubsystem::SaveOriginals()
{
    if(!bDirty||!Player)return;
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("version"),1);
    TArray<TSharedPtr<FJsonValue>> Entries;
    for(const auto& P:Originals)
    {
        auto J=MakeShared<FJsonObject>();const auto& O=P.Value;
        J->SetNumberField(TEXT("id"),uint32(P.Key));J->SetNumberField(TEXT("wcid"),O.WCID);
        J->SetNumberField(TEXT("setup"),O.Setup);J->SetNumberField(TEXT("icon"),O.Icon);J->SetStringField(TEXT("name"),O.Name);
        auto Stats=MakeShared<FJsonObject>();for(const auto& S:O.Stats)Stats->SetNumberField(S.Key,S.Value);
        J->SetObjectField(TEXT("stats"),Stats);Entries.Add(MakeShared<FJsonValueObject>(J));
    }
    Root->SetArrayField(TEXT("items"),Entries);FString Text;
    if(!FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text)))return;
    const FString Path=CachePath(),Temp=Path+TEXT(".tmp");IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    if(FFileHelper::SaveStringToFile(Text,*Temp)&&IFileManager::Get().Move(*Path,*Temp,true,true))bDirty=false;
}
void UACELootQualitySubsystem::LoadOriginals()
{
    const FString Path=CachePath();if(IFileManager::Get().FileSize(*Path)>1024*1024)return;
    FString Text;TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root)return;
    double Version=0;if(!Root->TryGetNumberField(TEXT("version"),Version)||Version!=1)return;
    const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;
    if(!Root->TryGetArrayField(TEXT("items"),Entries)||Entries->Num()>512)return;
    for(const auto& Entry:*Entries)
    {
        const auto J=Entry->AsObject();if(!J)continue;
        double Id=0,WCID=0,Setup=0,Icon=0;FACEQualityOriginal O;
        if(!J->TryGetNumberField(TEXT("id"),Id)||Id<=0||Id>MAX_uint32||!J->TryGetNumberField(TEXT("wcid"),WCID)
            ||!J->TryGetNumberField(TEXT("setup"),Setup)||!J->TryGetNumberField(TEXT("icon"),Icon)||!J->TryGetStringField(TEXT("name"),O.Name)
            ||O.Name.Len()>256)continue;
        const TSharedPtr<FJsonObject>* Stats=nullptr;if(!J->TryGetObjectField(TEXT("stats"),Stats)||(*Stats)->Values.Num()>12)continue;
        bool Valid=true;for(const auto& S:(*Stats)->Values)
        {double V=0;if(!S.Value->TryGetNumber(V)||!FMath::IsFinite(V)){Valid=false;break;}O.Stats.Add(FString(S.Key),V);}
        if(!Valid)continue;O.WCID=WCID;O.Setup=Setup;O.Icon=Icon;Originals.Add(int32(uint32(Id)),MoveTemp(O));
    }
}
