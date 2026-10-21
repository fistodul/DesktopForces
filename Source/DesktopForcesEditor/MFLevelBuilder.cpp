// FMFImporter::BuildMap - turns Maps/<map>/level.json (written by Tools/mfport) into a UE level.
#include "MFImporter.h"
#include "DesktopForcesEditor.h"
#include "Builders/CubeBuilder.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "ActorFactories/ActorFactory.h"
#include "World/MFLadder.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "EngineUtils.h"
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
#include "NavMesh/NavMeshBoundsVolume.h"
#include "World/MFZoneVolume.h"
#include "World/MFRoadNetwork.h"
#include "World/MFTeleporter.h"
#include "World/MFPickupSpot.h"
#include "World/MFMover.h"
#include "World/MFBlocker.h"
#include "World/MFBotSpot.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Weapons/MFWeapon.h"
#include "Components/CapsuleComponent.h"
#include "Lightmass/PrecomputedVisibilityVolume.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
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

	/** UE1 lightmaps had one texel per 32 uu: soft shadows. Light source size for Lightmass penumbras (cm). */
	constexpr float LightSourceRadius = 40.f;

	FLinearColor Color(const TSharedPtr<FJsonObject>& O)
	{
		const FVector C = Vec(O, TEXT("color"));
		return FLinearColor(C.X, C.Y, C.Z);
	}

	/** Team id from level.json ("0", "1" ... ; anything else = no team). */
	uint8 Team(const FString& S)
	{
		return S.IsNumeric() ? uint8(FMath::Clamp(FCString::Atoi(*S), 0, 255)) : MF::NoTeam;
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

	void MakeBoxBrush(UWorld* World, AVolume* Volume, const FVector& Extent)
	{
		UCubeBuilder* Builder = NewObject<UCubeBuilder>();
		Builder->X = Extent.X * 2.f;
		Builder->Y = Extent.Y * 2.f;
		Builder->Z = Extent.Z * 2.f;
		// Same path as dragging a volume into the editor: creates the brush model + body setup
		UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
	}

	/** Bake the navmesh into the level (bots need no runtime rebuild; mobile-friendly). */
	void BuildNavigation(UWorld* World)
	{
		FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode);
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			UE_LOG(LogMFEditor, Warning, TEXT("No navigation system - navmesh not built"));
			return;
		}
		// Editor worlds start locked (AsyncLoadLock / InitialLock) - this world is complete
		Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock | ENavigationBuildLock::InitialLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
		Nav->Build();   // synchronous, like Build > Build Paths
		// Ladder ends that missed the navmesh (top traced onto a beam, foot in a corner):
		// snap them to the nearest walkable spot and build again so the links connect
		int32 Snapped = 0;
		for (TActorIterator<AMFLadderVolume> It(World); It; ++It)
		{
			const FTransform& T = It->GetActorTransform();
			for (FVector* End : { &It->LinkBottom, &It->LinkTop })
			{
				FNavLocation Exact, Near;
				const FVector W = T.TransformPosition(*End);
				if (!Nav->ProjectPointToNavigation(W, Exact, FVector(40, 40, 100)) && Nav->ProjectPointToNavigation(W, Near, FVector(200, 200, 300)))
				{
					*End = T.InverseTransformPosition(Near.Location);
					++Snapped;
				}
			}
		}
		if (Snapped > 0)
		{
			UE_LOG(LogMFEditor, Display, TEXT("Snapped %d ladder link ends to the navmesh, rebuilding"), Snapped);
			Nav->Build();
		}
		for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
		{
			UE_LOG(LogMFEditor, Display, TEXT("Navmesh %s: %d tiles"), *It->GetName(), It->GetNavMeshTilesCount());
		}
	}

	/** Records a built map can take in place, without a rebuild or a new bake (`--steps patch`):
	 *  actors over a UE1 zone's bounds (loadout_zone, zone_volume, zone_box), the road graph
	 *  (road_network), teleporters, periodic sounds, pickups, movers, blockers, placed bots. nullptr for any other record. */
	AActor* SpawnMapData(UWorld* World, const TSharedPtr<FJsonObject>& R, const FActorSpawnParameters& SP, TFunctionRef<USoundWave*(const FString&)> FindSound,
		TFunctionRef<UStaticMesh*(const FString&)> LoadMesh)
	{
		const FString Type = R->GetStringField(TEXT("type"));
		if (Type == TEXT("bot_spot"))
		{
			AMFBotSpot* B = World->SpawnActor<AMFBotSpot>(Vec(R, TEXT("location")), Rot(R), SP);
			B->Health = int32(R->GetNumberField(TEXT("health")));
			B->bStationary = R->GetBoolField(TEXT("stationary"));
			B->Team = Team(R->GetStringField(TEXT("team")));
			for (const TSharedPtr<FJsonValue>& W : R->GetArrayField(TEXT("loadout")))
			{
				if (UClass* C = LoadClass<AMFWeapon>(nullptr, *W->AsString()))
				{
					B->Loadout.Add(C);
				}
			}
			return B;
		}
		if (Type == TEXT("blocker"))
		{
			AMFBlocker* B = World->SpawnActor<AMFBlocker>(Vec(R, TEXT("location")), FRotator::ZeroRotator, SP);
			B->Team = Team(R->GetStringField(TEXT("team")));
			B->bBlockVehicles = R->GetBoolField(TEXT("vehicles"));
			B->SetCylinder(R->GetNumberField(TEXT("radius")), R->GetNumberField(TEXT("height")));
			return B;
		}
		if (Type == TEXT("mover"))
		{
			UStaticMesh* MoverMesh = LoadMesh(R->GetStringField(TEXT("mesh")));
			if (!MoverMesh)
			{
				return nullptr;
			}
			AMFMover* M = World->SpawnActor<AMFMover>(Vec(R, TEXT("location")), Rot(R), SP);
			M->Mesh->SetStaticMesh(MoverMesh);
			if (!R->GetBoolField(TEXT("collide")))
			{
				M->Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			const TArray<TSharedPtr<FJsonValue>>& S = R->GetArrayField(TEXT("spin"));
			M->Spin->RotationRate = FRotator(S[0]->AsNumber(), S[1]->AsNumber(), S[2]->AsNumber());
			for (const TSharedPtr<FJsonValue>& K : R->GetArrayField(TEXT("keys")))
			{
				const TSharedPtr<FJsonObject> KO = K->AsObject();
				const TArray<TSharedPtr<FJsonValue>>& KR = KO->GetArrayField(TEXT("rot"));
				M->KeyPos.Add(Vec(KO, TEXT("pos")));
				M->KeyRot.Add(FRotator(KR[0]->AsNumber(), KR[1]->AsNumber(), KR[2]->AsNumber()));
			}
			M->MoveTime = R->GetNumberField(TEXT("move_time"));
			M->StayOpenTime = R->GetNumberField(TEXT("stay_open_time"));
			M->InitialState = FName(*R->GetStringField(TEXT("initial_state")));
			const FString Cond = R->GetStringField(TEXT("cond_game_type"));
			M->CondGameType = Cond.IsEmpty() ? NAME_None : FName(*Cond);
			M->bTeamPlay = R->GetBoolField(TEXT("team_play"));
			M->bNonTeamPlay = R->GetBoolField(TEXT("non_team_play"));
			return M;
		}
		if (Type == TEXT("teleporter"))
		{
			AMFTeleporter* T = World->SpawnActor<AMFTeleporter>(Vec(R, TEXT("location")), Rot(R), SP);
			T->URL = R->GetStringField(TEXT("url"));
			T->TeleporterTag = FName(*R->GetStringField(TEXT("tag")));
			T->bEnabled = R->GetBoolField(TEXT("enabled"));
			T->bChangesYaw = R->GetBoolField(TEXT("changes_yaw"));
			T->bChangesVelocity = R->GetBoolField(TEXT("changes_velocity"));
			T->TargetVelocity = Vec(R, TEXT("target_velocity"));
			const TArray<TSharedPtr<FJsonValue>>& Rev = R->GetArrayField(TEXT("reverses"));
			T->bReversesX = Rev[0]->AsBool();
			T->bReversesY = Rev[1]->AsBool();
			T->bReversesZ = Rev[2]->AsBool();
			T->Trigger->SetCapsuleSize(R->GetNumberField(TEXT("radius")), R->GetNumberField(TEXT("height")));
			return T;
		}
		if (Type == TEXT("pickup"))
		{
			UClass* Item = LoadClass<AMFWeapon>(nullptr, *R->GetStringField(TEXT("weapon")));
			if (!Item)
			{
				return nullptr;
			}
			AMFPickupSpot* P = World->SpawnActor<AMFPickupSpot>(Vec(R, TEXT("location")), Rot(R), SP);
			P->ItemClass = Item;
			P->RespawnTime = R->GetNumberField(TEXT("respawn_time"));
			P->MaxClipAmmo = int32(R->GetNumberField(TEXT("max_clip_ammo")));
			P->MaxClips = int32(R->GetNumberField(TEXT("max_clips")));
			return P;
		}
		if (Type == TEXT("periodic_sound"))
		{
			USoundWave* Sound = FindSound(R->GetStringField(TEXT("sound")));
			if (!Sound)
			{
				return nullptr;   // sounds not imported yet
			}
			Sound->bLooping = true;
			AMFPeriodicSound* P = World->SpawnActor<AMFPeriodicSound>(Vec(R, TEXT("location")), FRotator::ZeroRotator, SP);
			UAudioComponent* AC = P->Audio;
			AC->SetSound(Sound);
			AC->VolumeMultiplier = R->GetNumberField(TEXT("volume"));
			AC->PitchMultiplier = R->GetNumberField(TEXT("pitch"));
			AC->bOverrideAttenuation = true;
			AC->AttenuationOverrides.FalloffDistance = R->GetNumberField(TEXT("radius"));
			AC->AttenuationOverrides.AttenuationShapeExtents = FVector(R->GetNumberField(TEXT("radius")) * 0.25f, 0, 0);
			P->Period = R->GetNumberField(TEXT("period"));
			P->Interval = R->GetNumberField(TEXT("interval"));
			P->PhaseShift = R->GetNumberField(TEXT("phase"));
			return P;
		}
		if (Type == TEXT("road_network"))
		{
			AMFRoadNetwork* Net = World->SpawnActor<AMFRoadNetwork>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
			for (const TSharedPtr<FJsonValue>& N : R->GetArrayField(TEXT("nodes")))
			{
				Net->Nodes.Add(Vec(N->AsObject(), TEXT("location")));
				Net->MaxSpeed.Add(uint8(FMath::Clamp(int32(N->AsObject()->GetNumberField(TEXT("max_speed"))), 0, 255)));
			}
			for (const TSharedPtr<FJsonValue>& L : R->GetArrayField(TEXT("edges")))
			{
				const TArray<TSharedPtr<FJsonValue>>& AB = L->AsArray();
				Net->Links.Add(FIntPoint(int32(AB[0]->AsNumber()), int32(AB[1]->AsNumber())));
			}
			return Net;
		}
		if (Type != TEXT("loadout_zone") && Type != TEXT("zone_volume") && Type != TEXT("zone_box"))
		{
			return nullptr;
		}
		const FVector Min = Vec(R, TEXT("box_min")), Max = Vec(R, TEXT("box_max"));
		const FVector Center = (Min + Max) * 0.5f, Extent = (Max - Min) * 0.5f;
		if (Type == TEXT("loadout_zone"))
		{
			AMFLoadoutZone* Z = World->SpawnActor<AMFLoadoutZone>(Center, FRotator::ZeroRotator, SP);
			Z->Volume->SetBoxExtent(Extent);
			Z->Team = Team(R->GetStringField(TEXT("team")));
			return Z;
		}
		if (Type == TEXT("zone_volume"))
		{
			// UE1 zone with gameplay (water, pain, current) -> a physics volume over its bounds
			AMFZoneVolume* Z = World->SpawnActor<AMFZoneVolume>(Center, FRotator::ZeroRotator, SP);
			MakeBoxBrush(World, Z, Extent);
			Z->bWaterVolume = R->GetBoolField(TEXT("water"));
			Z->DamagePerSec = R->GetNumberField(TEXT("damage_per_sec"));
			Z->ZoneVelocity = Vec(R, TEXT("velocity"));
			Z->ZoneName = R->GetStringField(TEXT("zone_name"));
			Z->Priority = Z->bWaterVolume ? 1 : 2;
			return Z;
		}
		AMFZoneBox* Z = World->SpawnActor<AMFZoneBox>(Center, FRotator::ZeroRotator, SP);
		Z->Volume->SetBoxExtent(Extent);
		Z->Tags.Add(FName(*R->GetStringField(TEXT("tag"))));
		return Z;
	}

	/** Generated maps are rebuilt from scratch: drop the old level + its built data first. */
	void DeleteOldMap(const FString& PackageName)
	{
		for (const FString& Pkg : { PackageName, PackageName + TEXT("_BuiltData") })
		{
			for (const FString& Ext : { FPackageName::GetMapPackageExtension(), FPackageName::GetAssetPackageExtension() })
			{
				const FString File = FPackageName::LongPackageNameToFilename(Pkg, Ext);
				if (IFileManager::Get().FileExists(*File))
				{
					IFileManager::Get().Delete(*File, false, true);
				}
			}
		}
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

	const FString PackageName = Packs.MapPackage(MapName);   // core: /Game/Maps, else the classic plugin
	TGuardValue<bool> CoreMaterials(bBuildingCoreMap, Packs.IsCoreMap(MapName));
	DeleteOldMap(PackageName);
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
		bool bSky = false;
		W->TryGetBoolField(TEXT("sky"), bSky);
		if (bRender && bSky)
		{
			// sky windows: drawn (unlit sky material) but transparent to light, so the sun and
			// the sky colour reach the outdoor areas through them
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->CastShadow = false;
			C->bAffectDynamicIndirectLighting = false;
			C->bOverrideLightMapRes = true;
			C->OverriddenLightMapRes = 4;
			A->SetActorLabel(FString::Printf(TEXT("Sky_%s"), *Mesh->GetName()));
		}
		else if (bRender)
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
			// Plain assignments: the Set*() functions do nothing on a registered Static light
			// (AreDynamicDataChangesAllowed), which left every light at UE's defaults.
			C->bUseInverseSquaredFalloff = false;   // UE1-style radial falloff
			C->LightFalloffExponent = 2.f;
			C->AttenuationRadius = R->GetNumberField(TEXT("radius"));
			C->Intensity = R->GetNumberField(TEXT("intensity"));
			C->LightColor = Color(R).ToFColor(true);
			C->CastShadows = true;
			C->SourceRadius = LightSourceRadius;
			C->SetMobility(EComponentMobility::Static);
			C->MarkRenderStateDirty();
			Actor = L;
		}
		else if (Type == TEXT("spot_light"))
		{
			ASpotLight* L = World->SpawnActor<ASpotLight>(Loc, Rotation, SP);
			USpotLightComponent* C = L->SpotLightComponent;
			C->bUseInverseSquaredFalloff = false;
			C->LightFalloffExponent = 2.f;
			C->AttenuationRadius = R->GetNumberField(TEXT("radius"));
			C->Intensity = R->GetNumberField(TEXT("intensity"));
			C->LightColor = Color(R).ToFColor(true);
			C->OuterConeAngle = R->GetNumberField(TEXT("cone"));
			C->InnerConeAngle = R->GetNumberField(TEXT("cone")) * 0.7f;
			C->SourceRadius = LightSourceRadius;
			C->SetMobility(EComponentMobility::Static);
			C->MarkRenderStateDirty();
			Actor = L;
		}
		else if (Type == TEXT("directional_light"))
		{
			ADirectionalLight* L = World->SpawnActor<ADirectionalLight>(Loc, Rotation, SP);
			UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(L->GetLightComponent());
			C->Intensity = R->GetNumberField(TEXT("intensity"));
			C->LightSourceAngle = 3.f;   // soft sunset shadows
			C->LightColor = Color(R).ToFColor(true);
			C->SetMobility(EComponentMobility::Static);
			C->MarkRenderStateDirty();
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
		else if (AActor* Data = SpawnMapData(World, R, SP, [this](const FString& S) { return FindSound(S); },
			[this, &MapDir, &MapName](const FString& M) { return LoadOrBuildMesh(MapDir, M, MapName); }))
		{
			Actor = Data;
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
			const TArray<TSharedPtr<FJsonValue>>* Amb = nullptr;
			if (R->TryGetArrayField(TEXT("ambient"), Amb) && Amb->Num() == 3)
			{
				Info->Ambient = FLinearColor((*Amb)[0]->AsNumber(), (*Amb)[1]->AsNumber(), (*Amb)[2]->AsNumber());
			}
			const TSharedPtr<FJsonObject>* MM = nullptr;
			if (R->TryGetObjectField(TEXT("minimap"), MM))
			{
				Info->MiniMap = FindTexture((*MM)->GetStringField(TEXT("texture")));
				const TArray<TSharedPtr<FJsonValue>>& B = (*MM)->GetArrayField(TEXT("bounds"));
				if (B.Num() == 4)
				{
					Info->MiniMapBounds = FVector4(B[0]->AsNumber(), B[1]->AsNumber(), B[2]->AsNumber(), B[3]->AsNumber());
				}
			}
			FString Song, SongPkg, SongName;
			if (R->TryGetStringField(TEXT("song"), Song) && Song.Split(TEXT("."), &SongPkg, &SongName))
			{
				// converted tracker music: <package root>/Music/<package>/<name>
				Info->Music = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("%s/Music/%s/%s.%s"), *Packs.PackageRoot(SongPkg), *SongPkg, *SongName, *SongName), nullptr, LOAD_NoWarn | LOAD_Quiet);
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
		// MF zone ambient light -> Lightmass sky colour (rays leaving through sky windows)
		const TArray<TSharedPtr<FJsonValue>>& C = (*LM)->GetArrayField(TEXT("environment_color"));
		WS->LightmassSettings.EnvironmentColor = FLinearColor(C[0]->AsNumber(), C[1]->AsNumber(), C[2]->AsNumber()).ToFColor(true);
		WS->LightmassSettings.EnvironmentIntensity = (*LM)->GetNumberField(TEXT("environment_intensity"));
		int32 Bounces = 2;
		(*LM)->TryGetNumberField(TEXT("bounces"), Bounces);
		WS->LightmassSettings.NumIndirectLightingBounces = Bounces;   // baked, free at runtime
	}
	WS->LightmassSettings.bUseAmbientOcclusion = false;
	// UE1 lighting came from light actors only; unlit/sky surfaces (signs, lamps, sky windows)
	// never lit anything. Lightmass would otherwise gather their emissive as light.
	WS->LightmassSettings.EmissiveBoost = 0.f;
	WS->LightmassSettings.StaticLightingLevelScale = 1.f;
	WS->bPrecomputeVisibility = true;   // cheap occlusion for mobile, built with lighting
	if (WorldBounds.IsValid)
	{
		// Falling out of the map kills (AMFCharacter::FellOutOfWorld -> respawn) instead of
		// falling for ever: 20 m below the lowest geometry
		WS->bEnableWorldBoundsChecks = true;
		WS->KillZ = WorldBounds.Min.Z - 2000.f;
	}

	if (WorldBounds.IsValid)
	{
		ALightmassImportanceVolume* Vol = World->SpawnActor<ALightmassImportanceVolume>(WorldBounds.GetCenter(), FRotator::ZeroRotator, SP);
		MakeBoxBrush(World, Vol, WorldBounds.GetExtent() + FVector(100.f));
		Vol->SetActorLabel(TEXT("LightmassImportance"));
		// navigation for bots (built by `mfport.py light` via -BuildNavigationData)
		ANavMeshBoundsVolume* Nav = World->SpawnActor<ANavMeshBoundsVolume>(WorldBounds.GetCenter(), FRotator::ZeroRotator, SP);
		MakeBoxBrush(World, Nav, WorldBounds.GetExtent() + FVector(200.f));
		Nav->SetActorLabel(TEXT("NavMeshBounds"));
		// precomputed visibility: cheap occlusion culling on mobile, baked with lighting
		APrecomputedVisibilityVolume* Vis = World->SpawnActor<APrecomputedVisibilityVolume>(WorldBounds.GetCenter(), FRotator::ZeroRotator, SP);
		MakeBoxBrush(World, Vis, WorldBounds.GetExtent());
		Vis->SetActorLabel(TEXT("PrecomputedVisibility"));
	}

	World->UpdateWorldComponents(true, false);
	// Ladder nav links need the collision that now exists
	for (TActorIterator<AMFLadderVolume> It(World); It; ++It)
	{
		It->PlaceNavLinks();
	}
	BuildNavigation(World);
	const bool bSaved = SaveAsset(World);
	UE_LOG(LogMFEditor, Display, TEXT("Map %s: %d actors -> %s (%s)"), *MapName, Spawned, *PackageName, bSaved ? TEXT("saved") : TEXT("SAVE FAILED"));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return bSaved;
}

