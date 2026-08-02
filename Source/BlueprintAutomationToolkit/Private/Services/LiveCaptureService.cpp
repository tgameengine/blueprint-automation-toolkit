// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Services/LiveCaptureService.h"

#include "BlueprintAutomationToolkitModule.h"
#include "BlueprintAutomationToolkitSettings.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PlatformFeatures.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#include "VideoRecordingSystem.h"

namespace
{
	static TSharedPtr<FJsonValue> ObjectValue(const TSharedRef<FJsonObject>& Object)
	{
		return MakeShared<FJsonValueObject>(Object);
	}

	static FString NormalizeDiskPath(FString Path)
	{
		FPaths::NormalizeFilename(Path);
		FPaths::CollapseRelativeDirectories(Path);
		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Path);
		return Path;
	}

	static bool IsInsideDirectory(const FString& Path, const FString& Root)
	{
		FString NormalizedPath = NormalizeDiskPath(Path).ToLower();
		FString NormalizedRoot = NormalizeDiskPath(Root).ToLower();
		NormalizedRoot.RemoveFromEnd(TEXT("/"));
		return NormalizedPath == NormalizedRoot || NormalizedPath.StartsWith(NormalizedRoot + TEXT("/"));
	}

	static bool ResolveOutputDirectory(const TSharedPtr<FJsonObject>& Request, const FString& SessionId, FString& OutDirectory, FString& OutError, bool bCreate)
	{
		FString RelativeFolder = TEXT("BlueprintAutomationToolkit/LiveCaptures");
		if (Request.IsValid())
		{
			Request->TryGetStringField(TEXT("output_folder"), RelativeFolder);
		}
		RelativeFolder.TrimStartAndEndInline();
		FPaths::NormalizeFilename(RelativeFolder);
		if (RelativeFolder.IsEmpty() || !FPaths::IsRelative(RelativeFolder) || RelativeFolder.Contains(TEXT("..")))
		{
			OutError = TEXT("output_folder must be a relative path beneath the project Saved directory");
			return false;
		}

		const FString SavedRoot = NormalizeDiskPath(FPaths::ProjectSavedDir());
		OutDirectory = NormalizeDiskPath(FPaths::Combine(SavedRoot, RelativeFolder, SessionId));
		if (!IsInsideDirectory(OutDirectory, SavedRoot))
		{
			OutError = TEXT("Capture output resolves outside the project Saved directory");
			return false;
		}
		if (bCreate && !IFileManager::Get().MakeDirectory(*OutDirectory, true))
		{
			OutError = TEXT("Could not create the capture output directory");
			return false;
		}
		if (bCreate && !IsInsideDirectory(IFileManager::Get().GetFilenameOnDisk(*OutDirectory), SavedRoot))
		{
			OutError = TEXT("Capture output resolves outside Saved through a filesystem link");
			return false;
		}
		return true;
	}

	static FViewport* ResolveViewport(FBlueprintAutomationToolkitModule* Module, const FString& Source, int32 PieIndex)
	{
		const bool bPreferPie = Source.Equals(TEXT("pie"), ESearchCase::IgnoreCase)
			|| Source.Equals(TEXT("auto"), ESearchCase::IgnoreCase);
		if (bPreferPie && Module)
		{
			if (UWorld* PieWorld = Module->GetPIEWorld(PieIndex))
			{
				if (UGameViewportClient* GameViewport = PieWorld->GetGameViewport())
				{
					if (GameViewport->Viewport)
					{
						return GameViewport->Viewport;
					}
				}
			}
		}

		if (!Source.Equals(TEXT("pie"), ESearchCase::IgnoreCase))
		{
			if (FViewport* Active = GEditor ? GEditor->GetActiveViewport() : nullptr)
			{
				return Active;
			}
			if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
			{
				FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
				const TSharedPtr<IAssetViewport> AssetViewport = LevelEditor.GetFirstActiveViewport();
				return AssetViewport.IsValid() ? AssetViewport->GetActiveViewport() : nullptr;
			}
		}
		return nullptr;
	}

	static bool IsSafeFilePrefix(const FString& Prefix)
	{
		if (Prefix.IsEmpty() || Prefix.Len() > 64)
		{
			return false;
		}
		for (const TCHAR Character : Prefix)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-'))
			{
				return false;
			}
		}
		return true;
	}
}

