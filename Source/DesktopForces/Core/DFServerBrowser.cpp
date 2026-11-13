#include "Core/DFServerBrowser.h"
#include "Async/Async.h"
#include "Common/TcpSocketBuilder.h"
#include "Common/UdpSocketBuilder.h"
#include "Core/DFGameSettings.h"
#include "Core/DFServerBeacon.h"
#include "DesktopForces.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace {
	const TCHAR* GameName = UDFServerBeacon::GameName;
	const TCHAR* Cipher = UDFServerBeacon::Cipher;
	constexpr int32 ListPort = 28900; // the masters' TCP port
	// LAN: query ports of game ports 7777 up (a second server on one PC takes the next free pair)
	constexpr int32 LanFirstPort = 7778, LanPorts = 20;

	/** Everything a socket sends until \final\ or the time is up. */
	FString ReadUntilFinal(FSocket& S, double Seconds) {
		TArray<ANSICHAR> All;
		const double End = FPlatformTime::Seconds() + Seconds;
		while (FPlatformTime::Seconds() < End) {
			uint32 Pending = 0;
			if (!S.HasPendingData(Pending)) {
				if (S.GetConnectionState() != SCS_Connected)
					break;
				FPlatformProcess::Sleep(0.02f);
				continue;
			}
			TArray<uint8> Bytes;
			Bytes.SetNumUninitialized(FMath::Min<uint32>(Pending, 65536));
			int32 Read = 0;
			if (!S.Recv(Bytes.GetData(), Bytes.Num(), Read) || Read <= 0)
				break;
			All.Append(reinterpret_cast<ANSICHAR*>(Bytes.GetData()), Read);
			All.Add(0);
			const bool bDone = FString(UTF8_TO_TCHAR(All.GetData())).Contains(TEXT("\\final\\"));
			All.Pop();
			if (bDone)
				break;
		}
		All.Add(0);
		return FString(UTF8_TO_TCHAR(All.GetData()));
	}

	void Send(FSocket& S, const FString& Text) {
		const FTCHARToUTF8 Bytes(*Text);
		int32 Sent = 0;
		S.Send(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Sent);
	}

	/** \key\value\key\value... into a map; later keys win. */
	TArray<TPair<FString, FString>> Pairs(const FString& Text) {
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT("\\"), false);
		TArray<TPair<FString, FString>> Out;
		for (int32 I = 1; I + 1 < Parts.Num(); I += 2)
			Out.Emplace(Parts[I].ToLower(), Parts[I + 1]);
		return Out;
	}

	/** One master's list of ip:query port; empty when it doesn't answer. */
	TArray<FString> AskMaster(const FString& Entry) {
		TArray<FString> Found;
		FString Host, Port;
		if (!Entry.Split(TEXT(":"), &Host, &Port))
			Host = Entry;
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		const FAddressInfoResult Info = Sockets->GetAddressInfo(*Host, nullptr, EAddressInfoFlags::Default, FNetworkProtocolTypes::IPv4);
		if (Info.Results.Num() == 0)
			return Found;
		TSharedRef<FInternetAddr> Addr = Info.Results[0].Address;
		Addr->SetPort(ListPort);
		FSocket* S = FTcpSocketBuilder(TEXT("DFMasterList")).AsBlocking();
		if (!S)
			return Found;
		// no blocking connect: a dead master would hold the list for the OS's 2 minutes
		S->SetNonBlocking(true);
		S->Connect(*Addr);
		if (S->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromSeconds(3.0)) && S->GetConnectionState() == SCS_Connected) {
			FString Secure;
			for (const TPair<FString, FString>& P : Pairs(ReadUntilFinal(*S, 5.0)))
				if (P.Key == TEXT("secure"))
					Secure = P.Value;
			Send(*S, FString::Printf(TEXT("\\gamename\\%s\\location\\0\\validate\\%s\\final\\"), GameName, *UDFServerBeacon::ValidateResponse(Secure, Cipher)));
			Send(*S, FString::Printf(TEXT("\\list\\\\gamename\\%s\\final\\"), GameName));
			for (const TPair<FString, FString>& P : Pairs(ReadUntilFinal(*S, 8.0)))
				if (P.Key == TEXT("ip"))
					Found.AddUnique(P.Value);
		}
		S->Close();
		Sockets->DestroySocket(S);
		return Found;
	}
} // namespace

