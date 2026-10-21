// MF sprite particles (Engine.RageSprite) -> particle systems. Tools/mfport/mflib/effects.py
// writes the emitter descriptions (UE1 units) and flipbook atlases (work/out/Effects); this
// builds them as Cascade systems: plain data, made without the editor, CPU-simulated sprites
// that sleep when off screen (SecondsBeforeInactive) - cheap enough for MF's rain-filled maps.
#include "MFImporter.h"
#include "MFImportUtil.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionFloatConstantCurve.h"
#include "Distributions/DistributionFloatUniform.h"
#include "Distributions/DistributionVectorConstant.h"
#include "Distributions/DistributionVectorUniform.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionTextureSampleParameterSubUV.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialShared.h"
#include "Particles/Acceleration/ParticleModuleAcceleration.h"
#include "Particles/Collision/ParticleModuleCollision.h"
#include "Particles/Color/ParticleModuleColor.h"
#include "Particles/Color/ParticleModuleColorScaleOverLife.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/Location/ParticleModuleLocation.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSpriteEmitter.h"
#include "Particles/ParticleSystem.h"
#include "Particles/Rotation/ParticleModuleRotation.h"
#include "Particles/RotationRate/ParticleModuleRotationRate.h"
#include "Particles/Size/ParticleModuleSize.h"
#include "Particles/Size/ParticleModuleSizeScaleBySpeed.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/SubUV/ParticleModuleSubUV.h"
#include "Particles/SubUV/ParticleModuleSubUVMovie.h"
#include "Particles/Velocity/ParticleModuleVelocity.h"

using namespace MFImportUtil;

namespace
{
	constexpr float Uu = 2.25f;
	constexpr float MFGravity = -950.f;      // ZoneInfo.ZoneGravity, uu/s^2
	constexpr float LoopFps = 16.f;          // frames a second of sprites that loop their animation
	constexpr float FullRotation = 1.f;      // turns a second at ROTRATE FULL

	FVector JVec(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O->TryGetArrayField(Field, A) || A->Num() < 3)
		{
			return FVector::ZeroVector;
		}
		return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
	}

	float JNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, float Default = 0.f)
	{
		double V = Default;
		O->TryGetNumberField(Field, V);
		return float(V);
	}

	bool JBool(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		bool B = false;
		O->TryGetBoolField(Field, B);
		return B;
	}

	UDistributionFloat* Const(UObject* Outer, float V)
	{
		UDistributionFloatConstant* D = NewObject<UDistributionFloatConstant>(Outer);
		D->Constant = V;
		return D;
	}

	UDistributionFloat* Range(UObject* Outer, float Lo, float Hi)
	{
		if (FMath::IsNearlyEqual(Lo, Hi))
		{
			return Const(Outer, Lo);
		}
		UDistributionFloatUniform* D = NewObject<UDistributionFloatUniform>(Outer);
		D->Min = Lo;
		D->Max = Hi;
		return D;
	}

	/** Keys over time; bStep: each value held until the next key (else linear). */
	UDistributionFloat* Curve(UObject* Outer, const TArray<FVector2D>& Keys, bool bStep)
	{
		UDistributionFloatConstantCurve* D = NewObject<UDistributionFloatConstantCurve>(Outer);
		for (const FVector2D& K : Keys)
		{
			const int32 I = D->ConstantCurve.AddPoint(K.X, K.Y);
			D->ConstantCurve.Points[I].InterpMode = bStep ? CIM_Constant : CIM_Linear;
		}
		return D;
	}

	UDistributionVector* VRange(UObject* Outer, const FVector& Lo, const FVector& Hi, bool bLockXYZ = false)
	{
		if (Lo.Equals(Hi))
		{
			UDistributionVectorConstant* D = NewObject<UDistributionVectorConstant>(Outer);
			D->Constant = Lo;
			return D;
		}
		UDistributionVectorUniform* D = NewObject<UDistributionVectorUniform>(Outer);
		D->Min = Lo;
		D->Max = Hi;
		D->LockedAxes = bLockXYZ ? EDVLF_XYZ : EDVLF_None;
		return D;
	}

	/** Modules belong to the system (ClassWithin ParticleSystem), listed per LOD level. */
	/** effects.py draw -> asset name part: M_MF_Particle_Additive, MI_<texture>_Subtractive ... */
	FString ParticleDrawName(const FString& Draw)
	{
		return Draw == TEXT("add") ? TEXT("Additive") : Draw == TEXT("sub") ? TEXT("Subtractive") : TEXT("Translucent");
	}

	template <typename T>
	T* AddModule(UParticleLODLevel* LOD)
	{
		T* M = NewObject<T>(LOD->GetOuter()->GetOuter());
		M->LODValidity = 1;
		LOD->Modules.Add(M);
		return M;
	}
}