FString FLiveCaptureService::StateToString(const EState State)
{
	switch (State)
	{
	case EState::Idle: return TEXT("idle");
	case EState::WarmingUp: return TEXT("warming_up");
	case EState::Recording: return TEXT("recording");
	case EState::Finalizing: return TEXT("finalizing");
	case EState::Completed: return TEXT("completed");
	case EState::Failed: return TEXT("failed");
	case EState::Canceled: return TEXT("canceled");
	default: return TEXT("unknown");
	}
}

FLiveCaptureService::~FLiveCaptureService()
{
	Shutdown();
}

bool FLiveCaptureService::IsActiveState(const EState State)
{
	return State == EState::WarmingUp || State == EState::Recording || State == EState::Finalizing;
}

FAutomationResult FLiveCaptureService::DescribeSchema() const
{
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("api"), TEXT("native_live_gameplay_capture"));
	Data->SetBoolField(TEXT("requires_python"), false);
	Data->SetBoolField(TEXT("launches_external_encoder"), false);
	Data->SetArrayField(TEXT("sources"), {
		MakeShared<FJsonValueString>(TEXT("auto")),
		MakeShared<FJsonValueString>(TEXT("pie")),
		MakeShared<FJsonValueString>(TEXT("editor")),
	});
	Data->SetArrayField(TEXT("output_formats"), {
		MakeShared<FJsonValueString>(TEXT("png_sequence")),
		MakeShared<FJsonValueString>(TEXT("mp4")),
		MakeShared<FJsonValueString>(TEXT("both")),
	});
	const UBlueprintAutomationToolkitSettings* Settings = GetDefault<UBlueprintAutomationToolkitSettings>();
	Data->SetNumberField(TEXT("max_capture_frames"), Settings ? Settings->AssetPipelineMaxCaptureFrames : 3600);
	Data->SetNumberField(TEXT("max_fps"), 120);
	Data->SetBoolField(TEXT("real_time_world_ticks"), true);
	Data->SetBoolField(TEXT("png_supports_multi_pie_selection"), true);
	Data->SetBoolField(TEXT("mp4_supports_multi_pie_selection"), false);
	Data->SetStringField(TEXT("mp4_backend"), TEXT("Unreal platform video recording system"));

	IVideoRecordingSystem* Recorder = IPlatformFeaturesModule::Get().GetVideoRecordingSystem();
	const bool bMp4Available = Recorder && Recorder->IsEnabled();
	Data->SetBoolField(TEXT("mp4_available"), bMp4Available);
	Data->SetNumberField(TEXT("mp4_min_seconds"), Recorder ? Recorder->GetMinimumRecordingSeconds() : 0);
	Data->SetNumberField(TEXT("mp4_max_seconds"), Recorder ? Recorder->GetMaximumRecordingSeconds() : 0);
	Data->SetStringField(TEXT("audio_capture"), TEXT("platform_if_available"));
	return FAutomationResult::Ok(ObjectValue(Data));
}

