#include "MFImportCommandlet.h"
#include "MFImporter.h"
#include "DesktopForcesEditor.h"
#include "Misc/Paths.h"

UMFImportCommandlet::UMFImportCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMFImportCommandlet::Main(const FString& Params)
{
	FString Work, Steps = TEXT("textures,sounds,maps"), Maps;
	FParse::Value(*Params, TEXT("work="), Work);
	FParse::Value(*Params, TEXT("steps="), Steps, false);   // false: keep commas
	FParse::Value(*Params, TEXT("maps="), Maps, false);
	if (Work.IsEmpty())
	{
		Work = FPaths::ProjectDir() / TEXT("Tools/mfport/work/out");
	}
	Work = FPaths::ConvertRelativePathToFull(Work);
	const bool bNeedsWork = Steps.Contains(TEXT("textures")) || Steps.Contains(TEXT("props")) || Steps.Contains(TEXT("sounds")) || Steps.Contains(TEXT("music")) || Steps.Contains(TEXT("maps"));
	if (bNeedsWork && !FPaths::DirectoryExists(Work))
	{
		UE_LOG(LogMFEditor, Error, TEXT("Work dir %s not found - run Tools/mfport/mfport.py first"), *Work);
		return 1;
	}

	FMFImporter Importer(Work);
	int32 Errors = 0;
	Importer.EnsureTeamTintMaterial();   // shared runtime material, cheap no-op once it exists
	Importer.FixMasterMaterialUsage();
	if (Steps.Contains(TEXT("textures")) && !Importer.ImportTextures())
	{
		++Errors;
	}
	if (Steps.Contains(TEXT("sounds")))
	{
		Importer.ImportSounds();
	}
	if (Steps.Contains(TEXT("music")))
	{
		Importer.ImportSounds(TEXT("Music"), true);
	}
	if (Steps.Contains(TEXT("props")))
	{
		Importer.ImportProps();
	}
	if (Steps.Contains(TEXT("art")))
	{
		Importer.ImportArt();
	}
	if (Steps.Contains(TEXT("testmap")))
	{
		Errors += Importer.BuildTestMap() ? 0 : 1;
	}
	if (Steps.Contains(TEXT("maps")))
	{
		TArray<FString> MapList;
		if (Maps.IsEmpty())
		{
			// every converted map
			IFileManager::Get().FindFiles(MapList, *(Work / TEXT("Maps/*")), false, true);
		}
		else
		{
			Maps.ParseIntoArray(MapList, TEXT(","));
		}
		for (const FString& Map : MapList)
		{
			Errors += Importer.BuildMap(Map) ? 0 : 1;
		}
	}
	UE_LOG(LogMFEditor, Display, TEXT("MFImport finished with %d error(s)"), Errors);
	return Errors;
}
