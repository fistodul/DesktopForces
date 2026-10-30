#include "Core/DFDownloads.h"
#include "ChunkDownloader.h"
#include "Core/DFContent.h"
#include "Core/DFGameSettings.h"
#include "Core/DFUserSettings.h"
#include "DesktopForces.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "DFDownloads"

namespace {
	/** A name for ChunkDownloader's builds (its config section can be per deployment: unused). */
	const TCHAR* Deployment = TEXT("DesktopForces");

	/** Where ChunkDownloader keeps its files (FChunkDownloader::Initialize). */
	FString CacheDir() {
		return FPaths::ProjectPersistentDownloadDir() / TEXT("PakCache/");
	}

	/** The downloaded build manifest it saved there: "$KEY = value" lines, then
	 *  name <tab> size <tab> version <tab> chunk <tab> url per file. */
	FString ManifestPath() {
		return CacheDir() / TEXT("CachedBuildManifest.txt");
	}
} // namespace

UDFDownloads* UDFDownloads::Get(const UObject* WorldContext) {
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UDFDownloads>() : nullptr;
}

void UDFDownloads::Initialize(FSubsystemCollectionBase& Collection) {
	Super::Initialize(Collection);
	// written by Tools/package.py --downloads: the paks on the server must come from the same cook
	FString Id;
	if (FFileHelper::LoadFileToString(Id, *(FPaths::ProjectContentDir() / TEXT("Downloads/ContentBuildId.txt"))))
		Id.TrimStartAndEndInline();
	if (Id.IsEmpty() || !FCoreDelegates::MountPak.IsBound()) {
		UE_LOG(LogDF, Log, TEXT("Downloads: off (%s)"), Id.IsEmpty() ? TEXT("no content build id") : TEXT("not running from paks"));
		return;
	}
	BuildId = Id;
	SetServer(UDFUserSettings::Get().DownloadServer);
	StartDownloader();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDFDownloads::Tick), 0.25f);
}

void UDFDownloads::SetServer(const FString& PlayerServer) {
	Server = PlayerServer.TrimStartAndEnd();
	if (Server.IsEmpty())
		Server = UDFGameSettings::Get().DownloadServer;
	Server.RemoveFromEnd(TEXT("/"));
	// ChunkDownloader reads its servers from this config section when it loads a build; one
	// folder per platform on the server, a build's in it: <server>/Android/<build id>/
	GConfig->SetArray(TEXT("/Script/Plugins.ChunkDownloader"), TEXT("CdnBaseUrls"), { Server / FPlatformProperties::IniPlatformName() }, GGameIni);
	UE_LOG(LogDF, Log, TEXT("Downloads: content build %s from %s"), *BuildId, *Server);
}

void UDFDownloads::UseServer(const FString& PlayerServer) {
	if (!IsAvailable())
		return;
	SetServer(PlayerServer);
	// ChunkDownloader keeps the servers it was given: a new one, and the manifest again
	IFileManager::Get().Delete(*ManifestPath(), false, true, true);
	Restart();
}

void UDFDownloads::Deinitialize() {
	FTSTicker::RemoveTicker(TickHandle);
	if (IsAvailable())
		FChunkDownloader::Shutdown();
	Super::Deinitialize();
}

void UDFDownloads::StartDownloader() {
	TSharedRef<FChunkDownloader> D = FChunkDownloader::GetOrCreate();
	D->Initialize(FPlatformProperties::IniPlatformName(), 1); // one file at a time: each one is held in memory
	TWeakObjectPtr<UDFDownloads> WeakThis(this);
	D->OnDownloadAnalytics = [WeakThis](const FString&, const FString&, uint64, const FTimespan&, int32) {
		if (const UDFDownloads* Self = WeakThis.Get())
			Self->SaveLocalManifest();
	};
	// the manifest saved last time, if it is this build's: installed packs mount right away
	if (D->LoadCachedBuild(Deployment) && D->GetContentBuildId() == BuildId)
		OnManifest();
	// else (first run, a new build) it comes from the server; retried until it does
	const int32 ForGeneration = Generation;
	D->UpdateBuild(Deployment, BuildId, [WeakThis, ForGeneration](bool bSuccess) {
		if (UDFDownloads* Self = WeakThis.Get(); Self && bSuccess && ForGeneration == Self->Generation)
			Self->OnManifest();
	});
}

void UDFDownloads::Restart() {
	// ChunkDownloader can't forget one chunk or cancel one download: make a new one (it reads what
	// is on disk again); the download in progress goes back to the front of the queue
	++Generation;
	if (Active != INDEX_NONE) {
		Queue.Insert(Active, 0);
		Active = INDEX_NONE;
	}
	FChunkDownloader::Shutdown();
	bManifestReady = false;
	StartDownloader();
	StartNext();
	Refresh();
}

