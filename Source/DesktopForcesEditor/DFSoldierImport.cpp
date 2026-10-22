// MF soldiers (RageGfx RagePlayer1Mesh / RagePlayer2Mesh: skeletal meshes with their
// MeshAnimations). Tools/mfport/mflib/skelmesh.py writes them as glTF (work/out/Soldiers),
// already in actor space and cm; Interchange imports skeleton, mesh and every sequence as an
// AnimSequence, and this puts MF's team skins on the material slots.
#include "DFImporter.h"
#include "DFImportUtil.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/SkeletalMesh.h"
#include "IAssetTools.h"
#include "Materials/Material.h"
#include "ObjectTools.h"

using namespace DFImportUtil;

bool FDFImporter::ImportSoldiers()
{
	TSharedPtr<FJsonObject> Info = LoadJson(WorkDir / TEXT("Soldiers/soldiers.json"));
	if (!Info)
	{
		UE_LOG(LogDFEditor, Warning, TEXT("No Soldiers/soldiers.json - run mfport.py soldiers"));
		return false;
	}
	// skinned meshes need their master material compiled for skeletal meshes too
	if (UMaterial* Master = GetMasterMaterial(TEXT("Opaque"))->GetMaterial(); Master && !Master->GetUsageByFlag(MATUSAGE_SkeletalMesh))
	{
		Master->SetMaterialUsage(MATUSAGE_SkeletalMesh);
		SaveAsset(Master);
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	int32 Ok = 0;
	for (const TSharedPtr<FJsonValue>& V : Info->GetArrayField(TEXT("meshes")))
	{
		const TSharedPtr<FJsonObject> M = V->AsObject();
		const FString Name = M->GetStringField(TEXT("name"));
		const FString Dest = FString::Printf(TEXT("%s/Soldiers/%s"), *Packs.PackageRoot(M->GetStringField(TEXT("package"))), *Name);
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = FPaths::ConvertRelativePathToFull(WorkDir / M->GetStringField(TEXT("file")));
		Task->DestinationPath = Dest;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		AssetTools.ImportAssetTasks({ Task });

		TArray<FAssetData> Assets;
		Registry.ScanSynchronous({ Dest }, {}, UE::AssetRegistry::EScanFlags::ForceRescan);
		Registry.GetAssetsByPath(FName(*Dest), Assets, true);
		USkeletalMesh* Mesh = nullptr;
		TArray<UObject*> Junk;
		for (const FAssetData& A : Assets)
		{
			UObject* Obj = A.GetAsset();
			if (USkeletalMesh* SK = Cast<USkeletalMesh>(Obj))
			{
				Mesh = SK;
			}
			else if (Cast<UMaterialInterface>(Obj) || Cast<UTexture>(Obj))
			{
				Junk.Add(Obj);   // glTF's own materials: the slots get MF's skins instead
			}
		}
		if (!Mesh)
		{
			UE_LOG(LogDFEditor, Error, TEXT("Soldier %s: no skeletal mesh imported into %s"), *Name, *Dest);
			continue;
		}
		// slot "SkinN" = MF texture index N = the team skin element (Pawn.SetMultiSkin): blue
		// 00-03 by default; the game swaps in red (04-07) and captains' (08 / 09) at runtime
		const FString Skin = M->GetStringField(TEXT("skin"));
		TArray<FSkeletalMaterial> Mats = Mesh->GetMaterials();
		for (FSkeletalMaterial& Slot : Mats)
		{
			const int32 Index = FCString::Atoi(*Slot.MaterialSlotName.ToString().RightChop(4));
			Slot.MaterialInterface = GetMaterial(FString::Printf(TEXT("%s%02d|opaque"), *Skin, Index));
		}
		for (int32 i = 0; i < 10; ++i)
		{
			GetMaterial(FString::Printf(TEXT("%s%02d|opaque"), *Skin, i));
		}
		Mesh->SetMaterials(Mats);
		Mesh->PostEditChange();
		if (Junk.Num())
		{
			ObjectTools::ForceDeleteObjects(Junk, false);
		}
		int32 Anims = 0;
		Assets.Reset();
		Registry.GetAssetsByPath(FName(*Dest), Assets, true);
		for (const FAssetData& A : Assets)
		{
			if (UObject* Obj = A.GetAsset())
			{
				Anims += Obj->IsA<UAnimSequence>() ? 1 : 0;
				SaveAsset(Obj);
			}
		}
		UE_LOG(LogDFEditor, Display, TEXT("Soldier %s: %d bones, %d animations"), *Name, Mesh->GetRefSkeleton().GetNum(), Anims);
		++Ok;
	}
	return Ok > 0;
}
