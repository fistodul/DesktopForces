#include "Core/DFServerBeacon.h"
#include "Async/Async.h"
#include "Common/UdpSocketBuilder.h"
#include "Core/DFGameSettings.h"
#include "DesktopForces.h"
#include "Core/DFTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/DFGameMode.h"
#include "Game/DFGameState.h"
#include "GameFramework/GameSession.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/PlayerState.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace {
	constexpr float HeartbeatEvery = 300.f; // seconds; masters drop a server after longer silence
	constexpr int32 MaxPacket = 1200;

	FString Clean(const FString& S) {
		return S.Replace(TEXT("\\"), TEXT("/"));
	}
} // namespace

FString UDFServerBeacon::ValidateResponse(const FString& Secure, const FString& InCipher) {
	// gsmsalg, as 333networks' GameSpy::getValidateResponse: RC4-style shuffle by the cipher, then
	// the challenge run through it, then base64 of the result
	if (Secure.Len() < 6 || InCipher.Len() < 6)
		return TEXT("validate");
	uint8 Enc[256];
	for (int32 J = 0; J < 256; ++J)
		Enc[J] = uint8(J);
	uint8 A = 0, X, Y, B = 0;
	for (int32 J = 0; J < 256; ++J) {
		A += Enc[J] + uint8(InCipher[J % 6]);
		X = Enc[A];
		Enc[A] = Enc[J];
		Enc[J] = X;
	}
	uint8 Tmp[66] = {};
	A = 0;
	for (int32 I = 0; I < 6; ++I) {
		A += uint8(Secure[I]) + 1;
		X = Enc[A];
		B += X;
		Y = Enc[B];
		Enc[B] = X;
		Enc[A] = Y;
		Tmp[I] = uint8(Secure[I]) ^ Enc[uint8(X + Y)];
	}
	auto Shift = [](uint8 V) -> TCHAR {
		V &= 63;
		return V < 26 ? TCHAR('A' + V) : V < 52 ? TCHAR('a' + V - 26)
			: V < 62                            ? TCHAR('0' + V - 52)
			: V == 62                           ? TCHAR('+')
												: TCHAR('/');
	};
	FString Out;
	for (int32 I = 0; I < 6; I += 3) {
		const uint8 P = Tmp[I], Q = Tmp[I + 1], R = Tmp[I + 2];
		Out.AppendChar(Shift(P >> 2));
		Out.AppendChar(Shift(((P & 3) << 4) | (Q >> 4)));
		Out.AppendChar(Shift(((Q & 15) << 2) | (R >> 6)));
		Out.AppendChar(Shift(R & 63));
	}
	return Out;
}

void UDFServerBeacon::Deinitialize() {
	Stop();
	Super::Deinitialize();
}

void UDFServerBeacon::Start(int32 InGamePort, bool bInPublic) {
	const bool bKeep = Socket && GamePort == InGamePort;
	GamePort = InGamePort;
	bPublic = bInPublic && !FParse::Param(FCommandLine::Get(), TEXT("nomaster"));
	NextHeartbeat = FPlatformTime::Seconds() + 1.0; // a map change is news: \statechanged\ on the next tick
	bAnnounced = false;
	if (bKeep) {
		if (bPublic && Masters.IsEmpty())
			ResolveMasters();
		return;
	}
	Stop();
	QueryPort = GamePort + 1;
	Socket = FUdpSocketBuilder(TEXT("DFServerBeacon")).AsNonBlocking().AsReusable().BoundToPort(QueryPort).WithReceiveBufferSize(8192).WithSendBufferSize(8192);
	if (!Socket) {
		UE_LOG(LogDF, Warning, TEXT("Server query port %d is taken: not listed"), QueryPort);
		return;
	}
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDFServerBeacon::Tick), 0.05f);
	if (bPublic)
		ResolveMasters();
	UE_LOG(LogDF, Display, TEXT("Server query port %d%s"), QueryPort, bPublic ? TEXT(", announcing to the masters") : TEXT(""));
}

void UDFServerBeacon::Stop() {
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
	if (Socket) {
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void UDFServerBeacon::ResolveMasters() {
	// names need DNS, which can take a while: not on the game thread
	const TArray<FString> List = MasterList();
	TWeakObjectPtr<UDFServerBeacon> Self(this);
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Self, List]() {
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		TArray<TSharedPtr<FInternetAddr>> Found;
		for (const FString& Entry : List) {
			FString Host, Port;
			if (!Entry.Split(TEXT(":"), &Host, &Port))
				continue;
			const FAddressInfoResult Info = Sockets->GetAddressInfo(*Host, nullptr, EAddressInfoFlags::Default, FNetworkProtocolTypes::IPv4);
			if (Info.Results.Num() == 0)
				continue;
			TSharedRef<FInternetAddr> Addr = Info.Results[0].Address;
			Addr->SetPort(FCString::Atoi(*Port));
			Found.Add(Addr);
		}
		AsyncTask(ENamedThreads::GameThread, [Self, Found]() {
			if (UDFServerBeacon* B = Self.Get())
				B->Masters = Found;
		});
	});
}

TArray<FString> UDFServerBeacon::MasterList() {
	FString Only;
	if (FParse::Value(FCommandLine::Get(), TEXT("-master="), Only))
		return { Only };
	return UDFGameSettings::Get().MasterServers;
}

