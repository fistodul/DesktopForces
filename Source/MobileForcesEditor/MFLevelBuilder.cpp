// FMFImporter::BuildMap - turns Maps/<map>/level.json (written by Tools/mfport) into a UE level.
#include "MFImporter.h"
#include "MobileForcesEditor.h"
#include "Builders/CubeBuilder.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/AmbientSound.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "Game/MFPlayerStart.h"
#include "Loadout/MFLoadout.h"
#include "World/MFMapActors.h"

namespace
{
	FVector Vec(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O->TryGetArrayField(Field, A) || A->Num() < 3)
		{
			return FVector::ZeroVector;
		}
		return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
	}

	FRotator Rot(const TSharedPtr<FJsonObject>& O)
	{
		const TSharedPtr<FJsonObject>* R = nullptr;
		if (!O->TryGetObjectField(TEXT("rotation"), R))
		{
			return FRotator::ZeroRotator;
		}
		return FRotator((*R)->GetNumberField(TEXT("pitch")), (*R)->GetNumberField(TEXT("yaw")), (*R)->GetNumberField(TEXT("roll")));
	}

	FLinearColor Color(const TSharedPtr<FJsonObject>& O)
	{
		const FVector C = Vec(O, TEXT("color"));
		return FLinearColor(C.X, C.Y, C.Z);
	}

	EMFTeam Team(const FString& S)
	{
		return S == TEXT("Blue") ? EMFTeam::Blue : (S == TEXT("Red") ? EMFTeam::Red : EMFTeam::None);
	}

	/** Set UPROPERTYs from text (same syntax as copy/paste in the editor). */
	void ApplyProperties(UObject* Obj, const TSharedPtr<FJsonObject>& Props)
	{
		if (!Props)
		{
			return;
		}
		for (const auto& P : Props->Values)
		{
			const FString Key(P.Key);
			FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(*Key));
			if (!Prop)
			{
				UE_LOG(LogMFEditor, Warning, TEXT("%s has no property %s"), *Obj->GetClass()->GetName(), *Key);
				continue;
			}
			Prop->ImportText_InContainer(*P.Value->AsString(), Obj, Obj, PPF_None);
		}
	}

	void MakeBoxBrush(UWorld* World, ABrush* Brush, const FVector& Extent)
	{
		UCubeBuilder* Builder = NewObject<UCubeBuilder>();
		Builder->X = Extent.X * 2.f;
		Builder->Y = Extent.Y * 2.f;
		Builder->Z = Extent.Z * 2.f;
		Builder->Build(World, Brush);
	}
}

