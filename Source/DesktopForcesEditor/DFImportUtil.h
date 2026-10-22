#pragma once

// Helpers shared by the importer's source files (DFImporter.cpp, DFEffectsImport.cpp).
#include "CoreMinimal.h"
#include "DesktopForcesEditor.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace DFImportUtil
{
	inline TSharedPtr<FJsonObject> LoadJson(const FString& File)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			UE_LOG(LogDFEditor, Error, TEXT("Can't read %s"), *File);
			return nullptr;
		}
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			UE_LOG(LogDFEditor, Error, TEXT("Bad JSON in %s"), *File);
			return nullptr;
		}
		return Obj;
	}

	/** UE1 names may hold characters UE package / object names can't ("ap_P&Hd", "chest,box"). */
	inline FString SafeName(const FString& In)
	{
		FString Out = In;
		for (TCHAR& Ch : Out)
		{
			if (FCString::Strchr(TEXT("\\:*?\"<>|' ,.&!~@#(){}[]=;^%$`"), Ch))
			{
				Ch = TEXT('_');
			}
		}
		return Out;
	}

	/** "rage_warehouse.metal.ware_metal_29" -> ("rage_warehouse/metal", "ware_metal_29") */
	inline void SplitKey(const FString& Key, FString& OutDir, FString& OutName)
	{
		TArray<FString> Parts;
		Key.ParseIntoArray(Parts, TEXT("."));
		OutName = SafeName(Parts.Num() ? Parts.Pop() : Key);
		for (FString& P : Parts)
		{
			P = SafeName(P);
		}
		OutDir = FString::Join(Parts, TEXT("/"));
	}

	template <typename T>
	T* FindOrCreateAsset(const FString& PackagePath, const FString& Name, bool& bOutCreated)
	{
		const FString Full = PackagePath / Name;
		T* Existing = LoadObject<T>(nullptr, *(Full + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		bOutCreated = Existing == nullptr;
		if (Existing)
		{
			return Existing;
		}
		UPackage* Pkg = CreatePackage(*Full);
		return NewObject<T>(Pkg, *Name, RF_Public | RF_Standalone);
	}
}
