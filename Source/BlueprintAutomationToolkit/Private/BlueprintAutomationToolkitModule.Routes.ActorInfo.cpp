#include "BlueprintAutomationToolkitModule.h"

#include "Commands/CommandDispatcher.h"
#include "Commands/GetActorInfoCommand.h"
#include "Commands/GetActorPropertiesCommand.h"
#include "Services/ActorService.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Misc/Guid.h"
#include "IHttpRouter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace
{
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

	static bool ActorMatchesName(const AActor* Actor, const FString& Target)
	{
		if (!Actor)
		{
			return false;
		}

		if (Actor->GetName().Equals(Target, ESearchCase::IgnoreCase))
		{
			return true;
		}

#if WITH_EDITOR
		if (Actor->GetActorLabel().Equals(Target, ESearchCase::IgnoreCase))
		{
			return true;
		}
#endif

		return false;
	}

	static AActor* FindActorByNameOrLabel(UWorld* World, const FString& Target)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (ActorMatchesName(Actor, Target))
			{
				return Actor;
			}
		}

		return nullptr;
	}
}

void FBlueprintAutomationToolkitModule::BindActorInfoRoutes()
{
	ActorIntrospectRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/actor/introspect")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/actor/introspect")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString Target;
			BodyObj->TryGetStringField(TEXT("target"), Target);
			Target.TrimStartAndEndInline();

			FString Filter;
			BodyObj->TryGetStringField(TEXT("filter"), Filter);
			Filter.TrimStartAndEndInline();

			double MaxFunctionsD = 200.0;
			BodyObj->TryGetNumberField(TEXT("max_functions"), MaxFunctionsD);
			const int32 MaxFunctions = (int32)FMath::Clamp(MaxFunctionsD, 0.0, 2000.0);

			const FString RequestId = ResolveOrCreateRequestId(Request);
			FActorService Service([this, Target, Filter, MaxFunctions](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, Target, Filter, MaxFunctions, &ThreadResult]()
				{
					bool bIsPie = false;
					int32 ResolvedPieIndex = -1;
					FString ResolveError;
					UWorld* World = ResolveWorld(TEXT("pie"), 0, bIsPie, ResolvedPieIndex, ResolveError);
					if (!World)
					{
						ThreadResult = FAutomationResult::Error(TEXT("not_in_pie"), ResolveError.IsEmpty() ? TEXT("PIE must be running") : ResolveError, (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}
					APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
					APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;

					auto IsPlayerAlias = [](const FString& S) -> bool
					{
						return S.IsEmpty() || S.Equals(TEXT("player"), ESearchCase::IgnoreCase) || S.Equals(TEXT("pawn"), ESearchCase::IgnoreCase) || S.Equals(TEXT("player_pawn"), ESearchCase::IgnoreCase);
					};

					AActor* Actor = nullptr;
					if (IsPlayerAlias(Target))
					{
						Actor = PlayerPawn;
					}
					else
					{
						Actor = ResolveActor(World, Target);
					}

					TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetBoolField(TEXT("ok"), true);
					Obj->SetBoolField(TEXT("found"), Actor != nullptr);
					Obj->SetStringField(TEXT("target"), Target);
					Obj->SetStringField(TEXT("resolved_actor"), Actor ? Actor->GetName() : TEXT(""));
					Obj->SetStringField(TEXT("resolved_actor_class"), Actor ? Actor->GetClass()->GetName() : TEXT(""));
					Obj->SetStringField(TEXT("player_pawn"), PlayerPawn ? PlayerPawn->GetName() : TEXT(""));
					Obj->SetStringField(TEXT("player_pawn_class"), PlayerPawn ? PlayerPawn->GetClass()->GetName() : TEXT(""));

					if (!Actor)
					{
						ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
						return;
					}

					UClass* Cls = Actor->GetClass();
					TArray<TSharedPtr<FJsonValue>> SuperChain;
					{
						UClass* It = Cls;
						int32 Depth = 0;
						while (It && Depth++ < 16)
						{
							SuperChain.Add(MakeShared<FJsonValueString>(It->GetName()));
							It = It->GetSuperClass();
						}
					}
					Obj->SetArrayField(TEXT("class_chain"), SuperChain);

					TArray<TSharedPtr<FJsonValue>> Components;
					{
						TInlineComponentArray<UActorComponent*> Comps;
						Actor->GetComponents(Comps);
						const int32 MaxComps = 128;
						for (int32 i = 0; i < Comps.Num() && i < MaxComps; ++i)
						{
							UActorComponent* C = Comps[i];
							if (!C)
							{
								continue;
							}
							TSharedRef<FJsonObject> CObj = MakeShared<FJsonObject>();
							CObj->SetStringField(TEXT("name"), C->GetName());
							CObj->SetStringField(TEXT("class"), C->GetClass()->GetName());
							Components.Add(MakeShared<FJsonValueObject>(CObj));
						}
					}
					Obj->SetArrayField(TEXT("components"), Components);

					TArray<FString> Tokens;
					if (Filter.IsEmpty())
					{
						Tokens = { TEXT("Fire"), TEXT("Shoot"), TEXT("Geometry"), TEXT("Tool"), TEXT("Carve") };
					}
					else
					{
						Filter.ParseIntoArray(Tokens, TEXT("|"), /*CullEmpty*/ true);
						for (FString& T : Tokens)
						{
							T.TrimStartAndEndInline();
						}
						Tokens.RemoveAll([](const FString& T) { return T.IsEmpty(); });
					}

					auto Matches = [&Tokens](const FString& Name) -> bool
					{
						if (Tokens.Num() == 0)
						{
							return true;
						}
						for (const FString& T : Tokens)
						{
							if (Name.Contains(T, ESearchCase::IgnoreCase))
							{
								return true;
							}
						}
						return false;
					};

					TArray<TSharedPtr<FJsonValue>> Functions;
					int32 Added = 0;
					for (TFieldIterator<UFunction> It(Cls, EFieldIteratorFlags::IncludeSuper); It; ++It)
					{
						UFunction* Func = *It;
						if (!Func)
						{
							continue;
						}
						const FString FuncName = Func->GetName();
						if (!Matches(FuncName))
						{
							continue;
						}
						TSharedRef<FJsonObject> FObj = MakeShared<FJsonObject>();
						FObj->SetStringField(TEXT("name"), FuncName);
						FObj->SetBoolField(TEXT("exec"), Func->HasAnyFunctionFlags(FUNC_Exec));
						FObj->SetBoolField(TEXT("blueprint_callable"), Func->HasAnyFunctionFlags(FUNC_BlueprintCallable));
						FObj->SetBoolField(TEXT("blueprint_event"), Func->HasAnyFunctionFlags(FUNC_BlueprintEvent));
						Functions.Add(MakeShared<FJsonValueObject>(FObj));
						if (++Added >= MaxFunctions)
						{
							break;
						}
					}
					Obj->SetArrayField(TEXT("functions"), Functions);
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by actor introspect operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FGetActorInfoCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext Context;
			Context.RequestId = RequestId;
			Context.Body = BodyObj;

			const FAutomationResult Result = Dispatcher.Dispatch(Command, Context);
			if (!Result.bSuccess)
			{
				OnComplete(MakeErrorResponse(Result.StatusCode, RequestId, Result.ErrorCode, Result.ErrorMessage));
				return true;
			}

			BAT::Http::JsonOk(OnComplete, Result.Data, Result.StatusCode, RequestId);

			return true;
		}));

	ActorPropertiesRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/actor/properties")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/actor/properties")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString Target;
			if (!BodyObj->TryGetStringField(TEXT("target"), Target) || Target.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_target"), TEXT("Body must include 'target'")));
				return true;
			}

			const FBATExecWorldRequest WorldReq = ParseExecWorldRequest(BodyObj);
			if (WorldReq.bRequireExplicitWorld && !WorldReq.bHasWorldField)
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("world_required"), TEXT("Body must include 'world' when require_world=true (editor|pie)")));
				return true;
			}

			const TArray<TSharedPtr<FJsonValue>>* PropsArrPtr = nullptr;
			if (!BodyObj->TryGetArrayField(TEXT("properties"), PropsArrPtr) || !PropsArrPtr)
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_properties"), TEXT("Body must include 'properties' array")));
				return true;
			}

			TArray<FString> PropertyNames;
			PropertyNames.Reserve(PropsArrPtr->Num());
			for (const TSharedPtr<FJsonValue>& V : *PropsArrPtr)
			{
				if (!V.IsValid())
				{
					continue;
				}
				FString P;
				if (V->TryGetString(P) && !P.IsEmpty())
				{
					P.TrimStartAndEndInline();
					if (!P.IsEmpty())
					{
						PropertyNames.Add(P);
					}
				}
			}

			if (PropertyNames.Num() == 0)
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("empty_properties"), TEXT("'properties' must contain at least one non-empty string")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);

			FActorService Service([this, Target, WorldReq, PropertyNames](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, Target, WorldReq, PropertyNames, &ThreadResult]()
				{
					bool bIsPie = false;
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
					UWorld* World = ResolveWorld(WorldMode, WorldReq.PieIndex, bIsPie, ResolvedPieIndex, ResolveError);
					if (!World)
					{
						ThreadResult = FAutomationResult::Error(TEXT("no_world"), ResolveError.IsEmpty() ? TEXT("No valid world context") : ResolveError, (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					if (WorldReq.Mode == EBATExecWorldMode::Pie && !bIsPie)
					{
						ThreadResult = FAutomationResult::Error(TEXT("not_in_pie"), TEXT("PIE must be running (and pie_index valid) when world=pie"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					AActor* TargetActor = ResolveActor(World, Target);
					if (!TargetActor)
					{
						TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
						Obj->SetBoolField(TEXT("ok"), true);
						Obj->SetBoolField(TEXT("found"), false);
						Obj->SetStringField(TEXT("target"), Target);
						Obj->SetStringField(TEXT("world"), bIsPie ? TEXT("pie") : TEXT("editor"));
						Obj->SetNumberField(TEXT("pie_index"), ResolvedPieIndex);
						ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
						return;
					}

					TSharedRef<FJsonObject> ValuesObj = MakeShared<FJsonObject>();
					for (const FString& PropName : PropertyNames)
					{
						const FName PropFName(*PropName);
						FProperty* Prop = FindFProperty<FProperty>(TargetActor->GetClass(), PropFName);
						if (!Prop)
						{
							ValuesObj->SetField(PropName, MakeShared<FJsonValueNull>());
							continue;
						}

						if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
						{
							const bool bVal = BoolProp->GetPropertyValue_InContainer(TargetActor);
							ValuesObj->SetBoolField(PropName, bVal);
							continue;
						}

						if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
						{
							const int64 RawValue = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(TargetActor));
							const UEnum* Enum = EnumProp->GetEnum();
							ValuesObj->SetStringField(PropName, Enum ? Enum->GetNameStringByValue(RawValue) : FString::Printf(TEXT("%lld"), RawValue));
							continue;
						}

						if (const FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
						{
							double Num = 0.0;
							if (NumProp->IsInteger())
							{
								Num = (double)NumProp->GetSignedIntPropertyValue(NumProp->ContainerPtrToValuePtr<void>(TargetActor));
							}
							else
							{
								Num = (double)NumProp->GetFloatingPointPropertyValue(NumProp->ContainerPtrToValuePtr<void>(TargetActor));
							}
							ValuesObj->SetNumberField(PropName, Num);
							continue;
						}

						if (const FStrProperty* StrProp = CastField<FStrProperty>(Prop))
						{
							ValuesObj->SetStringField(PropName, StrProp->GetPropertyValue_InContainer(TargetActor));
							continue;
						}

						if (const FNameProperty* NameProp = CastField<FNameProperty>(Prop))
						{
							ValuesObj->SetStringField(PropName, NameProp->GetPropertyValue_InContainer(TargetActor).ToString());
							continue;
						}

						if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
						{
							UObject* ObjVal = ObjProp->GetObjectPropertyValue_InContainer(TargetActor);
							if (!ObjVal)
							{
								ValuesObj->SetField(PropName, MakeShared<FJsonValueNull>());
								continue;
							}

							if (AActor* ActorVal = Cast<AActor>(ObjVal))
							{
								ValuesObj->SetStringField(PropName, ActorVal->GetActorLabel());
								continue;
							}

							ValuesObj->SetStringField(PropName, ObjVal->GetName());
							continue;
						}

						if (const FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
						{
							const FSoftObjectPtr SoftPtr = SoftObjProp->GetPropertyValue_InContainer(TargetActor);
							const FString Path = SoftPtr.ToSoftObjectPath().ToString();
							ValuesObj->SetStringField(PropName, Path);
							continue;
						}

						FString TextValue;
						Prop->ExportText_InContainer(0, TextValue, TargetActor, TargetActor, TargetActor, PPF_None);
						ValuesObj->SetStringField(PropName, TextValue);
					}

					TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetBoolField(TEXT("ok"), true);
					Obj->SetBoolField(TEXT("found"), true);
					Obj->SetStringField(TEXT("target"), Target);
					Obj->SetStringField(TEXT("target_actor"), TargetActor->GetName());
					Obj->SetStringField(TEXT("world"), bIsPie ? TEXT("pie") : TEXT("editor"));
					Obj->SetNumberField(TEXT("pie_index"), ResolvedPieIndex);
					Obj->SetStringField(TEXT("class"), TargetActor->GetClass() ? TargetActor->GetClass()->GetName() : TEXT(""));
					Obj->SetObjectField(TEXT("values"), ValuesObj);
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by actor properties operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FGetActorPropertiesCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext Context;
			Context.RequestId = RequestId;
			Context.Body = BodyObj;

			const FAutomationResult Result = Dispatcher.Dispatch(Command, Context);
			if (!Result.bSuccess)
			{
				OnComplete(MakeErrorResponse(Result.StatusCode, RequestId, Result.ErrorCode, Result.ErrorMessage));
				return true;
			}

			BAT::Http::JsonOk(OnComplete, Result.Data, Result.StatusCode, RequestId);

			return true;
		}));
}
