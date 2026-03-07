#include "BlueprintAutomationToolkitModule.h"

#include "Commands/AutomationCommand.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Http/HttpRequestUtils.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

namespace
{
	static TSharedPtr<FJsonObject> BuildObjectQueryBody(const FHttpServerRequest& Request)
	{
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();

		auto CopyString = [&Request, &Body](const TCHAR* QueryName, const TCHAR* FieldName)
		{
			if (const FString* Value = Request.QueryParams.Find(QueryName); Value && !Value->TrimStartAndEnd().IsEmpty())
			{
				Body->SetStringField(FieldName, *Value);
			}
		};

		auto CopyBool = [&Request, &Body](const TCHAR* QueryName, const TCHAR* FieldName)
		{
			if (const FString* Value = Request.QueryParams.Find(QueryName))
			{
				const bool bValue = Value->Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value->Equals(TEXT("1"), ESearchCase::CaseSensitive);
				Body->SetBoolField(FieldName, bValue);
			}
		};

		auto CopyNumber = [&Request, &Body](const TCHAR* QueryName, const TCHAR* FieldName)
		{
			if (const FString* Value = Request.QueryParams.Find(QueryName))
			{
				double NumberValue = 0.0;
				if (LexTryParseString(NumberValue, **Value))
				{
					Body->SetNumberField(FieldName, NumberValue);
				}
			}
		};

		CopyString(TEXT("target"), TEXT("target"));
		CopyString(TEXT("objectPath"), TEXT("objectPath"));
		CopyString(TEXT("path"), TEXT("path"));
		CopyString(TEXT("actorName"), TEXT("actorName"));
		CopyString(TEXT("classPath"), TEXT("classPath"));
		CopyString(TEXT("world"), TEXT("world"));
		CopyString(TEXT("function"), TEXT("function"));
		CopyBool(TEXT("selectedActor"), TEXT("selectedActor"));
		CopyBool(TEXT("verbose"), TEXT("verbose"));
		CopyNumber(TEXT("pie_index"), TEXT("pie_index"));
		CopyNumber(TEXT("propertyLimit"), TEXT("propertyLimit"));
		CopyNumber(TEXT("functionLimit"), TEXT("functionLimit"));
		CopyNumber(TEXT("valueLimit"), TEXT("valueLimit"));

		if (const FString* PropertyValue = Request.QueryParams.Find(TEXT("property")); PropertyValue && !PropertyValue->TrimStartAndEnd().IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Properties;
			Properties.Add(MakeShared<FJsonValueString>(*PropertyValue));
			Body->SetArrayField(TEXT("properties"), Properties);
		}
		else if (const FString* PropertiesValue = Request.QueryParams.Find(TEXT("properties")); PropertiesValue && !PropertiesValue->TrimStartAndEnd().IsEmpty())
		{
			TArray<FString> SplitValues;
			PropertiesValue->ParseIntoArray(SplitValues, TEXT(","), true);
			TArray<TSharedPtr<FJsonValue>> Properties;
			for (const FString& Property : SplitValues)
			{
				const FString Trimmed = Property.TrimStartAndEnd();
				if (!Trimmed.IsEmpty())
				{
					Properties.Add(MakeShared<FJsonValueString>(Trimmed));
				}
			}
			Body->SetArrayField(TEXT("properties"), Properties);
		}

		return Body;
	}

	static TSharedPtr<FJsonObject> BuildGraphReadQueryBody(const FHttpServerRequest& Request)
	{
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		if (const FString* Blueprint = Request.QueryParams.Find(TEXT("blueprint")); Blueprint && !Blueprint->TrimStartAndEnd().IsEmpty())
		{
			Body->SetStringField(TEXT("blueprint"), *Blueprint);
		}
		if (const FString* Graph = Request.QueryParams.Find(TEXT("graph")); Graph && !Graph->TrimStartAndEnd().IsEmpty())
		{
			Body->SetStringField(TEXT("graph"), *Graph);
		}
		return Body;
	}
}