bool FMFImporter::PatchMapData(const FString& MapName)
{
	// zone actors (water / pain / current volumes, loadout zones, captain boxes) and the road
	// graph into an already built (and baked) map: they don't touch the lighting, so no rebuild
	FString Text;
	TSharedPtr<FJsonObject> Level;
	const FString MapDir = WorkDir / TEXT("Maps") / MapName;
	if (!FFileHelper::LoadFileToString(Text, *(MapDir / TEXT("level.json"))) ||
		!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Level) || !Level)
	{
		return false;
	}
	const FString PackageName = Packs.MapPackage(MapName);
	UPackage* Pkg = LoadPackage(nullptr, *PackageName, LOAD_None);
	UWorld* World = Pkg ? UWorld::FindWorldInPackage(Pkg) : nullptr;
	if (!World)
	{
		UE_LOG(LogMFEditor, Warning, TEXT("Patch %s: map %s not built"), *MapName, *PackageName);
		return false;
	}
	World->AddToRoot();
	if (!World->bIsWorldInitialized)
	{
		UWorld::InitializationValues IVS;
		IVS.RequiresHitProxies(false).ShouldSimulatePhysics(false).EnableTraceCollision(false).CreateNavigation(false)
			.CreateAISystem(false).AllowAudioPlayback(false).CreatePhysicsScene(true);
		World->InitWorld(IVS);
		World->UpdateWorldComponents(true, false);
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
	Context.SetCurrentWorld(World);
	int32 Removed = 0, Added = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsA<AMFZoneVolume>() || It->IsA<AMFLoadoutZone>() || It->IsA<AMFZoneBox>() || It->IsA<AMFRoadNetwork>()
			|| It->IsA<AMFTeleporter>() || It->IsA<AMFPeriodicSound>() || It->IsA<AMFPickupSpot>() || It->IsA<AMFMover>() || It->IsA<AMFBlocker>() || It->IsA<AMFBotSpot>())
		{
			World->EditorDestroyActor(*It, true);
			++Removed;
		}
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const TSharedPtr<FJsonValue>& V : Level->GetArrayField(TEXT("spawns")))
	{
		const TSharedPtr<FJsonObject> R = V->AsObject();
		if (AActor* Data = SpawnMapData(World, R, SP, [this](const FString& S) { return FindSound(S); },
			[this, &MapDir, &MapName](const FString& M) { return LoadOrBuildMesh(MapDir, M, MapName); }))
		{
			Data->SetActorLabel(R->GetStringField(TEXT("name")));
			++Added;
		}
	}
	const bool bSaved = (Removed || Added) ? SaveAsset(World) : true;
	UE_LOG(LogMFEditor, Display, TEXT("Patch %s: %d map data actors (%d replaced)%s"), *MapName, Added, Removed, bSaved ? TEXT("") : TEXT(" SAVE FAILED"));
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return bSaved;
}

