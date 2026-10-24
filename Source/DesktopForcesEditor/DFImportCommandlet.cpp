#include "DFImportCommandlet.h"
#include "DFImporter.h"
#include "DesktopForcesEditor.h"
#include "Misc/Paths.h"

UDFImportCommandlet::UDFImportCommandlet() {
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UDFImportCommandlet::Main(const FString& Params) {
	FString Work, Steps = TEXT("textures,sounds,maps"), Maps;
	FParse::Value(*Params, TEXT("work="), Work);
	FParse::Value(*Params, TEXT("steps="), Steps, false); // false: keep commas
	FParse::Value(*Params, TEXT("maps="), Maps, false);
	if (Work.IsEmpty())
		Work = FPaths::ProjectDir() / TEXT("Tools/mfport/work/out");
	Work = FPaths::ConvertRelativePathToFull(Work);
	const bool bNeedsWork = Steps.Contains(TEXT("textures")) || Steps.Contains(TEXT("props")) || Steps.Contains(TEXT("sounds")) || Steps.Contains(TEXT("music")) || Steps.Contains(TEXT("maps")) || Steps.Contains(TEXT("effects")) || Steps.Contains(TEXT("skeletal")) || Steps.Contains(TEXT("soldiers"));
	if (bNeedsWork && !FPaths::DirectoryExists(Work)) {
		UE_LOG(LogDFEditor, Error, TEXT("Work dir %s not found - run Tools/mfport/mfport.py first"), *Work);
		return 1;
	}

	FDFImporter Importer(Work);
	int32 Errors = 0;
	Importer.EnsureTeamTintMaterial(); // shared runtime material, cheap no-op once it exists
	Importer.FixMasterMaterialUsage();
	if (Steps.Contains(TEXT("textures")) && !Importer.ImportTextures())
		++Errors;
	if (Steps.Contains(TEXT("sounds")))
		Importer.ImportSounds();
	if (Steps.Contains(TEXT("music")))
		Importer.ImportSounds(TEXT("Music"), true);
	if (Steps.Contains(TEXT("props")))
		Importer.ImportProps();
	if (Steps.Contains(TEXT("art")))
		Importer.ImportArt();
	if (Steps.Contains(TEXT("effects")))
		Importer.ImportEffects();
	if (Steps.Contains(TEXT("skeletal")) || Steps.Contains(TEXT("soldiers")))
		Importer.ImportSkeletal();
	if (Steps.Contains(TEXT("testmap")))
		Errors += Importer.BuildTestMap() ? 0 : 1;
	if (Steps.Contains(TEXT("patch")) || Steps.Contains(TEXT("zones"))) // "zones": its old name
	{
		TArray<FString> MapList;
		if (Maps.IsEmpty())
			IFileManager::Get().FindFiles(MapList, *(Work / TEXT("Maps/*")), false, true);
		else
			Maps.ParseIntoArray(MapList, TEXT(","));
		for (const FString& Map : MapList)
			Importer.PatchMapData(Map);
	}
	if (Steps.Contains(TEXT("maps"))) {
		TArray<FString> MapList;
		if (Maps.IsEmpty()) {
			// every converted map
			IFileManager::Get().FindFiles(MapList, *(Work / TEXT("Maps/*")), false, true);
		} else {
			Maps.ParseIntoArray(MapList, TEXT(","));
		}
		for (const FString& Map : MapList)
			Errors += Importer.BuildMap(Map) ? 0 : 1;
	}
	UE_LOG(LogDFEditor, Display, TEXT("DFImport finished with %d error(s)"), Errors);
	return Errors;
}