FAutomationResult FLiveCaptureService::Start(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request)
{
	if (!Request.IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_json"), TEXT("Request body is required"), 400);
	}
	if (IsActiveState(Session.State))
	{
		return FAutomationResult::Error(TEXT("capture_already_active"), TEXT("A live capture session is already active"), 409);
	}

	FSession Candidate;
	Candidate.Id = FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	Request->TryGetStringField(TEXT("source"), Candidate.Source);
	Candidate.Source.ToLowerInline();
	if (Candidate.Source != TEXT("auto") && Candidate.Source != TEXT("pie") && Candidate.Source != TEXT("editor"))
	{
		return FAutomationResult::Error(TEXT("invalid_source"), TEXT("source must be auto, pie, or editor"), 400);
	}
	Request->TryGetStringField(TEXT("output_format"), Candidate.OutputFormat);
	Candidate.OutputFormat.ToLowerInline();
	if (Candidate.OutputFormat != TEXT("png_sequence") && Candidate.OutputFormat != TEXT("mp4") && Candidate.OutputFormat != TEXT("both"))
	{
		return FAutomationResult::Error(TEXT("invalid_output_format"), TEXT("output_format must be png_sequence, mp4, or both"), 400);
	}
	Candidate.bCapturePng = Candidate.OutputFormat != TEXT("mp4");
	Candidate.bCaptureVideo = Candidate.OutputFormat != TEXT("png_sequence");

	double Number = 0.0;
	if (Request->TryGetNumberField(TEXT("pie_index"), Number)) Candidate.PieIndex = FMath::Max(0, FMath::RoundToInt(Number));
	if (Request->TryGetNumberField(TEXT("fps"), Number)) Candidate.Fps = FMath::RoundToInt(FMath::Clamp(Number, 1.0, 120.0));
	if (Request->TryGetNumberField(TEXT("warmup_seconds"), Number)) Candidate.WarmupSeconds = FMath::Clamp(Number, 0.0, 60.0);
	if (Request->TryGetNumberField(TEXT("width"), Number)) Candidate.Width = FMath::Clamp(FMath::RoundToInt(Number), 0, 7680);
	if (Request->TryGetNumberField(TEXT("height"), Number)) Candidate.Height = FMath::Clamp(FMath::RoundToInt(Number), 0, 4320);
	Request->TryGetBoolField(TEXT("stop_pie_when_complete"), Candidate.bStopPieWhenComplete);
	Request->TryGetStringField(TEXT("file_prefix"), Candidate.FilePrefix);
	if (!IsSafeFilePrefix(Candidate.FilePrefix))
	{
		return FAutomationResult::Error(TEXT("invalid_file_prefix"), TEXT("file_prefix may contain only letters, numbers, underscore, and hyphen"), 400);
	}
	if ((Candidate.Width == 0) != (Candidate.Height == 0) || (Candidate.Width > 0 && (Candidate.Width < 64 || Candidate.Height < 64)))
	{
		return FAutomationResult::Error(TEXT("invalid_resolution"), TEXT("width and height must both be zero or both be at least 64"), 400);
	}
	if (Candidate.bCaptureVideo && Candidate.PieIndex != 0)
	{
		return FAutomationResult::Error(TEXT("mp4_multi_pie_unsupported"), TEXT("Unreal's platform MP4 recorder targets the primary game back buffer; use pie_index 0 or png_sequence for another PIE instance"), 400);
	}

	const UBlueprintAutomationToolkitSettings* Settings = GetDefault<UBlueprintAutomationToolkitSettings>();
	const int32 MaxFrames = FMath::Max(1, Settings ? Settings->AssetPipelineMaxCaptureFrames : 3600);
	double DurationSeconds = 0.0;
	if (Request->TryGetNumberField(TEXT("duration_seconds"), DurationSeconds))
	{
		if (DurationSeconds <= 0.0)
		{
			return FAutomationResult::Error(TEXT("invalid_duration"), TEXT("duration_seconds must be greater than zero"), 400);
		}
		const double RequestedFrameCount = DurationSeconds * Candidate.Fps;
		if (!FMath::IsFinite(RequestedFrameCount) || RequestedFrameCount > MaxFrames)
		{
			return FAutomationResult::Error(TEXT("capture_frame_limit_exceeded"), FString::Printf(TEXT("Requested capture exceeds the configured maximum of %d frames"), MaxFrames), 413);
		}
		const int32 RequestedFrames = FMath::CeilToInt(RequestedFrameCount);
		Candidate.TargetFrames = FMath::Max(1, RequestedFrames);
	}
	else if (Request->TryGetNumberField(TEXT("frame_count"), Number))
	{
		if (!FMath::IsFinite(Number) || Number > MaxFrames)
		{
			return FAutomationResult::Error(TEXT("capture_frame_limit_exceeded"), FString::Printf(TEXT("Requested capture exceeds the configured maximum of %d frames"), MaxFrames), 413);
		}
		const int32 RequestedFrames = FMath::RoundToInt(Number);
		if (RequestedFrames <= 0) return FAutomationResult::Error(TEXT("invalid_frame_count"), TEXT("frame_count must be greater than zero"), 400);
		Candidate.TargetFrames = RequestedFrames;
	}
	else
	{
		Candidate.TargetFrames = FMath::Min(300, MaxFrames);
	}
	DurationSeconds = static_cast<double>(Candidate.TargetFrames) / Candidate.Fps;

	bool bDryRun = false;
	Request->TryGetBoolField(TEXT("dry_run"), bDryRun);
	if (!bDryRun && Candidate.Source == TEXT("auto"))
	{
		Candidate.Source = Module.GetPIEWorld(Candidate.PieIndex) ? TEXT("pie") : TEXT("editor");
	}
	if (!bDryRun && Candidate.Source == TEXT("pie") && !Module.GetPIEWorld(Candidate.PieIndex))
	{
		return FAutomationResult::Error(TEXT("pie_world_unavailable"), TEXT("The requested PIE world is not running"), 409);
	}
	if (!bDryRun && Candidate.bCapturePng && !ResolveViewport(&Module, Candidate.Source, Candidate.PieIndex))
	{
		return FAutomationResult::Error(TEXT("viewport_unavailable"), TEXT("No matching editor or PIE viewport is available"), 503);
	}

	FString PathError;
	if (!ResolveOutputDirectory(Request, Candidate.Id, Candidate.OutputDirectory, PathError, !bDryRun))
	{
		return FAutomationResult::Error(TEXT("capture_path_denied"), PathError, 403);
	}
	if (bDryRun)
	{
		Session = Candidate;
		Session.State = EState::Idle;
		TSharedRef<FJsonObject> Data = BuildStatusObject();
		Data->SetBoolField(TEXT("dry_run"), true);
		Data->SetBoolField(TEXT("would_start"), true);
		return FAutomationResult::Ok(ObjectValue(Data));
	}

	if (Candidate.bCaptureVideo)
	{
		VideoRecorder = IPlatformFeaturesModule::Get().GetVideoRecordingSystem();
		if (!VideoRecorder || !VideoRecorder->IsEnabled())
		{
			return FAutomationResult::Error(TEXT("mp4_unavailable"), TEXT("Unreal's platform MP4 recorder is unavailable in this editor session; use png_sequence"), 503);
		}
		const uint64 MinSeconds = VideoRecorder->GetMinimumRecordingSeconds();
		const uint64 MaxSeconds = VideoRecorder->GetMaximumRecordingSeconds();
		if (DurationSeconds < static_cast<double>(MinSeconds) || DurationSeconds > static_cast<double>(MaxSeconds))
		{
			return FAutomationResult::Error(TEXT("mp4_duration_out_of_range"),
				FString::Printf(TEXT("MP4 duration must be between %llu and %llu seconds on this platform"), MinSeconds, MaxSeconds), 400);
		}
		if (VideoRecorder->GetRecordingState() != EVideoRecordingState::None)
		{
			return FAutomationResult::Error(TEXT("platform_recorder_busy"), TEXT("Unreal's platform video recorder is already in use"), 409);
		}

		VideoFinalizedHandle = VideoRecorder->GetOnVideoRecordingFinalizedDelegate().AddRaw(this, &FLiveCaptureService::HandleVideoFinalized);
		FVideoRecordingParameters Parameters;
		Parameters.RecordingLengthSeconds = FMath::CeilToInt(DurationSeconds);
		Parameters.bAutoStart = false;
		Parameters.bAutoContinue = false;
		Parameters.bExportToLibrary = false;
		const FString RecordingName = TEXT("BAT_") + Candidate.Id.Replace(TEXT(":"), TEXT("-"));
		if (!VideoRecorder->NewRecording(*RecordingName, Parameters))
		{
			VideoRecorder->GetOnVideoRecordingFinalizedDelegate().Remove(VideoFinalizedHandle);
			VideoFinalizedHandle.Reset();
			VideoRecorder = nullptr;
			return FAutomationResult::Error(TEXT("mp4_start_failed"), TEXT("Unreal could not initialize the platform MP4 recorder"), 503);
		}
		Candidate.bVideoInitialized = true;
	}

	Owner = &Module;
	Session = MoveTemp(Candidate);
	Session.StartedSeconds = FPlatformTime::Seconds();
	Session.State = Session.WarmupSeconds > 0.0 ? EState::WarmingUp : EState::Recording;
	if (Session.State == EState::Recording)
	{
		FString BeginError;
		if (!BeginRecording(BeginError))
		{
			Fail(BeginError);
			return FAutomationResult::Error(TEXT("capture_start_failed"), BeginError, 503);
		}
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FLiveCaptureService::Tick), 0.0f);
	return FAutomationResult::Ok(ObjectValue(BuildStatusObject()), 202);
}

