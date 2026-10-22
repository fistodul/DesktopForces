// MF skeletal meshes with their MeshAnimations: the soldiers (RageGfx RagePlayer1Mesh /
// RagePlayer2Mesh) and the first-person weapons (<X>PlayerMesh). Tools/mfport/mflib/skelmesh.py
// writes them as glTF (work/out/Skeletal), already in actor space and cm; Interchange imports
// skeleton, mesh and every sequence as an AnimSequence, and this puts MF's textures (the
// soldiers' team skins) on the material slots.
#include "DFImporter.h"
#include "DFImportUtil.h"
#include "Core/DFMeshScaleData.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/SkeletalMesh.h"
#include "IAssetTools.h"
#include "Materials/Material.h"
#include "ObjectTools.h"

using namespace DFImportUtil;

bool FDFImporter::ImportSkeletal()
{
	TSharedPtr<FJsonObject> Info = LoadJson(WorkDir / TEXT("Skeletal/skeletal.json"));
	if (!Info)
	{
		UE_LOG(LogDFEditor, Warning, TEXT("No Skeletal/skeletal.json - run mfport.py skeletal"));
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
		const FString Dest = FString::Printf(TEXT("%s/%s/%s"), *Packs.PackageRoot(M->GetStringField(TEXT("package"))), *M->GetStringField(TEXT("folder")), *Name);
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
			UE_LOG(LogDFEditor, Error, TEXT("%s: no skeletal mesh imported into %s"), *Name, *Dest);
			continue;
		}
		// slot "SkinN" = MF texture index N (MESHMAP SETTEXTURE; on the soldiers the team skin
		// element of Pawn.SetMultiSkin: blue 00-03 here, the game swaps in red 04-07 and the
		// captains' 08 / 09, made beforehand as extra_materials)
		const TSharedPtr<FJsonObject>* Keys = nullptr;
		M->TryGetObjectField(TEXT("materials"), Keys);
		TArray<FSkeletalMaterial> Mats = Mesh->GetMaterials();
		for (FSkeletalMaterial& Slot : Mats)
		{
			FString Key;
			if (Keys && (*Keys)->TryGetStringField(Slot.MaterialSlotName.ToString().RightChop(4), Key))
			{
				Slot.MaterialInterface = GetMaterial(Key);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Extra = nullptr;
		if (M->TryGetArrayField(TEXT("extra_materials"), Extra))
		{
			for (const TSharedPtr<FJsonValue>& E : *Extra)
			{
				GetMaterial(E->AsString());
			}
		}
		Mesh->SetMaterials(Mats);
		// a non-uniform MESHMAP SCALE can't be in the skeleton: its rest goes on the component
		Mesh->RemoveUserDataOfClass(UDFMeshScaleData::StaticClass());
		const TArray<TSharedPtr<FJsonValue>>* Scale = nullptr;
		if (M->TryGetArrayField(TEXT("component_scale"), Scale) && Scale->Num() == 3)
		{
			const FVector Rest((*Scale)[0]->AsNumber(), (*Scale)[1]->AsNumber(), (*Scale)[2]->AsNumber());
			if (!Rest.Equals(FVector::OneVector))
			{
				UDFMeshScaleData* Data = NewObject<UDFMeshScaleData>(Mesh);
				Data->ComponentScale = Rest;
				Mesh->AddAssetUserData(Data);
			}
		}
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
		UE_LOG(LogDFEditor, Display, TEXT("%s: %d bones, %d animations"), *Name, Mesh->GetRefSkeleton().GetNum(), Anims);
		++Ok;
	}
	return Ok > 0;
}