void UDFServerBrowser::Deinitialize() {
	Close();
	Super::Deinitialize();
}

static FAutoConsoleCommandWithWorld DFServersCmd(TEXT("DFServers"), TEXT("Refresh the server list and log it"), FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) {
	if (UGameInstance* GI = World ? World->GetGameInstance() : nullptr)
		GI->GetSubsystem<UDFServerBrowser>()->Refresh();
}));

static FAutoConsoleCommandWithWorld DFServersLanCmd(TEXT("DFServersLan"), TEXT("Search the local network for servers and log them"), FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) {
	if (UGameInstance* GI = World ? World->GetGameInstance() : nullptr)
		GI->GetSubsystem<UDFServerBrowser>()->Refresh(true);
}));

void UDFServerBrowser::Close() {
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
	if (Socket) {
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	bBusy = false;
}

void UDFServerBrowser::Refresh(bool bLan) {
	if (bBusy)
		return;
	bBusy = true;
	Servers.Reset();
	Partial.Reset();
	Asked.Reset();
	BroadcastAt = 0.0;
	++Version;
	if (bLan) {
		QueryLan();
		return;
	}
	const TArray<FString> Masters = UDFServerBeacon::MasterList();
	TWeakObjectPtr<UDFServerBrowser> Self(this);
	// Own threads, not task workers: a master can keep one waiting for seconds, and the frame's
	// work shares those workers (few on a low-end PC). All at once: one down doesn't delay the backup.
	Async(EAsyncExecution::Thread, [Self, Masters]() {
		TArray<TFuture<TArray<FString>>> Asks;
		for (const FString& Master : Masters)
			Asks.Add(Async(EAsyncExecution::Thread, [Master]() { return AskMaster(Master); }));
		TArray<FString> All;
		for (TFuture<TArray<FString>>& Ask : Asks)
			for (const FString& Address : Ask.Get())
				All.AddUnique(Address);
		AsyncTask(ENamedThreads::GameThread, [Self, All]() {
			if (UDFServerBrowser* B = Self.Get())
				B->QueryServers(All);
		});
	});
}

void UDFServerBrowser::QueryServers(const TArray<FString>& Addresses) {
	Socket = FUdpSocketBuilder(TEXT("DFBrowser")).AsNonBlocking().WithReceiveBufferSize(65536);
	if (!Socket || Addresses.Num() == 0) {
		Close();
		++Version;
		UE_LOG(LogDF, Display, TEXT("Server browser: no servers listed"));
		return;
	}
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	const FTCHARToUTF8 Query(TEXT("\\status\\"));
	for (const FString& Address : Addresses) {
		FString Ip, Port;
		if (!Address.Split(TEXT(":"), &Ip, &Port))
			continue;
		bool bValid = false;
		TSharedRef<FInternetAddr> To = Sockets->CreateInternetAddr();
		To->SetIp(*Ip, bValid);
		To->SetPort(FCString::Atoi(*Port));
		int32 Sent = 0;
		if (bValid && Socket->SendTo(reinterpret_cast<const uint8*>(Query.Get()), Query.Length(), Sent, *To))
			Asked.Add(Address, FPlatformTime::Seconds());
	}
	GiveUpAt = FPlatformTime::Seconds() + 3.0;
	// every frame for these 3 s: a reply waiting for the next poll would add to its ping
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDFServerBrowser::Tick));
}

