#include "Core/MFContent.h"
#include "UObject/UObjectGlobals.h"

FString MF::ClassicPath(const FString& Rel)
{
	return FString::Printf(TEXT("%s/%s.%s"), ClassicRoot, *Rel, *FPaths::GetBaseFilename(Rel));
}

UObject* MF::LoadClassicObject(UClass* Class, const FString& Rel)
{
	return StaticLoadObject(Class, nullptr, *ClassicPath(Rel), nullptr, LOAD_NoWarn | LOAD_Quiet);
}
