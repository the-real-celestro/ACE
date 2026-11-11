#include "ACENativeCursor.h"
#if !PLATFORM_MAC
TSharedPtr<FACENativeCursor> FACENativeCursor::Create(UGameViewportClient*) { return nullptr; }
#endif