bool FMFImporter::LoadEffects()
{
	if (Flipbooks.Num())
	{
		return true;
	}
	EffectsManifest = LoadJson(WorkDir / TEXT("Effects/effects.json"));
	if (!EffectsManifest)
	{
		UE_LOG(LogMFEditor, Warning, TEXT("No Effects/effects.json - run mfport.py effects"));
		return false;
	}
	for (const auto& Pair : EffectsManifest->GetObjectField(TEXT("flipbooks"))->Values)
	{
		Flipbooks.Add(FString(Pair.Key).ToLower(), Pair.Value->AsObject());
	}
	return true;
}

FString FMFImporter::FlipbookAssetPath(const FString& Key) const
{
	// the texture's own spelling ("RageEffects.DenseSmoke.DenseSmoke_A01"), keys are lower case
	const TSharedPtr<FJsonObject>* Book = Flipbooks.Find(Key.ToLower());
	FString Dir, Name;
	SplitKey(Book ? (*Book)->GetStringField(TEXT("key")) : Key, Dir, Name);
	return FString::Printf(TEXT("%s/Effects/Textures/%s/%s.%s"), *Packs.TextureRoot(Key), *Dir, *Name, *Name);
}

bool FMFImporter::ImportEffects()
{
	if (!LoadEffects())
	{
		return false;
	}
	// flipbook atlases
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UAssetImportTask*> Tasks;
	TArray<FString> Keys;
	for (const auto& Pair : Flipbooks)
	{
		const TSharedPtr<FJsonObject>& Info = Pair.Value;
		FString Dir, Name;
		SplitKey(Info->GetStringField(TEXT("key")), Dir, Name);
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = FPaths::ConvertRelativePathToFull(WorkDir / Info->GetStringField(TEXT("file")));
		Task->DestinationPath = FString::Printf(TEXT("%s/Effects/Textures/%s"), *Packs.TextureRoot(Pair.Key), *Dir);
		Task->DestinationName = Name;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		Tasks.Add(Task);
		Keys.Add(Pair.Key);
	}
	AssetTools.ImportAssetTasks(Tasks);
	int32 Ok = 0;
	for (const FString& Key : Keys)
	{
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *FlipbookAssetPath(Key), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Tex)
		{
			UE_LOG(LogMFEditor, Warning, TEXT("Flipbook import failed: %s"), *Key);
			continue;
		}
		Tex->LODGroup = TEXTUREGROUP_Effects;
		Tex->NeverStream = true;
		Tex->SRGB = true;
		Tex->CompressionSettings = TC_Default;
		Tex->CompressionNoAlpha = false;
		Tex->PostEditChange();
		Ok += SaveAsset(Tex) ? 1 : 0;
	}
	UE_LOG(LogMFEditor, Display, TEXT("Effects: %d / %d flipbooks"), Ok, Keys.Num());
	// the scripted effects (RageEffects.Explosion ...)
	int32 Systems = 0;
	const FString Root = Packs.PackageRoot(TEXT("RageEffects")) / TEXT("Effects");
	for (const auto& Pair : EffectsManifest->GetObjectField(TEXT("systems"))->Values)
	{
		Systems += BuildParticleSystem(Root, TEXT("P_") + FString(Pair.Key), Pair.Value->AsArray(), false) ? 1 : 0;
	}
	UE_LOG(LogMFEditor, Display, TEXT("Effects: %d particle systems"), Systems);
	return Ok > 0;
}