bool UDFServerBeacon::Tick(float DeltaTime) {
	if (!Socket)
		return false;
	uint32 Size = 0;
	uint8 Buffer[1500];
	TSharedRef<FInternetAddr> From = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	while (Socket->HasPendingData(Size)) {
		int32 Read = 0;
		if (!Socket->RecvFrom(Buffer, sizeof(Buffer) - 1, Read, *From) || Read <= 0)
			break;
		Buffer[Read] = 0;
		HandleQuery(FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Buffer))), *From);
	}
	// the clock, not DeltaTime: a ticker with a delay is handed the last frame's time
	const double Now = FPlatformTime::Seconds();
	if (bPublic && Now >= NextHeartbeat) {
		if (Masters.Num() > 0) {
			SendHeartbeat(!bAnnounced);
			bAnnounced = true;
			NextHeartbeat = Now + HeartbeatEvery;
		} else {
			ResolveMasters(); // no network or DNS yet: a dedicated host can start before them
			NextHeartbeat = Now + 60.0;
		}
	}
	return true;
}

void UDFServerBeacon::SendHeartbeat(bool bStateChanged) {
	// \heartbeat\ names the port masters query; they answer here with \secure\ first time round
	const FString Text = FString::Printf(TEXT("\\heartbeat\\%d\\gamename\\%s%s\\final\\"), QueryPort, GameName, bStateChanged ? TEXT("\\statechanged\\1") : TEXT(""));
	const FTCHARToUTF8 Bytes(*Text);
	for (const TSharedPtr<FInternetAddr>& Master : Masters) {
		int32 Sent = 0;
		Socket->SendTo(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Sent, *Master);
	}
}

void UDFServerBeacon::HandleQuery(const FString& Query, const FInternetAddr& From) {
	if (!Query.StartsWith(TEXT("\\")))
		return;
	TArray<FString> Parts;
	Query.ParseIntoArray(Parts, TEXT("\\"), false);
	TMap<FString, FString> Keys;
	for (int32 I = 1; I + 1 <= Parts.Num() - 1; I += 2)
		Keys.Add(Parts[I].ToLower(), Parts[I + 1]);
	const FString Answer = Status(Keys);
	if (!Answer.IsEmpty())
		Reply(Answer, From);
}

FString UDFServerBeacon::Status(const TMap<FString, FString>& Query) const {
	const bool bAll = Query.Contains(TEXT("status"));
	FString Out;
	auto Add = [&Out](const FString& Key, const FString& Value) {
		Out += FString::Printf(TEXT("\\%s\\%s"), *Key, *Clean(Value));
	};
	if (const FString* Secure = Query.Find(TEXT("secure")))
		Add(TEXT("validate"), ValidateResponse(*Secure, Cipher));
	if (const FString* Echo = Query.Find(TEXT("echo")))
		Add(TEXT("echo"), *Echo);
	const UWorld* World = GetGameInstance()->GetWorld();
	const ADFGameState* GS = World ? World->GetGameState<ADFGameState>() : nullptr;
	const ADFGameMode* GM = World ? World->GetAuthGameMode<ADFGameMode>() : nullptr;
	if (bAll || Query.Contains(TEXT("basic"))) {
		Add(TEXT("gamename"), GameName);
		Add(TEXT("gamever"), TEXT("1"));
		Add(TEXT("location"), TEXT("0"));
	}
	if ((bAll || Query.Contains(TEXT("info"))) && GS && GM) {
		Add(TEXT("hostname"), UDFGameSettings::Get().ServerName);
		Add(TEXT("hostport"), FString::FromInt(GamePort));
		Add(TEXT("mapname"), World->GetMapName());
		Add(TEXT("gametype"), GM->GetClass()->GetName());
		Add(TEXT("numplayers"), FString::FromInt(GS->PlayerArray.Num()));
		Add(TEXT("maxplayers"), FString::FromInt(GM->GameSession ? GM->GameSession->MaxPlayers : 0));
		Add(TEXT("gamemode"), TEXT("openplaying"));
	}
	if ((bAll || Query.Contains(TEXT("rules"))) && GM)
		Add(TEXT("timelimit"), FString::FromInt(GM->TimeLimit));
	if ((bAll || Query.Contains(TEXT("players"))) && GS) {
		int32 I = 0;
		for (const APlayerState* PS : GS->PlayerArray) {
			Add(FString::Printf(TEXT("player_%d"), I), PS->GetPlayerName());
			Add(FString::Printf(TEXT("frags_%d"), I), FString::FromInt(FMath::RoundToInt(PS->GetScore())));
			Add(FString::Printf(TEXT("ping_%d"), I), FString::FromInt(FMath::RoundToInt(PS->GetPingInMilliseconds())));
			++I;
		}
	}
	return Out;
}

void UDFServerBeacon::Reply(const FString& Text, const FInternetAddr& To) {
	// UE1 style: \queryid\<id>.<packet> on every packet, \final\ on the last
	++QueryId;
	int32 Packet = 1, Start = 0;
	while (true) {
		const int32 Left = Text.Len() - Start;
		int32 Cut = FMath::Min(Left, MaxPacket);
		if (Left > MaxPacket) { // between pairs: \queryid\ inside a \key\value would break both
			int32 Slashes = 0, LastKey = 0;
			for (int32 I = 0; I < Cut; ++I)
				if (Text[Start + I] == TEXT('\\') && Slashes++ % 2 == 0)
					LastKey = I;
			if (LastKey > 0)
				Cut = LastKey;
		}
		const FString Part = Text.Mid(Start, Cut);
		Start += Part.Len();
		const bool bLast = Start >= Text.Len();
		const FString Out = FString::Printf(TEXT("%s\\queryid\\%d.%d%s"), *Part, QueryId, Packet, bLast ? TEXT("\\final\\") : TEXT(""));
		const FTCHARToUTF8 Bytes(*Out);
		int32 Sent = 0;
		Socket->SendTo(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Sent, To);
		if (bLast)
			return;
		++Packet;
	}
}
