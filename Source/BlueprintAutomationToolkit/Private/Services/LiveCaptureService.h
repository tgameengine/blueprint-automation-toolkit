// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"
#include "Containers/Ticker.h"

class FBlueprintAutomationToolkitModule;
class FJsonObject;
class IVideoRecordingSystem;

/**
 * Asynchronous editor/PIE viewport capture that advances with real engine
 * ticks. One session may be active at a time. PNG sequences are portable;
 * Win64 can additionally use Unreal's platform MP4 recorder.
 */
class FLiveCaptureService
{
public:
	~FLiveCaptureService();
	FAutomationResult DescribeSchema() const;
	FAutomationResult Start(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request);
	FAutomationResult Status() const;
	FAutomationResult Stop(const TSharedPtr<FJsonObject>& Request);
	void Shutdown();

private:
	enum class EState : uint8
	{
		Idle,
		WarmingUp,
		Recording,
		Finalizing,
		Completed,
		Failed,
		Canceled,
	};

	struct FSession
	{
		FString Id;
		EState State = EState::Idle;
		FString Source = TEXT("auto");
		FString OutputFormat = TEXT("png_sequence");
		FString OutputDirectory;
		FString ManifestPath;
		FString VideoPath;
		FString Error;
		FString FilePrefix = TEXT("frame");
		int32 PieIndex = 0;
		int32 Fps = 30;
		int32 TargetFrames = 300;
		int32 CapturedFrames = 0;
		int32 DroppedFrames = 0;
		int32 Width = 0;
		int32 Height = 0;
		double WarmupSeconds = 0.0;
		double StartedSeconds = 0.0;
		double RecordingStartedSeconds = 0.0;
		double NextFrameSeconds = 0.0;
		bool bCapturePng = true;
		bool bCaptureVideo = false;
		bool bVideoInitialized = false;
		bool bVideoFinalized = false;
		bool bStopPieWhenComplete = false;
		bool bStopRequested = false;
		TArray<FString> FramePaths;
	};

	bool Tick(float DeltaSeconds);
	bool BeginRecording(FString& OutError);
	bool CaptureFrame(FString& OutError);
	void BeginFinalize(bool bSaveVideo);
	void CompleteWithoutVideo();
	void Fail(const FString& Error);
	void WriteManifest();
	void HandleVideoFinalized(bool bSucceeded, const FString& PlatformPath);
	TSharedRef<FJsonObject> BuildStatusObject() const;
	static FString StateToString(EState State);
	static bool IsActiveState(EState State);

	FBlueprintAutomationToolkitModule* Owner = nullptr;
	FSession Session;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle VideoFinalizedHandle;
	IVideoRecordingSystem* VideoRecorder = nullptr;
};