void UDFServerBrowser::QueryLan() {
	Socket = FUdpSocketBuilder(TEXT("DFBrowserLan")).AsNonBlocking().WithBroadcast().WithReceiveBufferSize(65536);
	if (!Socket) {
		Close();
		++Version;
		return;
	}
	TSharedRef<FInternetAddr> To = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	To->SetBroadcastAddress();
	const FTCHARToUTF8 Query(TEXT("\\status\\"));
	for (int32 Port = LanFirstPort; Port < LanFirstPort + LanPorts; ++Port) {
		To->SetPort(Port);
		int32 Sent = 0;
		Socket->SendTo(reinterpret_cast<const uint8*>(Query.Get()), Query.Length(), Sent, *To);
	}
	BroadcastAt = FPlatformTime::Seconds();
	GiveUpAt = BroadcastAt + 1.5; // a LAN answers in milliseconds
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDFServerBrowser::Tick));
}

bool UDFServerBrowser::Tick(float DeltaTime) {
	uint32 Size = 0;
	uint8 Buffer[1500];
	TSharedRef<FInternetAddr> From = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	while (Socket && Socket->HasPendingData(Size)) {
		int32 Read = 0;
		if (!Socket->RecvFrom(Buffer, sizeof(Buffer) - 1, Read, *From) || Read <= 0)
			break;
		Buffer[Read] = 0;
		Parse(FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Buffer))), From->ToString(true));
	}
	if (FPlatformTime::Seconds() >= GiveUpAt) {
		Close();
		if (BroadcastAt > 0.0) { // braces: UE_LOG is a block of its own
			UE_LOG(LogDF, Display, TEXT("Server browser: %d servers answered on the LAN"), Servers.Num());
		} else {
			UE_LOG(LogDF, Display, TEXT("Server browser: %d of %d listed servers answered"), Servers.Num(), Asked.Num());
		}
		for (const FDFServerEntry& E : Servers)
			UE_LOG(LogDF, Display, TEXT("  %s  %s  %s  %d/%d  %s  %d ms"), *E.Name, *E.Map, *E.Mode, E.Players, E.MaxPlayers, *E.Join, E.PingMs);
		++Version;
		return false;
	}
	return true;
}

void UDFServerBrowser::Parse(const FString& Reply, const FString& From) {
	FString& Text = Partial.FindOrAdd(From);
	Text += Reply;
	if (!Reply.Contains(TEXT("\\final\\")))
		return;
	FDFServerEntry E;
	E.Address = From;
	FString Ip, Port;
	From.Split(TEXT(":"), &Ip, &Port);
	int32 HostPort = 0;
	for (const TPair<FString, FString>& P : Pairs(Text))
		if (P.Key == TEXT("hostname"))
			E.Name = P.Value;
		else if (P.Key == TEXT("mapname"))
			E.Map = P.Value;
		else if (P.Key == TEXT("gametype"))
			E.Mode = P.Value;
		else if (P.Key == TEXT("numplayers"))
			E.Players = FCString::Atoi(*P.Value);
		else if (P.Key == TEXT("maxplayers"))
			E.MaxPlayers = FCString::Atoi(*P.Value);
		else if (P.Key == TEXT("hostport"))
			HostPort = FCString::Atoi(*P.Value);
	E.Join = FString::Printf(TEXT("%s:%d"), *Ip, HostPort);
	const double* Sent = Asked.Find(From);
	if (Sent || BroadcastAt > 0.0)
		E.PingMs = FMath::RoundToInt((FPlatformTime::Seconds() - (Sent ? *Sent : BroadcastAt)) * 1000.0);
	E.bLan = BroadcastAt > 0.0;
	if (HostPort > 0) {
		// one row per server: a PC on two networks hears the broadcast twice
		Servers.RemoveAll([&From](const FDFServerEntry& Old) { return Old.Address == From; });
		Servers.Add(E);
		Servers.Sort([](const FDFServerEntry& A, const FDFServerEntry& B) { return A.PingMs < B.PingMs; });
		++Version;
	}
	Partial.Remove(From);
}