bool FMFImporter::BuildMap(const FString& MapName)
{
	const FString MapDir = WorkDir / TEXT("Maps") / MapName;
	FString Text;
	TSharedPtr<FJsonObject> Level;
	if (!FFileHelper::LoadFileToString(Text, *(MapDir / TEXT("level.json"))) ||
		!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Level) || !Level)
	{
		UE_LOG(LogMFEditor, Error, TEXT("Can't read %s/level.json - run Tools/mfport/mfport.py map %s"), *MapDir, *MapName);
		return false;
	}

	const FString PackageName = TEXT("/Game/MFMaps/Generated/") + MapName;
	UPackage* Pkg = CreatePackage(*PackageName);
	Pkg->SetPackageFlags(PKG_ContainsMap);
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(*MapName), Pkg, true);
	World->SetFlags(RF_Public | RF_Standalone);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
	Context.SetCurrentWorld(World);

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FBox WorldBounds(ForceInit);
	int32 Spawned = 0;

	// --- world geometry chunks -----------------------------------------------------------
	for (const TSharedPtr<FJsonValue>& V : Level->GetArrayField(TEXT("world")))
	{
		const TSharedPtr<FJsonObject> W = V->AsObject();
		UStaticMesh* Mesh = LoadOrBuildMesh(MapDir, W->GetStringField(TEXT("mesh")), MapName);
		if (!Mesh)
		{
			continue;
		}
		const bool bRender = W->GetBoolField(TEXT("render"));
		const FVector Loc = Vec(W, TEXT("location"));
		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Loc, FRotator::ZeroRotator, SP);
		UStaticMeshComponent* C = A->GetStaticMeshComponent();
		C->SetMobility(EComponentMobility::Static);
		C->SetStaticMesh(Mesh);
		if (bRender)
		{
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->bOverrideLightMapRes = false;
			WorldBounds += Mesh->GetBoundingBox().ShiftBy(Loc);
			A->SetActorLabel(FString::Printf(TEXT("World_%s"), *Mesh->GetName()));
		}
		else
		{
			// MF solid surfaces (incl. invisible ones): collision only
			C->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			C->SetVisibility(false);
			C->SetHiddenInGame(true);
			C->CastShadow = false;
			C->bAffectDynamicIndirectLighting = false;
			A->SetActorLabel(FString::Printf(TEXT("Collision_%s"), *Mesh->GetName()));
		}
		++Spawned;
	}

	// --- spawn records -------------------------------------------------------------------
	for (const TSharedPtr<FJsonValue>& V : Level->GetArrayField(TEXT("spawns")))
	{
		const TSharedPtr<FJsonObject> R = V->AsObject();
		const FString Type = R->GetStringField(TEXT("type"));
		const FString Name = R->GetStringField(TEXT("name"));
		const FVector Loc = Vec(R, TEXT("location"));
		const FRotator Rotation = Rot(R);
		AActor* Actor = nullptr;

		if (Type == TEXT("static_mesh"))
		{
			UStaticMesh* Mesh = LoadOrBuildMesh(MapDir, R->GetStringField(TEXT("mesh")), MapName);
			if (!Mesh)
			{
				continue;
			}
			AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Loc, Rotation, SP);
			A->SetActorScale3D(Vec(R, TEXT("scale")));
			UStaticMeshComponent* C = A->GetStaticMeshComponent();
			C->SetMobility(EComponentMobility::Static);
			C->SetStaticMesh(Mesh);
			if (!R->GetBoolField(TEXT("collision")))
			{
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			Actor = A;
		}
		else if (Type == TEXT("point_light"))
		{
			APointLight* L = World->SpawnActor<APointLight>(Loc, Rotation, SP);
			UPointLightComponent* C = L->PointLightComponent;
			C->SetMobility(EComponentMobility::Static);
			C->bUseInverseSquaredFalloff = false;   // UE1-style radial falloff
			C->LightFalloffExponent = 2.f;
			C->SetAttenuationRadius(R->GetNumberField(TEXT("radius")));
			C->SetIntensity(R->GetNumberField(TEXT("intensity")));
			C->SetLightColor(Color(R));
			C->SetCastShadows(true);
			Actor = L;
		}
		else if (Type == TEXT("spot_light"))
		{
			ASpotLight* L = World->SpawnActor<ASpotLight>(Loc, Rotation, SP);
			USpotLightComponent* C = L->SpotLightComponent;
			C->SetMobility(EComponentMobility::Static);
			C->bUseInverseSquaredFalloff = false;
			C->LightFalloffExponent = 2.f;
			C->SetAttenuationRadius(R->GetNumberField(TEXT("radius")));
			C->SetIntensity(R->GetNumberField(TEXT("intensity")));
			C->SetLightColor(Color(R));
			C->SetOuterConeAngle(R->GetNumberField(TEXT("cone")));
			C->SetInnerConeAngle(R->GetNumberField(TEXT("cone")) * 0.7f);
			Actor = L;
		}
		else if (Type == TEXT("directional_light"))
		{
			ADirectionalLight* L = World->SpawnActor<ADirectionalLight>(Loc, Rotation, SP);
			UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(L->GetLightComponent());
			C->SetMobility(EComponentMobility::Static);
			C->SetIntensity(R->GetNumberField(TEXT("intensity")));
			C->SetLightColor(Color(R));
			Actor = L;
		}
		else if (Type == TEXT("actor"))
		{
			UClass* Class = LoadClass<AActor>(nullptr, *R->GetStringField(TEXT("class")));
			if (!Class)
			{
				UE_LOG(LogMFEditor, Warning, TEXT("Unknown class %s"), *R->GetStringField(TEXT("class")));
				continue;
			}
			Actor = World->SpawnActor<AActor>(Class, Loc, Rotation, SP);
			const TSharedPtr<FJsonObject>* Props = nullptr;
			if (Actor && R->TryGetObjectField(TEXT("props"), Props))
			{
				ApplyProperties(Actor, *Props);
				Actor->RerunConstructionScripts();
			}
		}
		else if (Type == TEXT("loadout_zone"))
		{
			const FVector Min = Vec(R, TEXT("box_min")), Max = Vec(R, TEXT("box_max"));
			AMFLoadoutZone* Z = World->SpawnActor<AMFLoadoutZone>((Min + Max) * 0.5f, FRotator::ZeroRotator, SP);
			Z->Volume->SetBoxExtent((Max - Min) * 0.5f);
			Z->Team = Team(R->GetStringField(TEXT("team")));
			Actor = Z;
		}
		else if (Type == TEXT("ambient_sound"))
		{
			USoundWave* Sound = FindSound(R->GetStringField(TEXT("sound")));
			if (!Sound)
			{
				continue;   // sounds not imported yet
			}
			AAmbientSound* S = World->SpawnActor<AAmbientSound>(Loc, Rotation, SP);
			UAudioComponent* AC = S->GetAudioComponent();
			AC->SetSound(Sound);
			AC->VolumeMultiplier = R->GetNumberField(TEXT("volume"));
			AC->bOverrideAttenuation = true;
			AC->AttenuationOverrides.FalloffDistance = R->GetNumberField(TEXT("radius"));
			AC->AttenuationOverrides.AttenuationShapeExtents = FVector(R->GetNumberField(TEXT("radius")) * 0.25f, 0, 0);
			Sound->bLooping = true;
			Actor = S;
		}
		else if (Type == TEXT("map_info"))
		{
			AMFMapInfo* Info = World->SpawnActor<AMFMapInfo>(Loc, Rotation, SP);
			Info->Title = R->GetStringField(TEXT("title"));
			Info->Author = R->GetStringField(TEXT("author"));
			Info->IdealPlayerCount = R->GetStringField(TEXT("ideal_players"));
			Info->SourceMap = MapName;
			FString Song, SongPkg, SongName;
			if (R->TryGetStringField(TEXT("song"), Song) && Song.Split(TEXT("."), &SongPkg, &SongName))
			{
				// converted tracker music: /Game/MFImported/Music/<package>/<name>
				Info->Music = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/MFImported/Music/%s/%s.%s"), *SongPkg, *SongName, *SongName), nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
			Actor = Info;
		}
		else if (Type == TEXT("legacy"))
		{
			AMFLegacyActor* L = World->SpawnActor<AMFLegacyActor>(Loc, Rotation, SP);
			L->MFClass = FName(*R->GetStringField(TEXT("mf_class")));
			L->MFName = FName(*R->GetStringField(TEXT("mf_name")));
			L->Team = Team(R->GetStringField(TEXT("team")));
			const TSharedPtr<FJsonObject>* Props = nullptr;
			if (R->TryGetObjectField(TEXT("props"), Props))
			{
				for (const auto& P : (*Props)->Values)
				{
					L->Properties.Add(FString(P.Key), P.Value->AsString());
				}
			}
			Actor = L;
		}

		if (Actor)
		{
			Actor->SetActorLabel(Name);
			++Spawned;
		}
	}

	// --- lighting setup --------------------------------------------------------------------
	AWorldSettings* WS = World->GetWorldSettings();
	const TSharedPtr<FJsonObject>* LM = nullptr;
	if (Level->TryGetObjectField(TEXT("lightmass"), LM))
	{
		// MF zone ambient light -> Lightmass environment colour
		const TArray<TSharedPtr<FJsonValue>>& C = (*LM)->GetArrayField(TEXT("environment_color"));
		WS->LightmassSettings.EnvironmentColor = FLinearColor(C[0]->AsNumber(), C[1]->AsNumber(), C[2]->AsNumber()).ToFColor(true);
		WS->LightmassSettings.EnvironmentIntensity = (*LM)->GetNumberField(TEXT("environment_intensity")) * 2.f;
	}
	WS->LightmassSettings.NumIndirectLightingBounces = 2;
	WS->LightmassSettings.bUseAmbientOcclusion = false;
	WS->LightmassSettings.StaticLightingLevelScale = 1.f;
	WS->bPrecomputeVisibility = true;   // cheap occlusion for mobile, built with lighting

	if (WorldBounds.IsValid)
	{
		ALightmassImportanceVolume* Vol = World->SpawnActor<ALightmassImportanceVolume>(WorldBounds.GetCenter(), FRotator::ZeroRotator, SP);
		MakeBoxBrush(World, Vol, WorldBounds.GetExtent() + FVector(100.f));
		Vol->SetActorLabel(TEXT("LightmassImportance"));
	}

	World->UpdateWorldComponents(true, false);
	const bool bSaved = SaveAsset(World);
	UE_LOG(LogMFEditor, Display, TEXT("Map %s: %d actors -> %s (%s)"), *MapName, Spawned, *PackageName, bSaved ? TEXT("saved") : TEXT("SAVE FAILED"));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return bSaved;
}

