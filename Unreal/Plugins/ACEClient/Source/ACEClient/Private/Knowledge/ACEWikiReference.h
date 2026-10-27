#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ACEWikiReference
{
enum class ESource : uint8 { ACPedia, Fandom };
struct FEntry { FString Title,Kind,Url; ESource Source=ESource::ACPedia; int32 PageId=0; };
struct FBlock { FString Text; bool Heading=false; };
struct FArticle { FString Title,Url; int64 Revision=0; TArray<FBlock> Blocks; TArray<FString> Links; bool Truncated=false; };
FString Endpoint(ESource Source);
FString SourceName(ESource Source);
FString ArticleUrl(ESource Source,const FString& Title);
FString SearchUrl(ESource Source,const FString& Query);
FString ParseUrl(const FEntry& Entry);
bool ParseSearch(const TSharedPtr<FJsonObject>& Root,ESource Source,TArray<FEntry>& Out);
bool ParseArticle(const TSharedPtr<FJsonObject>& Root,ESource Source,FArticle& Out);
void FormatHtml(const FString& Html,TArray<FBlock>& Blocks,TArray<FString>& Links,bool& Truncated);
}
