#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ACEWikiReference
{
enum class ESource : uint8 { ACPedia, Fandom };
struct FEntry { FString Title,Kind,Url; ESource Source=ESource::ACPedia; int32 PageId=0; };
struct FAttribute { FString Name,Value; };
enum class ELayout : uint8 { Text, Identity, Rewards, Trophies, Combat, Ratings };
struct FBlock {
    FString Text; bool Heading=false; FString ImageUrl; TArray<FAttribute> Attributes; bool Portrait=false;
    TArray<FString> Cells; int32 RowBlocks=0;
    ELayout Layout=ELayout::Text; TArray<FAttribute> Fields;
};
struct FArticle { FString Title,Url; int64 Revision=0; TArray<FBlock> Blocks; TArray<FString> Links; bool Truncated=false; };
FString Endpoint(ESource Source);
FString SourceName(ESource Source);
FString ArticleUrl(ESource Source,const FString& Title);
FString SearchUrl(ESource Source,const FString& Query);
FString SuggestUrl(ESource Source,const FString& Query);
bool ParseSuggestions(const TSharedPtr<FJsonObject>& Root,TArray<FString>& Out);
FString ChooseSearchTitle(const FString& Query,const TArray<FString>& Titles);
FString ParseUrl(const FEntry& Entry);
bool ParseSearch(const TSharedPtr<FJsonObject>& Root,ESource Source,TArray<FEntry>& Out);
bool ParseArticle(const TSharedPtr<FJsonObject>& Root,ESource Source,FArticle& Out);
void FormatHtml(const FString& Html,TArray<FBlock>& Blocks,TArray<FString>& Links,bool& Truncated);
// Examine captures store premultiplied linear RGB and inverse coverage alpha.
bool MakeExaminePortrait(const TArray<FLinearColor>& Pixels,int32 Width,int32 Height,TArray<uint8>& BGRA,FIntPoint& Size);
}