bool FLiveCaptureService::BeginRecording(FString& OutError)
{
	Session.RecordingStartedSeconds = FPlatformTime::Seconds();
	Session.NextFrameSeconds = Session.RecordingStartedSeconds;
	Session.State = EState::Recording;
	if (Session.bCaptureVideo)
	{
		if (!VideoRecorder || VideoRecorder->GetRecordingState() != EVideoRecordingState::Paused)
		{
			OutError = TEXT("Platform video recorder is not ready");
			return false;
		}
		VideoRecorder->StartRecording();
		if (VideoRecorder->GetRecordingState() != EVideoRecordingState::Recording)
		{
			OutError = TEXT("Platform video recorder did not enter the recording state");
			return false;
		}
	}
	return true;
}

bool FLiveCaptureService::Tick(float DeltaSeconds)
{
	const double Now = FPlatformTime::Seconds();
	if (Session.State == EState::WarmingUp)
	{
		if (Now - Session.StartedSeconds < Session.WarmupSeconds)
		{
			return true;
		}
		FString BeginError;
		if (!BeginRecording(BeginError))
		{
			Fail(BeginError);
			TickerHandle.Reset();
			return false;
		}
	}
	if (Session.State != EState::Recording)
	{
		TickerHandle.Reset();
		return false;
	}
	if (Session.Source == TEXT("pie") && (!Owner || !Owner->GetPIEWorld(Session.PieIndex)))
	{
		Fail(TEXT("The requested PIE world ended during capture"));
		TickerHandle.Reset();
		return false;
	}

	if (Now >= Session.NextFrameSeconds)
	{
		const double FrameInterval = 1.0 / Session.Fps;
		if (Now > Session.NextFrameSeconds + FrameInterval)
		{
			Session.DroppedFrames += FMath::Max(0, FMath::FloorToInt((Now - Session.NextFrameSeconds) / FrameInterval));
		}
		FString CaptureError;
		if (Session.bCapturePng && !CaptureFrame(CaptureError))
		{
			Fail(CaptureError);
			TickerHandle.Reset();
			return false;
		}
		++Session.CapturedFrames;
		Session.NextFrameSeconds = Now + FrameInterval;
	}

	if (Session.CapturedFrames >= Session.TargetFrames)
	{
		BeginFinalize(true);
		TickerHandle.Reset();
		return false;
	}
	return true;
}