void UDFDownloads::OnManifest() {
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *ManifestPath());
	Files.Reset();
	for (const FString& Line : Lines) {
		TArray<FString> Fields;
		if (Line.StartsWith(TEXT("$")) || Line.ParseIntoArray(Fields, TEXT("\t"), false) < 5)
			continue;
		FFile& F = Files.AddDefaulted_GetRef();
		F.Name = Fields[0];
		F.Size = FCString::Strtoui64(*Fields[1], nullptr, 10);
		F.Version = Fields[2];
		F.Chunk = FCString::Atoi(*Fields[3]);
	}
	Bundled.Reset();
	for (const FFile& F : Files)
		if (F.Name.EndsWith(TEXT(".pak")) && (IFileManager::Get().FileExists(*(FPaths::ProjectContentDir() / TEXT("Paks") / F.Name)) || IFileManager::Get().FileExists(*(FPaths::ProjectSavedDir() / TEXT("Paks") / F.Name))))
			Bundled.Add(F.Chunk);
	bManifestReady = true;
	UE_LOG(LogDF, Log, TEXT("Downloads: manifest of %s, %d files, %d packs with the install"), *BuildId, Files.Num(), Bundled.Num());
	// packs downloaded before: mount them
	TSharedPtr<FChunkDownloader> D = FChunkDownloader::Get();
	TArray<int32> Chunks;
	D->GetAllChunkIds(Chunks);
	bool bMounted = false;
	for (int32 Chunk : Chunks)
		if (!Mounted.Contains(Chunk) && !Bundled.Contains(Chunk) && Active != Chunk && D->GetChunkStatus(Chunk) == FChunkDownloader::EChunkStatus::Cached)
			bMounted |= Mount(Chunk);
	if (bMounted)
		OnContentChanged.Broadcast();
	StartNext();
	Refresh();
}

void UDFDownloads::Download(int32 Chunk) {
	if (!bManifestReady || Mounted.Contains(Chunk) || Bundled.Contains(Chunk) || Active == Chunk || Queue.Contains(Chunk) || !Files.ContainsByPredicate([Chunk](const FFile& F) { return F.Chunk == Chunk; }))
		return;
	Failed.Remove(Chunk);
	Queue.Add(Chunk);
	StartNext();
	Refresh();
}

void UDFDownloads::Cancel(int32 Chunk) {
	if (Queue.Remove(Chunk)) {
		Refresh();
	} else if (Active == Chunk) {
		UE_LOG(LogDF, Log, TEXT("Downloads: pack %d cancelled"), Chunk);
		Active = INDEX_NONE;
		Restart();
	}
}

void UDFDownloads::Delete(int32 Chunk) {
	if (Bundled.Contains(Chunk))
		return;
	Queue.Remove(Chunk);
	Failed.Remove(Chunk);
	const bool bWasActive = Active == Chunk;
	if (bWasActive)
		Active = INDEX_NONE;
	const bool bWasMounted = Mounted.Remove(Chunk) > 0;
	if (bWasMounted)
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); // let go of anything loaded from it
	// a running download writes its file when done: stop it before deleting
	FChunkDownloader::Shutdown();
	for (const FFile& F : Files)
		if (F.Chunk == Chunk) {
			const FString Path = CacheDir() / F.Name;
			if (bWasMounted && F.Name.EndsWith(TEXT(".pak")) && FCoreDelegates::OnUnmountPak.IsBound())
				FCoreDelegates::OnUnmountPak.Execute(Path); // its .utoc/.ucas container too
			IFileManager::Get().Delete(*Path, false, true, true);
		}
	UE_LOG(LogDF, Log, TEXT("Downloads: pack %d deleted"), Chunk);
	Restart();
	if (bWasMounted) {
		DF::ResetContentLookups();
		OnContentChanged.Broadcast();
	}
}

void UDFDownloads::StartNext() {
	TSharedPtr<FChunkDownloader> D = FChunkDownloader::Get();
	if (Active != INDEX_NONE || Queue.IsEmpty() || !bManifestReady || !D)
		return;
	Active = Queue[0];
	Queue.RemoveAt(0);
	ActiveBase = BytesOnDisk(Active);
	StatsBase = D->GetLoadingStats().BytesDownloaded;
	UE_LOG(LogDF, Log, TEXT("Downloads: pack %d (%llu bytes on disk already)"), Active, ActiveBase);
	TWeakObjectPtr<UDFDownloads> WeakThis(this);
	const int32 Chunk = Active, ForGeneration = Generation;
	D->DownloadChunk(Chunk, [WeakThis, Chunk, ForGeneration](bool bSuccess) {
		if (UDFDownloads* Self = WeakThis.Get())
			Self->OnDownloaded(Chunk, bSuccess, ForGeneration);
	});
}

