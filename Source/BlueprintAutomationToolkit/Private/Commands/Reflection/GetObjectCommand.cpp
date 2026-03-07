#include "Commands/Reflection/GetObjectCommand.h"

#include "Dom/JsonObject.h"
#include "Services/Reflection/ReflectionObjectResolver.h"
#include "Services/Reflection/ReflectionPropertyService.h"
#include "Services/Reflection/ReflectionSerializationService.h"
#include "Services/Reflection/ReflectionTypes.h"

FAutomationResult FGetObjectCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	if (Context.Endpoint.Equals(TEXT("/object/resolve"), ESearchCase::CaseSensitive))
	{
		FReflectionObjectResolver Resolver;
		BAT::Reflection::FResolvedObject ResolvedObject;
		FAutomationResult Failure;
		if (!Resolver.Resolve(*Context.Module, Context.Body, Context.RequestId, ResolvedObject, Failure))
		{
			return Failure;
		}

		FReflectionSerializationService Serialization;
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> ObjectInfo = Serialization.SerializeObjectReference(ResolvedObject.Object);
		ObjectInfo->SetStringField(TEXT("resolutionSource"), ResolvedObject.ResolutionSource);
		Data->SetObjectField(TEXT("object"), ObjectInfo);
		return BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
	}

	static const FReflectionPropertyService Service;
	return Service.GetObject(*Context.Module, Context.RequestId, Context.Body);
}