UMaterialInterface* FMFImporter::GetParticleMaster(const FString& Draw)
{
	const FString Mode = TEXT("Particle_") + ParticleDrawName(Draw);
	if (UMaterialInterface** Found = MasterCache.Find(Mode))
	{
		return *Found;
	}
	bool bCreated = false;
	UMaterial* M = FindOrCreateAsset<UMaterial>(TEXT("/Game/MF/Materials"), TEXT("M_MF_") + Mode, bCreated);
	if (bCreated)
	{
		// RageSprite draw types: additive (fire), subtractive (dark smoke: the scene times
		// 1 - texture, as UE has no subtract blend), alpha (blood, masked bubbles and leaves);
		// ParticleColor carries the alpha and its fade
		UMaterialEditorOnlyData* E = M->GetEditorOnlyData();
		UMaterialExpressionTextureSampleParameterSubUV* Tex = NewObject<UMaterialExpressionTextureSampleParameterSubUV>(M);
		Tex->ParameterName = TEXT("Texture");
		Tex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		Tex->bBlend = false;
		M->GetExpressionCollection().AddExpression(Tex);
		UMaterialExpressionParticleColor* PC = NewObject<UMaterialExpressionParticleColor>(M);
		M->GetExpressionCollection().AddExpression(PC);
		auto Mul = [M](UMaterialExpression* A, int32 AOut, UMaterialExpression* B, int32 BOut)
		{
			UMaterialExpressionMultiply* X = NewObject<UMaterialExpressionMultiply>(M);
			X->A.Connect(AOut, A);
			X->B.Connect(BOut, B);
			M->GetExpressionCollection().AddExpression(X);
			return X;
		};
		M->SetShadingModel(MSM_Unlit);
		// the texture's alpha weighs its colour in every mode (Rage effect palettes: white under
		// see-through texels)
		UMaterialExpressionMultiply* Weighted = Mul(Tex, 0, Tex, 4);
		if (Draw == TEXT("add"))
		{
			M->BlendMode = BLEND_Additive;
			E->EmissiveColor.Connect(0, Mul(Mul(Weighted, 0, PC, 0), 0, PC, 4));
		}
		else if (Draw == TEXT("sub"))
		{
			M->BlendMode = BLEND_Modulate;
			UMaterialExpressionOneMinus* Inv = NewObject<UMaterialExpressionOneMinus>(M);
			Inv->Input.Connect(0, Mul(Weighted, 0, PC, 4));
			M->GetExpressionCollection().AddExpression(Inv);
			E->EmissiveColor.Connect(0, Inv);
		}
		else
		{
			M->BlendMode = BLEND_Translucent;
			E->EmissiveColor.Connect(0, Mul(Tex, 0, PC, 0));
			E->Opacity.Connect(0, Mul(Tex, 4, PC, 4));
		}
		M->SetMaterialUsage(MATUSAGE_ParticleSprites);
		M->PreEditChange(nullptr);
		M->PostEditChange();
		SaveAsset(M);
		UE_LOG(LogMFEditor, Display, TEXT("Created master material M_MF_%s"), *Mode);
	}
	MasterCache.Add(Mode, M);
	return M;
}

UMaterialInterface* FMFImporter::GetParticleMaterial(const FString& Key, const FString& Draw)
{
	const FString CacheKey = Key + TEXT("|particle_") + Draw;
	if (UMaterialInterface** Found = MaterialCache.Find(CacheKey))
	{
		return *Found;
	}
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *FlipbookAssetPath(Key), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Tex)
	{
		UE_LOG(LogMFEditor, Warning, TEXT("No flipbook for %s (mfport.py effects, import --steps effects)"), *Key);
		return nullptr;
	}
	FString Dir, Name;
	SplitKey(Flipbooks[Key.ToLower()]->GetStringField(TEXT("key")), Dir, Name);
	bool bCreated = false;
	UMaterialInstanceConstant* MI = FindOrCreateAsset<UMaterialInstanceConstant>(
		FString::Printf(TEXT("%s/Effects/Materials/%s"), *Packs.TextureRoot(Key), *Dir), FString::Printf(TEXT("MI_%s_%s"), *Name, *ParticleDrawName(Draw)), bCreated);
	MI->SetParentEditorOnly(GetParticleMaster(Draw));
	MI->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Texture")), Tex);
	MI->PostEditChange();
	SaveAsset(MI);
	MaterialCache.Add(CacheKey, MI);
	return MI;
}