bool FMFImporter::BuildTestMap()
{
	const FString PackageName = TEXT("/Game/MF/Maps/MF_TestRange");
	DeleteOldMap(PackageName);
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

	auto Crate = [&](const FVector& Loc, uint8 Team)
	{
		AMFLoadoutCrate* C = World->SpawnActor<AMFLoadoutCrate>(Loc + FVector(0, 0, 60), FRotator::ZeroRotator, SP);
		C->Team = Team;
		C->bTeamPlay = Team != MF::NoTeam;
		C->bNonTeamPlay = Team == MF::NoTeam;
	};
	Crate(FVector(2000, 1700, 0), MF::NoTeam);
	Crate(FVector(-2000, 1700, 0), MF::NoTeam);
	Crate(FVector(1500, 300, 0), MF::NoTeam);
	Crate(FVector(2700, 0, 0), 0);
	Crate(FVector(-2700, 0, 0), 1);

	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 1000), FRotator(-50.f, 30.f, 0), SP);
	Sun->GetLightComponent()->Intensity = 2.5f;   // assign before Static: Set*() is ignored after
	Sun->GetLightComponent()->SetMobility(EComponentMobility::Static);
	APointLight* Lamp = World->SpawnActor<APointLight>(FVector(-1800, -1800, 700), FRotator::ZeroRotator, SP);
	Lamp->PointLightComponent->bUseInverseSquaredFalloff = false;
	Lamp->PointLightComponent->AttenuationRadius = 1500.f;
	Lamp->PointLightComponent->Intensity = 3.f;
	Lamp->PointLightComponent->LightColor = FLinearColor(1.f, 0.8f, 0.5f).ToFColor(true);
	Lamp->PointLightComponent->SetMobility(EComponentMobility::Static);

	AWorldSettings* WS = World->GetWorldSettings();
	WS->LightmassSettings.EnvironmentColor = FColor(90, 100, 120);
	WS->LightmassSettings.EnvironmentIntensity = 0.5f;
	ALightmassImportanceVolume* Vol = World->SpawnActor<ALightmassImportanceVolume>(FVector(0, 0, 300), FRotator::ZeroRotator, SP);
	MakeBoxBrush(World, Vol, FVector(3200, 3200, 500));
	ANavMeshBoundsVolume* Nav = World->SpawnActor<ANavMeshBoundsVolume>(FVector(0, 0, 300), FRotator::ZeroRotator, SP);
	MakeBoxBrush(World, Nav, FVector(3400, 3400, 700));

	AMFMapInfo* Info = World->SpawnActor<AMFMapInfo>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
	Info->Title = TEXT("Test Range");
	Info->Author = TEXT("Mobile Forces UE5 project");

	World->UpdateWorldComponents(true, false);
	BuildNavigation(World);
	const bool bSaved = SaveAsset(World);
	UE_LOG(LogMFEditor, Display, TEXT("Test map -> %s (%s)"), *PackageName, bSaved ? TEXT("saved") : TEXT("SAVE FAILED"));
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return bSaved;
}