void UDFDownloads::OnDownloaded(int32 Chunk, bool bSuccess, int32 ForGeneration) {
	if (ForGeneration != Generation || Chunk != Active)
		return; // cancelled
	Active = INDEX_NONE;
	SaveLocalManifest();
	if (bSuccess && Mount(Chunk)) {
		OnContentChanged.Broadcast();
	} else {
		Failed.Add(Chunk);
		UE_LOG(LogDF, Warning, TEXT("Downloads: pack %d failed: %s"), Chunk, *GetError().ToString());
	}
	StartNext();
	Refresh();
}

bool UDFDownloads::Mount(int32 Chunk) {
	bool bAny = false, bAll = true;
	for (const FFile& F : Files)
		if (F.Chunk == Chunk && F.Name.EndsWith(TEXT(".pak"))) {
			const bool bOk = FCoreDelegates::MountPak.Execute(CacheDir() / F.Name, INDEX_NONE) != nullptr;
			UE_LOG(LogDF, Log, TEXT("Downloads: mount %s: %s"), *F.Name, bOk ? TEXT("ok") : TEXT("FAILED"));
			bAny |= bOk;
			bAll &= bOk;
		}
	if (bAny) {
		Mounted.Add(Chunk);
		DF::ResetContentLookups(); // content that was missing may be here now
	}
	return bAny && bAll;
}

uint64 UDFDownloads::BytesOnDisk(int32 Chunk) const {
	uint64 Bytes = 0;
	for (const FFile& F : Files)
		if (F.Chunk == Chunk)
			Bytes += FMath::Max<int64>(IFileManager::Get().FileSize(*(CacheDir() / F.Name)), 0);
	return Bytes;
}

void UDFDownloads::SaveLocalManifest() const {
	// ChunkDownloader's record of what it has on disk, read when it starts: it writes it only when
	// a chunk's download starts, so after a restart it forgot every file finished since (the pack
	// looked half there, nothing to resume from). Its format: name, size, version, -1, "/".
	FString Text;
	int32 Num = 0;
	for (const FFile& F : Files)
		if (IFileManager::Get().FileSize(*(CacheDir() / F.Name)) == int64(F.Size)) {
			Text += FString::Printf(TEXT("%s\t%llu\t%s\t-1\t/\n"), *F.Name, F.Size, *F.Version);
			++Num;
		}
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("$NUM_ENTRIES = %d\n"), Num) + Text, *(CacheDir() / TEXT("LocalManifest.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FText UDFDownloads::GetError() const {
	TSharedPtr<FChunkDownloader> D = FChunkDownloader::Get();
	return D ? D->GetLoadingStats().LastError : FText();
}

void UDFDownloads::Refresh() {
	TSharedPtr<FChunkDownloader> D = FChunkDownloader::Get();
	TMap<int32, uint64> Sizes;
	for (const FFile& F : Files)
		Sizes.FindOrAdd(F.Chunk) += F.Size;
	Sizes.KeySort(TLess<int32>());
	const TArray<FDFPackStatus> Old = MoveTemp(Packs);
	for (const TPair<int32, uint64>& Pair : Sizes) {
		FDFPackStatus& P = Packs.AddDefaulted_GetRef();
		P.Chunk = Pair.Key;
		P.Size = Pair.Value;
		if (const FDFDownloadPack* Info = UDFGameSettings::Get().DownloadPacks.FindByPredicate([&P](const FDFDownloadPack& I) { return I.Chunk == P.Chunk; })) {
			P.Name = Info->Name;
			P.Description = Info->Description;
		} else {
			P.Name = FText::Format(LOCTEXT("PackN", "Pack {0}"), P.Chunk);
		}
		P.bBundled = Bundled.Contains(P.Chunk);
		if (P.bBundled || Mounted.Contains(P.Chunk)) {
			P.State = EDFPackState::Installed;
			P.Downloaded = P.Size;
		} else if (Active == P.Chunk) {
			P.State = EDFPackState::Downloading;
			const uint64 Stats = D ? D->GetLoadingStats().BytesDownloaded : StatsBase;
			P.Downloaded = FMath::Min(P.Size, ActiveBase + (Stats > StatsBase ? Stats - StatsBase : 0));
		} else {
			P.State = Queue.Contains(P.Chunk) ? EDFPackState::Queued : Failed.Contains(P.Chunk) ? EDFPackState::Failed
																								: EDFPackState::NotInstalled;
			P.Downloaded = FMath::Min(P.Size, BytesOnDisk(P.Chunk));
		}
	}
	// progress alone isn't a change (the menu reads it every frame)
	bool bChanged = Old.Num() != Packs.Num();
	for (int32 i = 0; !bChanged && i < Packs.Num(); ++i)
		bChanged = Old[i].Chunk != Packs[i].Chunk || Old[i].State != Packs[i].State || Old[i].bBundled != Packs[i].bBundled;
	if (bChanged)
		OnPacksChanged.Broadcast();
}

bool UDFDownloads::Tick(float DeltaTime) {
	if (Active != INDEX_NONE || !bManifestReady)
		Refresh();
	return true;
}

#undef LOCTEXT_NAMESPACE