UParticleSystem* FMFImporter::BuildParticleSystem(const FString& PackagePath, const FString& Name, const TArray<TSharedPtr<FJsonValue>>& Emitters, bool bLoop)
{
	if (!LoadEffects())
	{
		return nullptr;
	}
	bool bCreated = false;
	UParticleSystem* PS = FindOrCreateAsset<UParticleSystem>(PackagePath, Name, bCreated);
	PS->Emitters.Reset();
	PS->LODDistances = { 0.f };
	PS->LODSettings = { FParticleSystemLOD::CreateParticleSystemLOD() };
	PS->SecondsBeforeInactive = 1.f;   // off screen for a second: stops simulating
	float Longest = 0.f;
	for (const TSharedPtr<FJsonValue>& Value : Emitters)
	{
		const TSharedPtr<FJsonObject> E = Value->AsObject();
		const FString Key = E->GetStringField(TEXT("tex")).ToLower();
		const TSharedPtr<FJsonObject>* Book = Flipbooks.Find(Key);
		UMaterialInterface* Mat = Book ? GetParticleMaterial(Key, E->GetStringField(TEXT("draw"))) : nullptr;
		if (!Mat)
		{
			UE_LOG(LogMFEditor, Warning, TEXT("%s: no texture %s, emitter left out"), *Name, *Key);
			continue;
		}
		const int32 Frames = (*Book)->GetIntegerField(TEXT("frames"));
		UParticleSpriteEmitter* Em = NewObject<UParticleSpriteEmitter>(PS);
		Em->EmitterName = FName(*FPaths::GetBaseFilename(Key.Replace(TEXT("."), TEXT("/"))));
		if (!Em->GetLODLevel(0))
		{
			Em->CreateLODLevel(0);
		}
		UParticleLODLevel* LOD = Em->GetLODLevel(0);
		LOD->Modules.Reset();
		const float Life = FMath::Max(JNum(E, TEXT("life"), 1.f), 0.05f);

		// timing: a burst at the start, a rate on and off (map emitters), or a rate over time
		UParticleModuleRequired* Req = LOD->RequiredModule;
		Req->Material = Mat;
		Req->SubImages_Horizontal = (*Book)->GetIntegerField(TEXT("cols"));
		Req->SubImages_Vertical = (*Book)->GetIntegerField(TEXT("rows"));
		Req->InterpolationMethod = Frames > 1 ? PSUVIM_Linear : PSUVIM_None;
		Req->bUseLocalSpace = false;
		Req->bKillOnDeactivate = false;
		Req->ScreenAlignment = JNum(E, TEXT("stretch")) > 0.f ? PSA_Velocity : PSA_Square;
		UParticleModuleSpawn* Spawn = LOD->SpawnModule;
		Spawn->RateScale.Distribution = Const(Spawn, 1.f);
		Spawn->BurstList.Reset();
		const TArray<TSharedPtr<FJsonValue>>* RateKeys = nullptr;
		if (E->HasField(TEXT("burst")))
		{
			Spawn->Rate.Distribution = Const(Spawn, 0.f);
			FParticleBurst B;
			B.Count = E->GetIntegerField(TEXT("burst"));
			B.CountLow = -1;
			B.Time = 0.f;
			Spawn->BurstList.Add(B);
			Req->EmitterDuration = Req->EmitterDurationLow = Life;
			Req->EmitterLoops = 1;
		}
		else if (E->TryGetArrayField(TEXT("rate_keys"), RateKeys))
		{
			TArray<FVector2D> Keys;
			for (const TSharedPtr<FJsonValue>& K : *RateKeys)
			{
				Keys.Add(FVector2D(K->AsArray()[0]->AsNumber(), K->AsArray()[1]->AsNumber()));
			}
			Spawn->Rate.Distribution = Curve(Spawn, Keys, true);
			Req->EmitterDuration = Req->EmitterDurationLow = Keys.Num() ? FMath::Max(Keys.Last().X, 0.1f) : 1.f;
			Req->EmitterLoops = 1;
		}
		else
		{
			// ParticleEffect.Tick: Frequency a second for Period s, then nothing for Interval s;
			// PhaseShift s into the cycle at the start (negative: later)
			const float Rate = JNum(E, TEXT("rate"));
			float Period = JNum(E, TEXT("period"), 1000.f), Interval = JNum(E, TEXT("interval"));
			if (Period >= 1000.f)
			{
				Period = 1.f;
				Interval = 0.f;
			}
			const float Cycle = FMath::Max(Period + Interval, 0.05f);
			Spawn->Rate.Distribution = Interval > 0.f ? Curve(Spawn, { FVector2D(0.f, Rate), FVector2D(Period, 0.f) }, true) : Const(Spawn, Rate);
			Req->EmitterDuration = Req->EmitterDurationLow = Cycle;
			Req->EmitterLoops = bLoop || JBool(E, TEXT("loop")) ? 0 : 1;
			const float Phase = JNum(E, TEXT("phase"));
			Req->EmitterDelay = Req->EmitterDelayLow = Phase < 0.f ? -Phase : FMath::Fmod(Cycle - FMath::Fmod(Phase, Cycle), Cycle);
			Req->bDelayFirstLoopOnly = true;
		}
		Req->bKillOnCompleted = Req->EmitterLoops != 0;
		Longest = FMath::Max(Longest, Req->EmitterDelay + Req->EmitterDuration + Life);

		UParticleModuleLifetime* LifeM = AddModule<UParticleModuleLifetime>(LOD);
		LifeM->Lifetime.Distribution = Const(LifeM, Life);

		// RageSprite Size x the texture's width (UE1 sprites: DrawScale x USize)
		const TArray<TSharedPtr<FJsonValue>>& S = E->GetArrayField(TEXT("size"));
		const float W = (*Book)->GetNumberField(TEXT("usize")) * Uu, H = (*Book)->GetNumberField(TEXT("vsize")) * Uu;
		UParticleModuleSize* SizeM = AddModule<UParticleModuleSize>(LOD);
		SizeM->StartSize.Distribution = VRange(SizeM, FVector(W, H, W) * S[0]->AsNumber(), FVector(W, H, W) * S[1]->AsNumber(), true);

		// AddOne(Location + SetLocation + VRand() * RandomLocation, ...): a box here
		const FVector Loc = JVec(E, TEXT("loc")) * Uu, LocRand = JVec(E, TEXT("loc_rand")).GetAbs() * Uu;
		if (!Loc.IsZero() || !LocRand.IsZero())
		{
			UParticleModuleLocation* LocM = AddModule<UParticleModuleLocation>(LOD);
			LocM->StartLocation.Distribution = VRange(LocM, Loc - LocRand, Loc + LocRand);
		}
		const FVector Vel = JVec(E, TEXT("vel")) * Uu, VelRand = JVec(E, TEXT("vel_rand")).GetAbs() * Uu;
		const float Radial = JNum(E, TEXT("vel_radial")) * Uu;
		if (!Vel.IsZero() || !VelRand.IsZero() || Radial != 0.f)
		{
			UParticleModuleVelocity* VelM = AddModule<UParticleModuleVelocity>(LOD);
			VelM->StartVelocity.Distribution = VRange(VelM, Vel - VelRand, Vel + VelRand);
			VelM->StartVelocityRadial.Distribution = Const(VelM, Radial);
			VelM->bInWorldSpace = false;   // turns with the effect: wall hits fly off the wall
		}

		UParticleModuleColor* ColorM = AddModule<UParticleModuleColor>(LOD);
		ColorM->StartColor.Distribution = VRange(ColorM, FVector::OneVector, FVector::OneVector);
		ColorM->StartAlpha.Distribution = Const(ColorM, JNum(E, TEXT("alpha"), 1.f));
		ColorM->bClampAlpha = true;
		if (JBool(E, TEXT("fade")))
		{
			UParticleModuleColorScaleOverLife* Fade = AddModule<UParticleModuleColorScaleOverLife>(LOD);
			Fade->ColorScaleOverLife.Distribution = VRange(Fade, FVector::OneVector, FVector::OneVector);
			Fade->AlphaScaleOverLife.Distribution = Curve(Fade, { FVector2D(0.f, 1.f), FVector2D(1.f, 0.f) }, false);
		}

		// the AnimNext chain: once over the particle's life, or round and round
		if (Frames > 1 && JBool(E, TEXT("anim_once")))
		{
			UParticleModuleSubUV* Sub = AddModule<UParticleModuleSubUV>(LOD);
			Sub->SubImageIndex.Distribution = Curve(Sub, { FVector2D(0.f, 0.f), FVector2D(1.f, Frames - 0.01f) }, false);
		}
		else if (Frames > 1)
		{
			UParticleModuleSubUVMovie* Movie = AddModule<UParticleModuleSubUVMovie>(LOD);
			Movie->FrameRate.Distribution = Const(Movie, LoopFps);
			Movie->StartingFrame = 1;
			Movie->bUseEmitterTime = false;
		}

		if (const float Spin = JNum(E, TEXT("rotate")) * FullRotation; Spin > 0.f)
		{
			UParticleModuleRotation* RotM = AddModule<UParticleModuleRotation>(LOD);
			RotM->StartRotation.Distribution = Range(RotM, 0.f, 1.f);
			UParticleModuleRotationRate* RateM = AddModule<UParticleModuleRotationRate>(LOD);
			RateM->StartRotationRate.Distribution = Range(RateM, -Spin, Spin);
		}
		if (JBool(E, TEXT("gravity")))
		{
			UParticleModuleAcceleration* Acc = AddModule<UParticleModuleAcceleration>(LOD);
			Acc->Acceleration.Distribution = VRange(Acc, FVector(0.f, 0.f, MFGravity * Uu), FVector(0.f, 0.f, MFGravity * Uu));
			Acc->bAlwaysInWorldSpace = true;
		}
		if (const float Stretch = JNum(E, TEXT("stretch")); Stretch > 0.f)
		{
			// flags 256 + 512/1024: drawn along the velocity, longer the faster
			UParticleModuleSizeScaleBySpeed* Speed = AddModule<UParticleModuleSizeScaleBySpeed>(LOD);
			Speed->SpeedScale = FVector2D(0.f, 0.01f * Stretch);
			Speed->MaxScale = FVector2D(1.f, 8.f);
		}
		if (JBool(E, TEXT("collide")) || JBool(E, TEXT("kill_on_collide")))
		{
			UParticleModuleCollision* Col = AddModule<UParticleModuleCollision>(LOD);
			const bool bKill = JBool(E, TEXT("kill_on_collide"));
			Col->DampingFactor.Distribution = VRange(Col, FVector(0.5f), FVector(0.5f));
			Col->MaxCollisions.Distribution = Const(Col, bKill ? 1.f : 3.f);
			Col->CollisionCompletionOption = bKill ? EPCC_Kill : EPCC_Freeze;
		}
		Em->UpdateModuleLists();
		Em->PostEditChange();
		PS->Emitters.Add(Em);
	}
	if (PS->Emitters.Num() == 0)
	{
		return nullptr;
	}
	PS->PostEditChange();
	SaveAsset(PS);
	UE_LOG(LogMFEditor, Display, TEXT("Particle system %s/%s: %d emitter(s), %.1f s"), *PackagePath, *Name, PS->Emitters.Num(), Longest);
	return PS;
}
