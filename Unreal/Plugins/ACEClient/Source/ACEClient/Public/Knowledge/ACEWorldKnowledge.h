#pragma once
#include "CoreMinimal.h"

struct FACEKnowledgeDrop
{
    uint32 Wcid=0;
    uint32 IconDid=0;
    FString Name;
    TOptional<double> BaselineChance;
};
struct FACECreatureKnowledge
{
    FString Name;
    uint32 MeleeTypes=0;
    TOptional<uint32> MissileTypes;
    TMap<uint32,double> Resistances;
    TArray<uint32> Spells;
    TArray<FACEKnowledgeDrop> Drops;
};

/** Read-only public ACE reference data. Never substitutes for live server facts. */
class ACECLIENT_API FACEWorldKnowledge
{
public:
    static const FACEWorldKnowledge& Get();
    bool LoadJson(const FString& Json,FString& Error);
    const FACECreatureKnowledge* Find(uint32 Wcid) const { return Creatures.Find(Wcid); }
    const FString& GetSource() const { return Source; }
private:
    FString Source;
    TMap<uint32,FACECreatureKnowledge> Creatures;
};
