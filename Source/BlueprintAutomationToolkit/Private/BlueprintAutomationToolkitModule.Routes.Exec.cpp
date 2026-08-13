// Copyright 2026 AkaSoft. All Rights Reserved.

#include "BlueprintAutomationToolkitModule.h"

#include "Commands/CommandDispatcher.h"
#include "Commands/ExecCommand.h"
#include "Services/ActorService.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "Http/HttpRequestUtils.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Misc/Guid.h"
#include "Misc/OutputDevice.h"
#include "Runtime/Launch/Resources/Version.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
#include "Misc/StringOutputDevice.h"
#endif
#include "IHttpRouter.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintAutomationToolkitExecRoute, Log, All);

namespace
{
	class FBATLogCapture : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			static const FName LogPython(TEXT("LogPython"));
			if (Category == LogPython)
			{
				if (!CapturedOutput.IsEmpty())
				{
					CapturedOutput += TEXT("\n");
				}
				CapturedOutput += V;
			}
		}

		FString CapturedOutput;
	};
	static TUniquePtr<FHttpServerResponse> MakeJsonResponse(EHttpServerResponseCodes Code, const FString& JsonString)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(JsonString, TEXT("application/json"));
		Response->Code = Code;
		Response->Headers.FindOrAdd(TEXT("Cache-Control")).Add(TEXT("no-store"));
		Response->Headers.FindOrAdd(TEXT("X-Request-Id")).Add(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
		return Response;
	}

	static FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	static bool TryParseJsonBody(const TArray<uint8>& Body, TSharedPtr<FJsonObject>& OutObj)
	{
		FString BodyString = FString(Body.Num(), reinterpret_cast<const ANSICHAR*>(Body.GetData()));
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
		return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
	}

	static FString MakePythonExecViaConsoleCommand(const FString& PythonCode)
	{
		FTCHARToUTF8 Convert(*PythonCode);
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Convert.Get()), Convert.Length());
		const FString Enc = FBase64::Encode(Bytes);
		return FString::Printf(
			TEXT("py import base64;exec(compile(base64.b64decode('%s').decode('utf-8'),'<uas>', 'exec'),{'__builtins__':__builtins__,'__name__':'__bat_exec__'})"),
			*Enc);
	}

	enum class EBATExecWorldMode : uint8
	{
		Auto,
		Editor,
		Pie,
	};

	struct FBATExecWorldRequest
	{
		EBATExecWorldMode Mode = EBATExecWorldMode::Auto;
		bool bHasWorldField = false;
		bool bRequireExplicitWorld = false;
		int32 PieIndex = 0;
	};

	static FBATExecWorldRequest ParseExecWorldRequest(const TSharedPtr<FJsonObject>& BodyObj)
	{
		FBATExecWorldRequest Req;
		if (!BodyObj.IsValid())
		{
			return Req;
		}

		BodyObj->TryGetBoolField(TEXT("require_world"), Req.bRequireExplicitWorld);

		double PieIndexD = 0.0;
		if (BodyObj->TryGetNumberField(TEXT("pie_index"), PieIndexD))
		{
			Req.PieIndex = FMath::Max(0, (int32)PieIndexD);
		}

		FString Mode;
		Req.bHasWorldField = BodyObj->TryGetStringField(TEXT("world"), Mode);
		Mode.TrimStartAndEndInline();
		if (Mode.Equals(TEXT("editor"), ESearchCase::IgnoreCase))
		{
			Req.Mode = EBATExecWorldMode::Editor;
		}
		else if (Mode.Equals(TEXT("pie"), ESearchCase::IgnoreCase))
		{
			Req.Mode = EBATExecWorldMode::Pie;
		}
		else
		{
			Req.Mode = EBATExecWorldMode::Auto;
		}

		return Req;
	}

	static UWorld* GetWorldForExecMode(EBATExecWorldMode Mode, int32 PieIndex, bool& bOutPie, int32& OutResolvedPieIndex)
	{
		bOutPie = false;
		OutResolvedPieIndex = -1;
		if (!GEditor)
		{
			return nullptr;
		}

		auto GetPieWorldByIndex = [&](int32 Index) -> UWorld*
		{
			TArray<UWorld*> PieWorlds;
			for (const FWorldContext& Ctx : GEditor->GetWorldContexts())
			{
				if (Ctx.WorldType == EWorldType::PIE && Ctx.World())
				{
					PieWorlds.Add(Ctx.World());
				}
			}

			if (!PieWorlds.IsValidIndex(Index))
			{
				return nullptr;
			}

			OutResolvedPieIndex = Index;
			return PieWorlds[Index];
		};

		switch (Mode)
		{
		case EBATExecWorldMode::Pie:
			bOutPie = true;
			{
				UWorld* Pie = GetPieWorldByIndex(PieIndex);
				if (Pie)
				{
					return Pie;
				}
				if (GEditor->PlayWorld != nullptr)
				{
					OutResolvedPieIndex = 0;
					return GEditor->PlayWorld;
				}
				return nullptr;
			}
		case EBATExecWorldMode::Editor:
			bOutPie = false;
			return GEditor->GetEditorWorldContext().World();
		case EBATExecWorldMode::Auto:
		default:
			if (GEditor->PlayWorld != nullptr)
			{
				bOutPie = true;
				UWorld* Pie = GetPieWorldByIndex(PieIndex);
				if (Pie)
				{
					return Pie;
				}
				OutResolvedPieIndex = 0;
				return GEditor->PlayWorld;
			}
			bOutPie = false;
			return GEditor->GetEditorWorldContext().World();
		}
	}
}