bool FLiveCaptureService::CaptureFrame(FString& OutError)
{
	FViewport* Viewport = ResolveViewport(Owner, Session.Source, Session.PieIndex);
	if (!Viewport)
	{
		OutError = TEXT("Capture viewport became unavailable");
		return false;
	}
	Viewport->Draw(false);
	const FIntPoint SourceSize = Viewport->GetSizeXY();
	TArray<FColor> Pixels;
	if (SourceSize.X <= 0 || SourceSize.Y <= 0 || !Viewport->ReadPixels(Pixels) || Pixels.Num() != SourceSize.X * SourceSize.Y)
	{
		OutError = TEXT("Failed to read pixels from the live viewport");
		return false;
	}

	int32 OutputWidth = SourceSize.X;
	int32 OutputHeight = SourceSize.Y;
	if (Session.Width > 0 && Session.Height > 0 && (Session.Width != SourceSize.X || Session.Height != SourceSize.Y))
	{
		TArray<FColor> Resized;
		FImageUtils::ImageResize(SourceSize.X, SourceSize.Y, Pixels, Session.Width, Session.Height, Resized, true);
		Pixels = MoveTemp(Resized);
		OutputWidth = Session.Width;
		OutputHeight = Session.Height;
	}

	TArray64<uint8> Compressed;
	FImageUtils::PNGCompressImageArray(OutputWidth, OutputHeight, Pixels, Compressed);
	const FString FramePath = FPaths::Combine(Session.OutputDirectory,
		FString::Printf(TEXT("%s_%06d.png"), *Session.FilePrefix, Session.CapturedFrames));
	if (Compressed.Num() == 0 || !FFileHelper::SaveArrayToFile(Compressed, *FramePath))
	{
		OutError = TEXT("Failed to encode or save a live capture PNG");
		return false;
	}
	Session.FramePaths.Add(FramePath);
	return true;
}