bool FMFImporter::BuildTestMap()
{
	const FString PackageName = TEXT("/Game/MF/Maps/MF_TestRange");
	UPackage* Pkg = CreatePackage(*PackageName);
	Pkg->SetPackageFlags(PKG_ContainsMap);
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("MF_TestRange"), Pkg, true);
	World->SetFlags(RF_Public | RF_Standalone);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
	Context.SetCurrentWorld(World);
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Mat = GetMasterMaterial(TEXT("Opaque"));
	auto Block = [&](const FVector& Center, const FVector& Size, const FRotator& Rot = FRotator::ZeroRotator)
	{
		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Center, Rot, SP);
		UStaticMeshComponent* C = A->GetStaticMeshComponent();
		C->SetMobility(EComponentMobility::Static);
		C->SetStaticMesh(Cube);
		C->SetMaterial(0, Mat);
		A->SetActorScale3D(Size / 100.f);
		C->bOverrideLightMapRes = true;
		C->OverriddenLightMapRes = 64;
		return A;
	};
	// 60 x 60 m arena, 6 m walls, cover and a ramp to a ledge
	Block(FVector(0, 0, -50), FVector(6000, 6000, 100));
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector Dir = FRotator(0, 90.f * i, 0).Vector();
		Block(Dir * 3050.f + FVector(0, 0, 300), FVector(i % 2 ? 6200 : 100, i % 2 ? 100 : 6200, 600));
	}
	Block(FVector(800, 0, 100), FVector(200, 600, 200));
	Block(FVector(-800, 400, 60), FVector(300, 300, 120));
	Block(FVector(0, -1200, 150), FVector(1200, 100, 300));
	Block(FVector(-1800, -1800, 200), FVector(800, 800, 400));
	Block(FVector(-1050, -1800, 100), FVector(800, 400, 20), FRotator(-14.5f, 0, 0));

	auto Spawn = [&](const FVector& Loc, float Yaw, uint8 Team)
	{
		AMFPlayerStart* S = World->SpawnActor<AMFPlayerStart>(Loc + FVector(0, 0, 100), FRotator(0, Yaw, 0), SP);
		S->TeamNumber = Team;
	};
	Spawn(FVector(2000, 2000, 0), 225.f, 255);
	Spawn(FVector(-2000, 2000, 0), 315.f, 255);
	Spawn(FVector(2000, -2000, 0), 135.f, 255);
	Spawn(FVector(1500, 0, 0), 180.f, 255);
	Spawn(FVector(2400, 600, 0), 180.f, 0);
	Spawn(FVector(2400, -600, 0), 180.f, 0);
	Spawn(FVector(-2400, 600, 0), 0.f, 1);
	Spawn(FVector(-2400, -600, 0), 0.f, 1);

	auto Crate = [&](const FVector& Loc, EMFTeam Team)
	{
		AMFLoadoutCrate* C = World->SpawnActor<AMFLoadoutCrate>(Loc + FVector(0, 0, 60), FRotator::ZeroRotator, SP);
		C->Team = Team;
		C->bTeamPlay = Team != EMFTeam::None;
		C->bNonTeamPlay = Team == EMFTeam::None;
	};
	Crate(FVector(2000, 1700, 0), EMFTeam::None);
	Crate(FVector(-2000, 1700, 0), EMFTeam::None);
	Crate(FVector(1500, 300, 0), EMFTeam::None);
	Crate(FVector(2700, 0, 0), EMFTeam::Blue);
	Crate(FVector(-2700, 0, 0), EMFTeam::Red);

	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 1000), FRotator(-50.f, 30.f, 0), SP);
	Sun->GetLightComponent()->SetMobility(EComponentMobility::Static);
	Sun->GetLightComponent()->SetIntensity(2.5f);
	APointLight* Lamp = World->SpawnActor<APointLight>(FVector(-1800, -1800, 700), FRotator::ZeroRotator, SP);
	Lamp->PointLightComponent->SetMobility(EComponentMobility::Static);
	Lamp->PointLightComponent->bUseInverseSquaredFalloff = false;
	Lamp->PointLightComponent->SetAttenuationRadius(1500.f);
	Lamp->PointLightComponent->SetIntensity(3.f);
	Lamp->PointLightComponent->SetLightColor(FLinearColor(1.f, 0.8f, 0.5f));

	AWorldSettings* WS = World->GetWorldSettings();
	WS->LightmassSettings.EnvironmentColor = FColor(90, 100, 120);
	WS->LightmassSettings.EnvironmentIntensity = 0.5f;
	ALightmassImportanceVolume* Vol = World->SpawnActor<ALightmassImportanceVolume>(FVector(0, 0, 300), FRotator::ZeroRotator, SP);
	MakeBoxBrush(World, Vol, FVector(3200, 3200, 500));

	AMFMapInfo* Info = World->SpawnActor<AMFMapInfo>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
	Info->Title = TEXT("Test Range");
	Info->Author = TEXT("Mobile Forces UE5 project");

	World->UpdateWorldComponents(true, false);
	const bool bSaved = SaveAsset(World);
	UE_LOG(LogMFEditor, Display, TEXT("Test map -> %s (%s)"), *PackageName, bSaved ? TEXT("saved") : TEXT("SAVE FAILED"));
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return bSaved;
}