void FBlueprintAutomationToolkitModule::BindExecRoute()
{
	ExecRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/exec")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/exec")))
			{
				return true;
			}

			if (!bEnableExecRoute)
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("exec_route_disabled"), TEXT("Exec route is disabled")));
				UE_LOG(LogBlueprintAutomationToolkitExecRoute, Warning, TEXT("Denied request (/ai/exec): exec_route_disabled"));
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString Command;

			FString PythonCode;
			BodyObj->TryGetStringField(TEXT("python"), PythonCode);
			PythonCode.TrimStartAndEndInline();
			const bool bIsPython = !PythonCode.IsEmpty();

			TArray<FString> BatchCommands;
			const TArray<TSharedPtr<FJsonValue>>* CommandsField = nullptr;
			const bool bHasCommandsArray = BodyObj->TryGetArrayField(TEXT("commands"), CommandsField) && CommandsField && CommandsField->Num() > 0;
			const bool bIsBatch = bHasCommandsArray;

			if (bIsPython)
			{
				if (!bAllowPythonExec)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::Denied, TEXT("python_disabled"), TEXT("Python execution is disabled. Set [BlueprintAutomationToolkit] bAllowPythonExec=true")));
					UE_LOG(LogBlueprintAutomationToolkitExecRoute, Warning, TEXT("Denied request (/ai/exec): python_disabled"));
					return true;
				}

				if (bSafeModeEnabled)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::Denied, TEXT("python_requires_unsafe_mode"), TEXT("Python execution requires unsafe mode (safe mode disabled).")));
					UE_LOG(LogBlueprintAutomationToolkitExecRoute, Warning, TEXT("Denied request (/ai/exec): python_requires_unsafe_mode"));
					return true;
				}
				Command = MakePythonExecViaConsoleCommand(PythonCode);
			}
			else if (bIsBatch)
			{
				BatchCommands.Reserve(CommandsField->Num());
				for (const TSharedPtr<FJsonValue>& V : *CommandsField)
				{
					if (!V.IsValid() || V->Type != EJson::String)
					{
						continue;
					}
					FString Cmd = V->AsString();
					Cmd.TrimStartAndEndInline();
					if (!Cmd.IsEmpty())
					{
						BatchCommands.Add(MoveTemp(Cmd));
					}
				}
				if (BatchCommands.Num() == 0)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_commands"), TEXT("Body must include non-empty 'commands' array")));
					return true;
				}

				FString SandboxReason;
				for (const FString& BatchCommand : BatchCommands)
				{
					if (!IsCommandAllowedBySandbox(BatchCommand, SandboxReason))
					{
						UE_LOG(LogBlueprintAutomationToolkitExecRoute, Warning, TEXT("Denied request (/ai/exec): %s"), *SandboxReason);
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::Denied, TEXT("sandbox_blocked"), SandboxReason));
						return true;
					}
				}
			}
			else
			{
				if (!BodyObj->TryGetStringField(TEXT("command"), Command) || Command.IsEmpty())
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_command"), TEXT("Body must include 'command' (or 'commands' / 'python')")));
					return true;
				}

				FString SandboxReason;
				if (!IsCommandAllowedBySandbox(Command, SandboxReason))
				{
					UE_LOG(LogBlueprintAutomationToolkitExecRoute, Warning, TEXT("Denied request (/ai/exec): %s"), *SandboxReason);
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::Denied, TEXT("sandbox_blocked"), SandboxReason));
					return true;
				}
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FBATExecWorldRequest WorldReq = ParseExecWorldRequest(BodyObj);
			if (WorldReq.bRequireExplicitWorld && (!WorldReq.bHasWorldField || WorldReq.Mode == EBATExecWorldMode::Auto))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("world_required"), TEXT("Body must include 'world' when require_world=true (editor|pie)")));
				return true;
			}

			FActorService Service([this, Command, BatchCommands, bIsBatch, bIsPython, WorldReq](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, Command, BatchCommands, bIsBatch, bIsPython, WorldReq, &ThreadResult]()
				{
					bool bPie = false;
					int32 ResolvedPieIndex = -1;
					FString WorldMode = TEXT("auto");
					if (WorldReq.Mode == EBATExecWorldMode::Editor)
					{
						WorldMode = TEXT("editor");
					}
					else if (WorldReq.Mode == EBATExecWorldMode::Pie)
					{
						WorldMode = TEXT("pie");
					}
					FString ResolveError;
					UWorld* World = ResolveWorld(WorldMode, WorldReq.PieIndex, bPie, ResolvedPieIndex, ResolveError);

					if (WorldReq.Mode == EBATExecWorldMode::Pie && !World)
					{
						ThreadResult = FAutomationResult::Error(TEXT("not_in_pie"), TEXT("PIE must be running (and pie_index valid) when world=pie"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}
					if (!World)
					{
						ThreadResult = FAutomationResult::Error(TEXT("no_world"), ResolveError.IsEmpty() ? TEXT("No valid world context") : ResolveError, (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					if (bIsBatch)
					{
						TArray<TSharedPtr<FJsonValue>> Results;
						Results.Reserve(BatchCommands.Num());

						bool bAllOk = true;
						for (const FString& Cmd : BatchCommands)
						{
							FStringOutputDevice Out;
							bool bOk = false;
							if (!TryExecBatCommandDirect(World, Cmd, Out, bOk))
							{
								if (GEngine)
								{
									bOk = GEngine->Exec(World, *Cmd, Out);
								}
								if (!bOk)
								{
									bOk = IConsoleManager::Get().ProcessUserConsoleInput(*Cmd, Out, World);
								}
							}
							bAllOk &= bOk;

							TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
							R->SetStringField(TEXT("command"), Cmd);
							R->SetBoolField(TEXT("ok"), bOk);
							R->SetStringField(TEXT("output"), FString(Out));
							Results.Add(MakeShared<FJsonValueObject>(R));
						}

						TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
						Obj->SetStringField(TEXT("mode"), TEXT("batch"));
						Obj->SetBoolField(TEXT("ok"), bAllOk);
						Obj->SetBoolField(TEXT("pie"), bPie);
						Obj->SetStringField(TEXT("world"), bPie ? TEXT("pie") : TEXT("editor"));
						if (bPie)
						{
							Obj->SetNumberField(TEXT("pie_index"), ResolvedPieIndex);
						}
						Obj->SetArrayField(TEXT("results"), Results);
						ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
						return;
					}

					FStringOutputDevice Out;
					bool bOk = false;

					TUniquePtr<FBATLogCapture> PythonLogCapture;
					if (bIsPython)
					{
						PythonLogCapture = MakeUnique<FBATLogCapture>();
						GLog->AddOutputDevice(PythonLogCapture.Get());
					}

					if (!TryExecBatCommandDirect(World, Command, Out, bOk))
					{
						if (GEngine)
						{
							bOk = GEngine->Exec(World, *Command, Out);
						}
						if (!bOk)
						{
							bOk = IConsoleManager::Get().ProcessUserConsoleInput(*Command, Out, World);
						}
					}

					if (PythonLogCapture.IsValid())
					{
						GLog->RemoveOutputDevice(PythonLogCapture.Get());
						if (Out.IsEmpty() && !PythonLogCapture->CapturedOutput.IsEmpty())
						{
							Out.Log(*PythonLogCapture->CapturedOutput);
						}
					}

					TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetStringField(TEXT("mode"), bIsPython ? TEXT("python") : TEXT("command"));
					Obj->SetBoolField(TEXT("ok"), bOk);
					Obj->SetBoolField(TEXT("pie"), bPie);
					Obj->SetStringField(TEXT("world"), bPie ? TEXT("pie") : TEXT("editor"));
					if (bPie)
					{
						Obj->SetNumberField(TEXT("pie_index"), ResolvedPieIndex);
					}
					Obj->SetStringField(TEXT("output"), FString(Out));
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by exec operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FExecCommand ExecCommand(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext Context;
			Context.RequestId = RequestId;
			Context.Body = BodyObj;

			const FAutomationResult Result = Dispatcher.Dispatch(ExecCommand, Context);
			if (!Result.bSuccess)
			{
				OnComplete(MakeErrorResponse(Result.StatusCode, RequestId, Result.ErrorCode, Result.ErrorMessage));
				return true;
			}

			BAT::Http::JsonOk(OnComplete, Result.Data, Result.StatusCode, RequestId);

			return true;
		}));
}
