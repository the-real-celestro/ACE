#include "Knowledge/ACEWorldKnowledge.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

const FACEWorldKnowledge& FACEWorldKnowledge::Get()
{
    static const FACEWorldKnowledge Catalog=[]
    {
        FACEWorldKnowledge Result;FString Text,Error;
        const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("ACEClient"));
        if(Plugin && FFileHelper::LoadFileToString(Text,*(Plugin->GetBaseDir()/TEXT("ClientMods/knowledge/creatures.json"))))
            if(!Result.LoadJson(Text,Error))UE_LOG(LogTemp,Warning,TEXT("Creature reference unavailable: %s"),*Error);
        return Result;
    }();
    return Catalog;
}

bool FACEWorldKnowledge::LoadJson(const FString& Text,FString& Error)
{
    auto Fail=[&](const TCHAR* Message){Error=Message;return false;};
    if(Text.Len()>8*1024*1024)return Fail(TEXT("Catalog exceeds size limit"));
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root)return Fail(TEXT("Invalid catalog JSON"));
    double Schema=0;FString NewSource;
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Root->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1||!Root->TryGetStringField(TEXT("source"),NewSource)
        ||NewSource.IsEmpty()||NewSource.Len()>160||!Root->TryGetArrayField(TEXT("creatures"),Rows)||Rows->Num()>20000)
        return Fail(TEXT("Unsupported catalog schema"));
    auto Id=[](const TSharedPtr<FJsonObject>& O,const TCHAR* Key,uint32& Out)
    {double N=0;if(!O->TryGetNumberField(Key,N)||!FMath::IsFinite(N)||N<0||N>MAX_uint32||N!=FMath::FloorToDouble(N))return false;Out=uint32(N);return true;};
    TMap<uint32,FACECreatureKnowledge> Parsed;
    for(const auto& Row:*Rows)
    {
        if(Row->Type!=EJson::Object)return Fail(TEXT("Invalid creature record"));
        auto O=Row->AsObject();uint32 Wcid=0;FACECreatureKnowledge C;
        if(!Id(O,TEXT("wcid"),Wcid)||!Wcid||Parsed.Contains(Wcid)||!O->TryGetStringField(TEXT("name"),C.Name)
            ||C.Name.Len()>256||!Id(O,TEXT("melee_types"),C.MeleeTypes))return Fail(TEXT("Invalid creature identity"));
        if(O->HasField(TEXT("missile_types")))
        {uint32 Types=0;if(!Id(O,TEXT("missile_types"),Types))return Fail(TEXT("Invalid missile damage types"));C.MissileTypes=Types;}
        const TSharedPtr<FJsonObject>* Resists=nullptr;
        if(!O->TryGetObjectField(TEXT("resistances"),Resists)||(*Resists)->Values.Num()>8)return Fail(TEXT("Invalid resistance table"));
        for(const auto& R:(*Resists)->Values)
        {
            const uint32 Key=uint32(FCString::Atoi(*R.Key));double Value=0;
            if(FString::FromInt(Key)!=*R.Key||(!(Key>=64&&Key<=70)&&Key!=166))return Fail(TEXT("Unsupported resistance key"));
            if(!R.Value->TryGetNumber(Value)||!FMath::IsFinite(Value)||Value<0||Value>1000000)return Fail(TEXT("Invalid resistance value"));
            C.Resistances.Add(Key,Value);
        }
        const TArray<TSharedPtr<FJsonValue>>* Spells=nullptr;const TArray<TSharedPtr<FJsonValue>>* Drops=nullptr;
        if(!O->TryGetArrayField(TEXT("spells"),Spells)||Spells->Num()>256||!O->TryGetArrayField(TEXT("drops"),Drops)||Drops->Num()>256)return Fail(TEXT("Creature list exceeds bounds"));
        for(const auto& V:*Spells)
        {double N=0;if(!V->TryGetNumber(N)||!FMath::IsFinite(N)||N<1||N>MAX_uint32||N!=FMath::FloorToDouble(N))return Fail(TEXT("Invalid spell ID"));C.Spells.AddUnique(uint32(N));}
        for(const auto& V:*Drops)
        {
            if(V->Type!=EJson::Object)return Fail(TEXT("Invalid drop"));
            auto D=V->AsObject();FACEKnowledgeDrop Drop;
            if(!Id(D,TEXT("wcid"),Drop.Wcid)||!Drop.Wcid||!D->TryGetStringField(TEXT("name"),Drop.Name)||Drop.Name.Len()>256)return Fail(TEXT("Invalid drop identity"));
            if(D->HasField(TEXT("icon")) && (!Id(D,TEXT("icon"),Drop.IconDid)||(Drop.IconDid&&((Drop.IconDid>>24)!=6))))return Fail(TEXT("Invalid drop icon"));
            if(D->HasField(TEXT("chance")))
            {double N=0;if(!D->TryGetNumberField(TEXT("chance"),N)||!FMath::IsFinite(N)||N<0||N>1)return Fail(TEXT("Invalid drop chance"));Drop.BaselineChance=N;}
            C.Drops.Add(MoveTemp(Drop));
        }
        Parsed.Add(Wcid,MoveTemp(C));
    }
    Source=MoveTemp(NewSource);Creatures=MoveTemp(Parsed);Error.Reset();return true;
}
