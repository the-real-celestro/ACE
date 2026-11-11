#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "ACELootQuality.h"
#include "ACELootQualitySubsystem.generated.h"

/** Owns observed appraisals and original rolls; never changes loot or equips items. */
UCLASS()
class ACECLIENT_API UACELootQualitySubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(ACELootQuality, STATGROUP_Tickables); }
    virtual bool IsTickable() const override { return bInitialized && !IsTemplate(); }
    virtual UWorld* GetTickableGameObjectWorld() const override;
    const FACEQualityReview& Review(const FACEWorldObject& Item);
    uint64 Revision = 1;
    UFUNCTION() void ObserveAppraisal(const FACEAppraisalInfo& Info);
    UFUNCTION() void ObserveVitals(const FACEPlayerVitals& Vitals);
    UFUNCTION() void ObserveItemRefresh(int32 Guid);
    UFUNCTION() void ObserveEnchantments();
private:
    void CheckCharacter();
    void SaveOriginals();
    void LoadOriginals();
    FString CachePath() const;
    TMap<int32, FACEAppraisalInfo> Appraisals;
    TMap<int32, FACEQualityOriginal> Originals;
    TMap<int32, FACEQualityReview> Reviews;
    TMap<int32, FACEWorldObject> Items;
    TMap<int32, double> Requested;
    TArray<int32> Queue;
    TSet<int32> RefreshPending;
    FString Server;
    int32 Player = 0;
    double NextRequest = 0, NextSave = 0;
    bool bDirty = false;
    uint64 ProfileFingerprint = 0;
    bool bInitialized = false;
};