void FLiveCaptureService::BeginFinalize(const bool bSaveVideo)
{
	Session.bStopRequested = true;
	if (Session.bCaptureVideo && Session.bVideoInitialized && VideoRecorder)
	{
		if (!bSaveVideo && VideoFinalizedHandle.IsValid())
		{
			VideoRecorder->GetOnVideoRecordingFinalizedDelegate().Remove(VideoFinalizedHandle);
			VideoFinalizedHandle.Reset();
		}
		Session.State = EState::Finalizing;
		VideoRecorder->FinalizeRecording(bSaveVideo, FText::FromString(TEXT("BAT Live Capture")), FText::GetEmpty(), true);
		if (!bSaveVideo)
		{
			Session.bVideoFinalized = true;
			Session.State = EState::Canceled;
			WriteManifest();
		}
		return;
	}
	CompleteWithoutVideo();
}

void FLiveCaptureService::CompleteWithoutVideo()
{
	Session.State = EState::Completed;
	WriteManifest();
	if (Session.bStopPieWhenComplete && GEditor && GEditor->PlayWorld)
	{
		GEditor->RequestEndPlayMap();
	}
}

void FLiveCaptureService::HandleVideoFinalized(const bool bSucceeded, const FString& PlatformPath)
{
	if (VideoRecorder && VideoFinalizedHandle.IsValid())
	{
		VideoRecorder->GetOnVideoRecordingFinalizedDelegate().Remove(VideoFinalizedHandle);
	}
	VideoFinalizedHandle.Reset();
	Session.bVideoFinalized = true;
	if (!bSucceeded)
	{
		Fail(TEXT("Unreal's platform recorder failed to finalize the MP4"));
		return;
	}

	const FString Destination = FPaths::Combine(Session.OutputDirectory, TEXT("capture.mp4"));
	if (IFileManager::Get().Copy(*Destination, *PlatformPath, true, true) != COPY_OK)
	{
		Fail(FString::Printf(TEXT("MP4 was created but could not be copied into the session folder: %s"), *PlatformPath));
		return;
	}
	Session.VideoPath = Destination;
	Session.State = EState::Completed;
	WriteManifest();
	if (Session.bStopPieWhenComplete && GEditor && GEditor->PlayWorld)
	{
		GEditor->RequestEndPlayMap();
	}
}

void FLiveCaptureService::Fail(const FString& Error)
{
	Session.Error = Error;
	Session.State = EState::Failed;
	if (VideoRecorder && Session.bVideoInitialized
		&& VideoRecorder->GetRecordingState() != EVideoRecordingState::None
		&& VideoRecorder->GetRecordingState() != EVideoRecordingState::Finalizing)
	{
		VideoRecorder->FinalizeRecording(false, FText::GetEmpty(), FText::GetEmpty(), true);
	}
	if (VideoRecorder && VideoFinalizedHandle.IsValid())
	{
		VideoRecorder->GetOnVideoRecordingFinalizedDelegate().Remove(VideoFinalizedHandle);
	}
	VideoFinalizedHandle.Reset();
	WriteManifest();
}

void FLiveCaptureService::WriteManifest()
{
	if (Session.OutputDirectory.IsEmpty() || !IFileManager::Get().DirectoryExists(*Session.OutputDirectory))
	{
		return;
	}
	TSharedRef<FJsonObject> Manifest = BuildStatusObject();
	Manifest->SetArrayField(TEXT("frames"), [&]()
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(Session.FramePaths.Num());
		for (const FString& Frame : Session.FramePaths)
		{
			Values.Add(MakeShared<FJsonValueString>(Frame));
		}
		return Values;
	}());
	Session.ManifestPath = FPaths::Combine(Session.OutputDirectory, TEXT("capture-manifest.json"));
	Manifest->SetStringField(TEXT("manifest"), Session.ManifestPath);
	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Manifest, Writer);
	if (!FFileHelper::SaveStringToFile(Json, *Session.ManifestPath))
	{
		Session.Error = TEXT("Failed to write capture manifest");
		if (Session.State == EState::Completed) Session.State = EState::Failed;
	}
}

