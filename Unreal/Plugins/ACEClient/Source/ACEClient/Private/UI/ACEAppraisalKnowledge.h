#pragma once
#include "ACEAppraisalFormatting.h"
#include "Knowledge/ACEWorldKnowledge.h"

namespace ACEAppraisalKnowledge
{
ACECLIENT_API TArray<ACEAppraisalFormatting::FCreatureDetailLine> CreatureInsights(
    const FACEAppraisalInfo& Info,const FACEWorldObject* Subject,UACEDatSubsystem* Dat,
    const FACEWorldKnowledge& Knowledge=FACEWorldKnowledge::Get());
ACECLIENT_API TArray<ACEAppraisalFormatting::FCreatureDetailLine> CompactCreatureInsights(
    const FACEAppraisalInfo& Info,const FACEWorldObject* Subject,UACEDatSubsystem* Dat,
    const FACEWorldKnowledge& Knowledge=FACEWorldKnowledge::Get());
}
