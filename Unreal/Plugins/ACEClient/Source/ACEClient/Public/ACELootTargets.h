#pragma once
#include "CoreMinimal.h"
#include "ACETypes.h"
#include "ACELootScoring.h"
#include "Dom/JsonObject.h"

/** Quality preferences, deliberately independent of the looter's wield limits. */
struct FACELootTemplate
{
    FString Id,Name;
    int32 Primary=0;
    TArray<int32> Skills;
    FString Signature()const;
    FACEPlayerVitals Character()const;
};
struct FACELootTargetScore
{
    bool bRated=false,bEstimated=false,bPending=false;
    double Points=0;
    ACELootScoring::Grade Grade=ACELootScoring::Grade::Poor;
};
namespace ACELootTargets
{
    ACECLIENT_API const TArray<FACELootTemplate>& Presets();
    ACECLIENT_API bool Resolve(const FString& Id,const TSharedPtr<FJsonObject>& Profile,FACELootTemplate& Out);
    ACECLIENT_API bool Validate(const TSharedPtr<FJsonObject>& Profile);
    ACECLIENT_API TSharedPtr<FJsonObject> ToJson(const FACELootTemplate& Template);
}
