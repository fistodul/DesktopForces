#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DFDownloads.generated.h"

enum class EDFPackState : uint8 {
	NotInstalled, // on the server (maybe partly downloaded already)
	Queued,
	Downloading,
	Installed, // downloaded and mounted: its maps are in the menu
	Failed,
};

struct FDFPackStatus {
	int32 Chunk = INDEX_NONE;
	FText Name;
	FText Description;
	EDFPackState State = EDFPackState::NotInstalled;
	uint64 Size = 0; // all its files, bytes
	uint64 Downloaded = 0;
};

/**
 * Content packs downloaded in game: a build made with `Tools/package.sh --downloads` carries the
 * core pack only and a content build id (Content/Downloads/ContentBuildId.txt); the other packs'
 * paks (pakchunk1, 2 ...) and BuildManifest-<Platform>.txt are on the server under
 * <server>/<Platform>/<build id>/ (the player's UDFUserSettings::DownloadServer, else UDFGameSettings').
 *
 * UE's ChunkDownloader fetches the manifest, downloads into the app's own storage (resuming file
 * by file), checks SHA1s and remembers what's there across runs. It would mount every file of a
 * chunk as a pak, though, and iostore chunks are .pak + .utoc + .ucas: we mount the .pak ourselves
 * (the pak file system mounts its container with it). Installed packs mount at start; downloads
 * mount when done, no restart.
 */
UCLASS()
class DESKTOPFORCES_API UDFDownloads : public UGameInstanceSubsystem {
	GENERATED_BODY()

public:
	static UDFDownloads* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** This build offers downloads (it has a content build id and a server). */
	bool IsAvailable() const { return !BuildId.IsEmpty(); }
	/** The server's pack list is here (else still asking: GetError). */
	bool HasManifest() const { return bManifestReady; }
	const TArray<FDFPackStatus>& GetPacks() const { return Packs; }
	const FDFPackStatus* FindPack(int32 Chunk) const {
		return Packs.FindByPredicate([Chunk](const FDFPackStatus& P) { return P.Chunk == Chunk; });
	}
	/** The downloader's last problem (no connection, HTTP 404, disk full ...). */
	FText GetError() const;
	const FString& GetBuildId() const { return BuildId; }
	/** The server in use (Options > Game sets the player's: UseServer). */
	const FString& GetServer() const { return Server; }
	/** The player's server choice changed (empty: the game's): ask that one. */
	void UseServer(const FString& PlayerServer);

	/** Queue a pack; one downloads at a time. */
	void Download(int32 Chunk);
	/** Stop a download (finished files are kept: Download goes on from there) or unqueue it. */
	void Cancel(int32 Chunk);
	/** Unmount a pack and delete its files. Only from the menu: nothing of it may be in use. */
	void Delete(int32 Chunk);

	/** Pack list or a pack's state changed. */
	FSimpleMulticastDelegate OnPacksChanged;
	/** Content was mounted or unmounted: map lists change. */
	FSimpleMulticastDelegate OnContentChanged;

private:
	struct FFile {
		FString Name;
		uint64 Size = 0;
		FString Version;
		int32 Chunk = INDEX_NONE;
	};

	void SetServer(const FString& PlayerServer);
	void StartDownloader();
	void Restart();
	void OnManifest();
	void StartNext();
	void OnDownloaded(int32 Chunk, bool bSuccess, int32 ForGeneration);
	bool Mount(int32 Chunk);
	uint64 BytesOnDisk(int32 Chunk) const;
	void SaveLocalManifest() const;
	void Refresh();
	bool Tick(float DeltaTime);

	FString BuildId;
	FString Server;
	bool bManifestReady = false;
	/** The manifest's files (ChunkDownloader keeps its own list private). */
	TArray<FFile> Files;
	TArray<FDFPackStatus> Packs;
	TSet<int32> Mounted;
	TSet<int32> Failed;
	TArray<int32> Queue;
	int32 Active = INDEX_NONE;
	/** Progress of Active: bytes it had on disk when it started, the downloader's byte count then. */
	uint64 ActiveBase = 0, StatsBase = 0;
	/** Bumped when the downloader is remade: callbacks from the old one are ignored. */
	int32 Generation = 0;
	FTSTicker::FDelegateHandle TickHandle;
};
