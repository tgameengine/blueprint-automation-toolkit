#include "Transport/RequestParsing.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerRequest.h"

namespace BAT::Transport
{
	TSharedPtr<FJsonObject> BuildObjectQueryBody(const FHttpServerRequest& Request)
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

	TSharedPtr<FJsonObject> BuildBlueprintGraphReadQueryBody(const FHttpServerRequest& Request)
	{
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();

		auto CopyBool = [&Request, &Body](const TCHAR* QueryName, const TCHAR* FieldName)
		{
			if (const FString* Value = Request.QueryParams.Find(QueryName))
			{
				const bool bValue = Value->Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value->Equals(TEXT("1"), ESearchCase::CaseSensitive);
				Body->SetBoolField(FieldName, bValue);
			}
		};

		if (const FString* Blueprint = Request.QueryParams.Find(TEXT("blueprint")); Blueprint && !Blueprint->TrimStartAndEnd().IsEmpty())
		{
			Body->SetStringField(TEXT("blueprint"), *Blueprint);
		}
		if (const FString* Graph = Request.QueryParams.Find(TEXT("graph")); Graph && !Graph->TrimStartAndEnd().IsEmpty())
		{
			Body->SetStringField(TEXT("graph"), *Graph);
		}
		CopyBool(TEXT("includeNodeProperties"), TEXT("includeNodeProperties"));
		CopyBool(TEXT("includeNodeValidation"), TEXT("includeNodeValidation"));

		if (const FString* PropertyPaths = Request.QueryParams.Find(TEXT("propertyPaths")); PropertyPaths && !PropertyPaths->TrimStartAndEnd().IsEmpty())
		{
			TArray<FString> SplitValues;
			PropertyPaths->ParseIntoArray(SplitValues, TEXT(","), true);
			TArray<TSharedPtr<FJsonValue>> PropertyPathValues;
			for (const FString& PropertyPath : SplitValues)
			{
				const FString Trimmed = PropertyPath.TrimStartAndEnd();
				if (!Trimmed.IsEmpty())
				{
					PropertyPathValues.Add(MakeShared<FJsonValueString>(Trimmed));
				}
			}
			if (PropertyPathValues.Num() > 0)
			{
				Body->SetArrayField(TEXT("propertyPaths"), PropertyPathValues);
			}
		}
		return Body;
	}

	bool TryParseJsonObjectBody(const FHttpServerRequest& Request, TSharedPtr<FJsonObject>& OutBody)
	{
		return BAT::Http::TryParseJsonBody(Request.Body, OutBody) && OutBody.IsValid();
	}
}