TSharedRef<FJsonObject> FLiveCaptureService::BuildStatusObject() const
{
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("session_id"), Session.Id);
	Data->SetStringField(TEXT("state"), StateToString(Session.State));
	Data->SetBoolField(TEXT("active"), IsActiveState(Session.State));
	Data->SetStringField(TEXT("source"), Session.Source);
	Data->SetNumberField(TEXT("pie_index"), Session.PieIndex);
	Data->SetStringField(TEXT("output_format"), Session.OutputFormat);
	Data->SetNumberField(TEXT("fps"), Session.Fps);
	Data->SetNumberField(TEXT("target_frames"), Session.TargetFrames);
	Data->SetNumberField(TEXT("captured_frames"), Session.CapturedFrames);
	Data->SetNumberField(TEXT("dropped_frames"), Session.DroppedFrames);
	Data->SetNumberField(TEXT("progress"), Session.TargetFrames > 0 ? static_cast<double>(Session.CapturedFrames) / Session.TargetFrames : 0.0);
	Data->SetNumberField(TEXT("duration_seconds"), Session.Fps > 0 ? static_cast<double>(Session.TargetFrames) / Session.Fps : 0.0);
	Data->SetNumberField(TEXT("warmup_seconds"), Session.WarmupSeconds);
	Data->SetNumberField(TEXT("width"), Session.Width);
	Data->SetNumberField(TEXT("height"), Session.Height);
	Data->SetStringField(TEXT("output_directory"), Session.OutputDirectory);
	Data->SetStringField(TEXT("first_frame"), Session.FramePaths.Num() > 0 ? Session.FramePaths[0] : FString());
	Data->SetStringField(TEXT("last_frame"), Session.FramePaths.Num() > 0 ? Session.FramePaths.Last() : FString());
	Data->SetStringField(TEXT("video"), Session.VideoPath);
	Data->SetStringField(TEXT("manifest"), Session.ManifestPath);
	Data->SetStringField(TEXT("error"), Session.Error);
	Data->SetBoolField(TEXT("python_used"), false);
	Data->SetBoolField(TEXT("external_process_used"), false);
	Data->SetStringField(TEXT("audio_capture"), Session.bCaptureVideo ? TEXT("platform_if_available") : TEXT("none"));
	return Data;
}

FAutomationResult FLiveCaptureService::Status() const
{
	return FAutomationResult::Ok(ObjectValue(BuildStatusObject()));
}

FAutomationResult FLiveCaptureService::Stop(const TSharedPtr<FJsonObject>& Request)
{
	if (!IsActiveState(Session.State))
	{
		return FAutomationResult::Error(TEXT("capture_not_active"), TEXT("No live capture session is active"), 409);
	}
	FString RequestedId;
	if (Request.IsValid() && Request->TryGetStringField(TEXT("session_id"), RequestedId)
		&& !RequestedId.IsEmpty() && RequestedId != Session.Id)
	{
		return FAutomationResult::Error(TEXT("capture_session_mismatch"), TEXT("session_id does not match the active capture"), 409);
	}
	if (Session.State == EState::Finalizing)
	{
		return FAutomationResult::Ok(ObjectValue(BuildStatusObject()), 202);
	}
	bool bDiscard = false;
	if (Request.IsValid()) Request->TryGetBoolField(TEXT("discard"), bDiscard);
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	BeginFinalize(!bDiscard);
	return FAutomationResult::Ok(ObjectValue(BuildStatusObject()), Session.State == EState::Finalizing ? 202 : 200);
}

void FLiveCaptureService::Shutdown()
{
	const bool bWasActive = IsActiveState(Session.State);
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (VideoRecorder && VideoFinalizedHandle.IsValid())
	{
		VideoRecorder->GetOnVideoRecordingFinalizedDelegate().Remove(VideoFinalizedHandle);
		VideoFinalizedHandle.Reset();
	}
	if (VideoRecorder
		&& VideoRecorder->GetRecordingState() != EVideoRecordingState::None
		&& VideoRecorder->GetRecordingState() != EVideoRecordingState::Finalizing)
	{
		VideoRecorder->FinalizeRecording(false, FText::GetEmpty(), FText::GetEmpty(), true);
	}
	if (bWasActive)
	{
		Session.State = EState::Canceled;
		Session.Error = TEXT("Capture canceled because the BAT live automation service stopped");
		Session.bVideoFinalized = true;
		WriteManifest();
	}
	VideoRecorder = nullptr;
	Owner = nullptr;
}