void FBlueprintAutomationToolkitModule::BindAutomationCoreRoutes()
{
	auto BindPostCommandRoute = [this](FHttpRouteHandle& Handle, const TCHAR* Path, const TCHAR* Endpoint, bool bNormalizeObjectRequest)
	{
		Handle = Router->BindRoute(
			FHttpPath(Path),
			EHttpServerRequestVerbs::VERB_POST,
			FHttpRequestHandler::CreateLambda([this, Endpoint, bNormalizeObjectRequest](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!ValidateAndHandleRequest(Request, OnComplete, Endpoint))
				{
					return true;
				}

				TSharedPtr<FJsonObject> BodyObj;
				if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
				{
					OnComplete(MakeCanonicalErrorResponse(400, ResolveOrCreateRequestId(Request), TEXT("bad_json"), TEXT("Invalid JSON body.")));
					return true;
				}

				const TSharedPtr<FJsonObject> EffectiveBody = bNormalizeObjectRequest ? NormalizeCanonicalObjectRequest(BodyObj) : BodyObj;
				return DispatchAutomationCommandRoute(Endpoint, Request, OnComplete, EffectiveBody, true);
			}));
	};

	auto BindGetCommandRoute = [this](FHttpRouteHandle& Handle, const TCHAR* Path, const TCHAR* Endpoint, TFunction<TSharedPtr<FJsonObject>(const FHttpServerRequest&)> BodyBuilder, bool bNormalizeObjectRequest)
	{
		Handle = Router->BindRoute(
			FHttpPath(Path),
			EHttpServerRequestVerbs::VERB_GET,
			FHttpRequestHandler::CreateLambda([this, Endpoint, BodyBuilder, bNormalizeObjectRequest](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!ValidateAndHandleRequest(Request, OnComplete, Endpoint))
				{
					return true;
				}

				const TSharedPtr<FJsonObject> BodyObj = BodyBuilder(Request);
				const TSharedPtr<FJsonObject> EffectiveBody = bNormalizeObjectRequest ? NormalizeCanonicalObjectRequest(BodyObj) : BodyObj;
				const FString RequestId = ResolveOrCreateRequestId(Request);
				const FAutomationResult Result = ExecuteAutomationCommand(Endpoint, RequestId, EffectiveBody, true);
				OnComplete(MakeCanonicalResponseFromAutomationResult(Result, RequestId));
				return true;
			}));
	};

	BindGetCommandRoute(ObjectDescribeGetRoute, TEXT("/object/describe"), TEXT("/object/describe"), BuildObjectQueryBody, true);
	BindGetCommandRoute(ObjectGetPropertyRoute, TEXT("/object/get_property"), TEXT("/object/get_property"), BuildObjectQueryBody, true);
	BindPostCommandRoute(ObjectSetPropertyAliasRoute, TEXT("/object/set_property"), TEXT("/object/set_property"), true);
	BindPostCommandRoute(ObjectCallFunctionAliasRoute, TEXT("/object/call_function"), TEXT("/object/call_function"), true);
	BindGetCommandRoute(BlueprintGraphReadRoute, TEXT("/blueprint/graph/read"), TEXT("/blueprint/graph/read"), BuildGraphReadQueryBody, false);
	BindPostCommandRoute(BlueprintCompileSaveRoute, TEXT("/blueprint/compile_save"), TEXT("/blueprint/compile_save"), false);
	BindPostCommandRoute(ActorDestroyRoute, TEXT("/actor/destroy"), TEXT("/actor/destroy"), true);
	BindPostCommandRoute(EditorSelectRoute, TEXT("/editor/select"), TEXT("/editor/select"), true);
	BindPostCommandRoute(EditorFocusRoute, TEXT("/editor/focus"), TEXT("/editor/focus"), true);
}