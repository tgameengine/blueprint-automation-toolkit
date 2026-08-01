// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Services/UMGDesignerService.h"

#include "AssetToolsModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Dom/JsonObject.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "JsonObjectConverter.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Services/BlueprintCompileDiagnosticsService.h"
#include "UObject/Field.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace
{
	constexpr int32 MaxWidgetCount = 500;
	constexpr int32 MaxWidgetDepth = 32;

	struct FWidgetTypeDefinition
	{
		const TCHAR* Name;
		const TCHAR* ClassPath;
		bool bPanel;
		const TCHAR* Purpose;
	};

	static const FWidgetTypeDefinition WidgetTypes[] = {
		{TEXT("CanvasPanel"), TEXT("/Script/UMG.CanvasPanel"), true, TEXT("Responsive anchored and absolute layout")},
		{TEXT("Overlay"), TEXT("/Script/UMG.Overlay"), true, TEXT("Layered content")},
		{TEXT("VerticalBox"), TEXT("/Script/UMG.VerticalBox"), true, TEXT("Vertical flow layout")},
		{TEXT("HorizontalBox"), TEXT("/Script/UMG.HorizontalBox"), true, TEXT("Horizontal flow layout")},
		{TEXT("GridPanel"), TEXT("/Script/UMG.GridPanel"), true, TEXT("Row and column layout")},
		{TEXT("UniformGridPanel"), TEXT("/Script/UMG.UniformGridPanel"), true, TEXT("Uniform row and column layout")},
		{TEXT("WrapBox"), TEXT("/Script/UMG.WrapBox"), true, TEXT("Responsive wrapping flow")},
		{TEXT("ScrollBox"), TEXT("/Script/UMG.ScrollBox"), true, TEXT("Scrollable content")},
		{TEXT("WidgetSwitcher"), TEXT("/Script/UMG.WidgetSwitcher"), true, TEXT("One active child from multiple pages")},
		{TEXT("Border"), TEXT("/Script/UMG.Border"), true, TEXT("Single-child visual container")},
		{TEXT("Button"), TEXT("/Script/UMG.Button"), true, TEXT("Single-child interactive button")},
		{TEXT("SizeBox"), TEXT("/Script/UMG.SizeBox"), true, TEXT("Single-child size constraints")},
		{TEXT("ScaleBox"), TEXT("/Script/UMG.ScaleBox"), true, TEXT("Single-child responsive scaling")},
		{TEXT("SafeZone"), TEXT("/Script/UMG.SafeZone"), true, TEXT("Platform safe-area container")},
		{TEXT("RetainerBox"), TEXT("/Script/UMG.RetainerBox"), true, TEXT("Cached single-child rendering")},
		{TEXT("TextBlock"), TEXT("/Script/UMG.TextBlock"), false, TEXT("Plain localized text")},
		{TEXT("RichTextBlock"), TEXT("/Script/UMG.RichTextBlock"), false, TEXT("Styled rich text")},
		{TEXT("Image"), TEXT("/Script/UMG.Image"), false, TEXT("Brush, texture, or material image")},
		{TEXT("ProgressBar"), TEXT("/Script/UMG.ProgressBar"), false, TEXT("Progress indicator")},
		{TEXT("Slider"), TEXT("/Script/UMG.Slider"), false, TEXT("Numeric slider")},
		{TEXT("CheckBox"), TEXT("/Script/UMG.CheckBox"), false, TEXT("Boolean or tri-state control")},
		{TEXT("EditableText"), TEXT("/Script/UMG.EditableText"), false, TEXT("Editable inline text")},
		{TEXT("EditableTextBox"), TEXT("/Script/UMG.EditableTextBox"), false, TEXT("Styled editable text field")},
		{TEXT("MultiLineEditableText"), TEXT("/Script/UMG.MultiLineEditableText"), false, TEXT("Multiline editable text")},
		{TEXT("MultiLineEditableTextBox"), TEXT("/Script/UMG.MultiLineEditableTextBox"), false, TEXT("Styled multiline text field")},
		{TEXT("ComboBoxString"), TEXT("/Script/UMG.ComboBoxString"), false, TEXT("String selection control")},
		{TEXT("SpinBox"), TEXT("/Script/UMG.SpinBox"), false, TEXT("Numeric entry control")},
		{TEXT("Spacer"), TEXT("/Script/UMG.Spacer"), false, TEXT("Fixed layout space")},
		{TEXT("Throbber"), TEXT("/Script/UMG.Throbber"), false, TEXT("Loading indicator")},
		{TEXT("CircularThrobber"), TEXT("/Script/UMG.CircularThrobber"), false, TEXT("Circular loading indicator")},
	};

	static const TSet<FString>& DeniedEditableProperties()
	{
		static const TSet<FString> Names = {
			TEXT("Navigation"), TEXT("ToolTipWidget"), TEXT("AccessibleWidgetData"),
			TEXT("WidgetGeneratedBy"), TEXT("Slot"), TEXT("Parent"), TEXT("Children"),
		};
		return Names;
	}

	static TSharedPtr<FJsonValue> JsonObjectValue(const TSharedRef<FJsonObject>& Object)
	{
		return MakeShared<FJsonValueObject>(Object);
	}

	static FString NormalizeObjectPath(const FString& InPath)
	{
		FString Path = InPath.TrimStartAndEnd();
		if (!Path.IsEmpty() && !Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPackageName::GetShortName(Path);
		}
		return Path;
	}

	static UWidgetBlueprint* LoadWidgetBlueprint(const FString& InPath)
	{
		return LoadObject<UWidgetBlueprint>(nullptr, *NormalizeObjectPath(InPath));
	}

	static bool SaveBlueprint(UWidgetBlueprint* Blueprint, FString& OutError)
	{
		if (!Blueprint || !Blueprint->GetOutermost())
		{
			OutError = TEXT("Widget Blueprint has no package to save");
			return false;
		}

		TArray<UPackage*> Packages;
		Packages.Add(Blueprint->GetOutermost());
		if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, false))
		{
			OutError = TEXT("SavePackages returned false");
			return false;
		}
		return true;
	}

	static bool TryGetVector2(const TSharedPtr<FJsonValue>& Value, FVector2D& Out)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
			if (Values.Num() == 2 && Values[0].IsValid() && Values[1].IsValid())
			{
				Out = FVector2D(Values[0]->AsNumber(), Values[1]->AsNumber());
				return true;
			}
		}
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			double X = 0.0;
			double Y = 0.0;
			if (Object.IsValid() && Object->TryGetNumberField(TEXT("x"), X) && Object->TryGetNumberField(TEXT("y"), Y))
			{
				Out = FVector2D(X, Y);
				return true;
			}
		}
		return false;
	}

	static bool TryGetMargin(const TSharedPtr<FJsonValue>& Value, FMargin& Out)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::Number)
		{
			Out = FMargin(Value->AsNumber());
			return true;
		}
		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
			if (Values.Num() == 2)
			{
				Out = FMargin(Values[0]->AsNumber(), Values[1]->AsNumber());
				return true;
			}
			if (Values.Num() == 4)
			{
				Out = FMargin(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber(), Values[3]->AsNumber());
				return true;
			}
		}
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			double Left = 0.0;
			double Top = 0.0;
			double Right = 0.0;
			double Bottom = 0.0;
			if (Object.IsValid()
				&& Object->TryGetNumberField(TEXT("left"), Left)
				&& Object->TryGetNumberField(TEXT("top"), Top)
				&& Object->TryGetNumberField(TEXT("right"), Right)
				&& Object->TryGetNumberField(TEXT("bottom"), Bottom))
			{
				Out = FMargin(Left, Top, Right, Bottom);
				return true;
			}
		}
		return false;
	}

	static FString SnakeToPascal(const FString& In)
	{
		TArray<FString> Parts;
		In.ParseIntoArray(Parts, TEXT("_"), true);
		FString Result;
		for (FString Part : Parts)
		{
			if (!Part.IsEmpty())
			{
				Part[0] = FChar::ToUpper(Part[0]);
				Result += Part;
			}
		}
		return Result;
	}

	static bool TryGetLinearColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& Out)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::String)
		{
			FString Hex = Value->AsString();
			Hex.RemoveFromStart(TEXT("#"));
			if (Hex.Len() != 6 && Hex.Len() != 8)
			{
				return false;
			}
			Out = FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
			return true;
		}
		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
			if (Values.Num() == 3 || Values.Num() == 4)
			{
				Out = FLinearColor(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber(), Values.Num() == 4 ? Values[3]->AsNumber() : 1.0);
				return true;
			}
		}
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			double R = 0.0;
			double G = 0.0;
			double B = 0.0;
			double A = 1.0;
			if (Object.IsValid()
				&& Object->TryGetNumberField(TEXT("r"), R)
				&& Object->TryGetNumberField(TEXT("g"), G)
				&& Object->TryGetNumberField(TEXT("b"), B))
			{
				Object->TryGetNumberField(TEXT("a"), A);
				Out = FLinearColor(R, G, B, A);
				return true;
			}
		}
		return false;
	}

	static FProperty* FindEditableProperty(UObject* Object, const FString& JsonName)
	{
		if (!Object)
		{
			return nullptr;
		}

		const FString Pascal = SnakeToPascal(JsonName);
		const FString BoolPascal = TEXT("b") + Pascal;
		const FString Alias = JsonName.Equals(TEXT("enabled"), ESearchCase::IgnoreCase) ? TEXT("bIsEnabled") : FString();
		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			const FString PropertyName = Property->GetName();
			if (!PropertyName.Equals(JsonName, ESearchCase::IgnoreCase)
				&& !PropertyName.Equals(Pascal, ESearchCase::IgnoreCase)
				&& !PropertyName.Equals(BoolPascal, ESearchCase::IgnoreCase)
				&& (Alias.IsEmpty() || !PropertyName.Equals(Alias, ESearchCase::IgnoreCase)))
			{
				continue;
			}
			if (!Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)
				|| Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
				|| DeniedEditableProperties().Contains(PropertyName)
				|| Property->IsA<FDelegateProperty>()
				|| Property->IsA<FMulticastDelegateProperty>())
			{
				return nullptr;
			}
			return Property;
		}
		return nullptr;
	}

	static bool ApplyEditableProperties(UObject* Object, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
	{
		if (!Properties.IsValid())
		{
			return true;
		}

		Object->Modify();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
		{
			if (Pair.Key.Equals(TEXT("is_variable"), ESearchCase::IgnoreCase))
			{
				UWidget* Widget = Cast<UWidget>(Object);
				if (!Widget || !Pair.Value.IsValid() || Pair.Value->Type != EJson::Boolean)
				{
					OutError = FString::Printf(TEXT("Property '%s' must be a boolean widget property"), *Pair.Key);
					return false;
				}
				Widget->bIsVariable = Pair.Value->AsBool();
				continue;
			}

			FProperty* Property = FindEditableProperty(Object, Pair.Key);
			if (!Property)
			{
				OutError = FString::Printf(TEXT("Property '%s' is not an editable property of %s"), *Pair.Key, *Object->GetClass()->GetName());
				return false;
			}

			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
				{
					FVector2D Vector;
					if (TryGetVector2(Pair.Value, Vector))
					{
						*static_cast<FVector2D*>(ValuePtr) = Vector;
						continue;
					}
				}
				if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
				{
					FLinearColor Color;
					if (TryGetLinearColor(Pair.Value, Color))
					{
						*static_cast<FLinearColor*>(ValuePtr) = Color;
						continue;
					}
				}
				if (StructProperty->Struct->GetFName() == TEXT("SlateColor"))
				{
					FLinearColor Color;
					if (TryGetLinearColor(Pair.Value, Color))
					{
						*static_cast<FSlateColor*>(ValuePtr) = FSlateColor(Color);
						continue;
					}
				}
				if (StructProperty->Struct->GetFName() == TEXT("Margin"))
				{
					FMargin Margin;
					if (TryGetMargin(Pair.Value, Margin))
					{
						*static_cast<FMargin*>(ValuePtr) = Margin;
						continue;
					}
				}
			}

			FText FailureReason;
			if (!FJsonObjectConverter::JsonValueToUProperty(Pair.Value, Property, ValuePtr, 0, 0, false, &FailureReason))
			{
				OutError = FString::Printf(TEXT("Could not set %s.%s: %s"), *Object->GetClass()->GetName(), *Property->GetName(), *FailureReason.ToString());
				return false;
			}
		}
		Object->PostEditChange();
		return true;
	}

	static bool TryHorizontalAlignment(const FString& In, EHorizontalAlignment& Out)
	{
		if (In.Equals(TEXT("left"), ESearchCase::IgnoreCase)) { Out = HAlign_Left; return true; }
		if (In.Equals(TEXT("center"), ESearchCase::IgnoreCase)) { Out = HAlign_Center; return true; }
		if (In.Equals(TEXT("right"), ESearchCase::IgnoreCase)) { Out = HAlign_Right; return true; }
		if (In.Equals(TEXT("fill"), ESearchCase::IgnoreCase)) { Out = HAlign_Fill; return true; }
		return false;
	}

	static bool TryVerticalAlignment(const FString& In, EVerticalAlignment& Out)
	{
		if (In.Equals(TEXT("top"), ESearchCase::IgnoreCase)) { Out = VAlign_Top; return true; }
		if (In.Equals(TEXT("center"), ESearchCase::IgnoreCase)) { Out = VAlign_Center; return true; }
		if (In.Equals(TEXT("bottom"), ESearchCase::IgnoreCase)) { Out = VAlign_Bottom; return true; }
		if (In.Equals(TEXT("fill"), ESearchCase::IgnoreCase)) { Out = VAlign_Fill; return true; }
		return false;
	}

	static bool ApplySlotProperties(UPanelSlot* Slot, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
	{
		if (!Properties.IsValid())
		{
			return true;
		}

		TSharedRef<FJsonObject> Generic = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
		{
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
			{
				if (Pair.Key.Equals(TEXT("anchors"), ESearchCase::IgnoreCase))
				{
					const TSharedPtr<FJsonObject> AnchorsObject = Pair.Value.IsValid() && Pair.Value->Type == EJson::Object ? Pair.Value->AsObject() : nullptr;
					FVector2D Minimum = FVector2D::ZeroVector;
					FVector2D Maximum = FVector2D::ZeroVector;
					if (!AnchorsObject.IsValid()
						|| !TryGetVector2(AnchorsObject->TryGetField(TEXT("min")), Minimum)
						|| !TryGetVector2(AnchorsObject->TryGetField(TEXT("max")), Maximum))
					{
						OutError = TEXT("Canvas anchors must be {min:[x,y], max:[x,y]}");
						return false;
					}
					CanvasSlot->SetAnchors(FAnchors(Minimum.X, Minimum.Y, Maximum.X, Maximum.Y));
					continue;
				}
				if (Pair.Key.Equals(TEXT("offsets"), ESearchCase::IgnoreCase))
				{
					FMargin Margin;
					if (!TryGetMargin(Pair.Value, Margin))
					{
						OutError = TEXT("Canvas offsets must be [left,top,right,bottom]");
						return false;
					}
					CanvasSlot->SetOffsets(Margin);
					continue;
				}
				if (Pair.Key.Equals(TEXT("alignment"), ESearchCase::IgnoreCase)
					|| Pair.Key.Equals(TEXT("position"), ESearchCase::IgnoreCase)
					|| Pair.Key.Equals(TEXT("size"), ESearchCase::IgnoreCase))
				{
					FVector2D Vector;
					if (!TryGetVector2(Pair.Value, Vector))
					{
						OutError = FString::Printf(TEXT("Canvas %s must be [x,y]"), *Pair.Key);
						return false;
					}
					if (Pair.Key.Equals(TEXT("alignment"), ESearchCase::IgnoreCase)) { CanvasSlot->SetAlignment(Vector); }
					else if (Pair.Key.Equals(TEXT("position"), ESearchCase::IgnoreCase)) { CanvasSlot->SetPosition(Vector); }
					else { CanvasSlot->SetSize(Vector); }
					continue;
				}
				if (Pair.Key.Equals(TEXT("auto_size"), ESearchCase::IgnoreCase))
				{
					CanvasSlot->SetAutoSize(Pair.Value->AsBool());
					continue;
				}
				if (Pair.Key.Equals(TEXT("z_order"), ESearchCase::IgnoreCase))
				{
					CanvasSlot->SetZOrder(static_cast<int32>(Pair.Value->AsNumber()));
					continue;
				}
			}

			if (Pair.Key.Equals(TEXT("padding"), ESearchCase::IgnoreCase))
			{
				FMargin Margin;
				if (!TryGetMargin(Pair.Value, Margin))
				{
					OutError = TEXT("Slot padding must be a number, [horizontal,vertical], or [left,top,right,bottom]");
					return false;
				}
				TSharedRef<FJsonObject> MarginObject = MakeShared<FJsonObject>();
				MarginObject->SetNumberField(TEXT("left"), Margin.Left);
				MarginObject->SetNumberField(TEXT("top"), Margin.Top);
				MarginObject->SetNumberField(TEXT("right"), Margin.Right);
				MarginObject->SetNumberField(TEXT("bottom"), Margin.Bottom);
				Generic->SetObjectField(TEXT("Padding"), MarginObject);
				continue;
			}

			if (Pair.Key.Equals(TEXT("horizontal_alignment"), ESearchCase::IgnoreCase))
			{
				EHorizontalAlignment Alignment;
				if (!TryHorizontalAlignment(Pair.Value->AsString(), Alignment))
				{
					OutError = TEXT("horizontal_alignment must be left, center, right, or fill");
					return false;
				}
				Generic->SetStringField(TEXT("HorizontalAlignment"), StaticEnum<EHorizontalAlignment>()->GetNameStringByValue(Alignment));
				continue;
			}

			if (Pair.Key.Equals(TEXT("vertical_alignment"), ESearchCase::IgnoreCase))
			{
				EVerticalAlignment Alignment;
				if (!TryVerticalAlignment(Pair.Value->AsString(), Alignment))
				{
					OutError = TEXT("vertical_alignment must be top, center, bottom, or fill");
					return false;
				}
				Generic->SetStringField(TEXT("VerticalAlignment"), StaticEnum<EVerticalAlignment>()->GetNameStringByValue(Alignment));
				continue;
			}

			if (Pair.Key.Equals(TEXT("size_rule"), ESearchCase::IgnoreCase))
			{
				const FString Rule = Pair.Value->AsString();
				FSlateChildSize ChildSize;
				ChildSize.SizeRule = Rule.Equals(TEXT("fill"), ESearchCase::IgnoreCase) ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic;
				double Value = 1.0;
				Properties->TryGetNumberField(TEXT("fill_value"), Value);
				ChildSize.Value = Value;
				if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Slot)) { HorizontalSlot->SetSize(ChildSize); }
				else if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Slot)) { VerticalSlot->SetSize(ChildSize); }
				else
				{
					OutError = TEXT("size_rule is only valid for HorizontalBox and VerticalBox slots");
					return false;
				}
				continue;
			}

			if (Pair.Key.Equals(TEXT("fill_value"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			Generic->SetField(SnakeToPascal(Pair.Key), Pair.Value);
		}

		return ApplyEditableProperties(Slot, Generic, OutError);
	}

	static UClass* ResolveWidgetClass(const FString& Type)
	{
		for (const FWidgetTypeDefinition& Definition : WidgetTypes)
		{
			if (Type.Equals(Definition.Name, ESearchCase::IgnoreCase)
				|| Type.Equals(Definition.ClassPath, ESearchCase::IgnoreCase))
			{
				return StaticLoadClass(UWidget::StaticClass(), nullptr, Definition.ClassPath);
			}
		}
		return nullptr;
	}

	struct FBuildContext
	{
		UWidgetTree* Tree = nullptr;
		TSet<FName> Names;
		int32 WidgetCount = 0;
	};

	static UWidget* BuildWidget(FBuildContext& Context, const TSharedPtr<FJsonObject>& Spec, int32 Depth, FString& OutError)
	{
		if (!Context.Tree || !Spec.IsValid())
		{
			OutError = TEXT("Widget spec must be an object");
			return nullptr;
		}
		if (Depth > MaxWidgetDepth || ++Context.WidgetCount > MaxWidgetCount)
		{
			OutError = FString::Printf(TEXT("Widget tree exceeds the limit of %d widgets or depth %d"), MaxWidgetCount, MaxWidgetDepth);
			return nullptr;
		}

		FString Type;
		FString NameString;
		if (!Spec->TryGetStringField(TEXT("type"), Type) || Type.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("Every widget must include a non-empty 'type'");
			return nullptr;
		}
		if (!Spec->TryGetStringField(TEXT("name"), NameString) || NameString.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("Every widget must include a non-empty 'name'");
			return nullptr;
		}

		const FName Name(*NameString);
		if (Context.Names.Contains(Name))
		{
			OutError = FString::Printf(TEXT("Widget name '%s' is duplicated"), *NameString);
			return nullptr;
		}
		Context.Names.Add(Name);

		UClass* WidgetClass = ResolveWidgetClass(Type);
		if (!WidgetClass)
		{
			OutError = FString::Printf(TEXT("Widget type '%s' is not in the native UMG allowlist"), *Type);
			return nullptr;
		}

		UWidget* Widget = Context.Tree->ConstructWidget<UWidget>(WidgetClass, Name);
		if (!Widget)
		{
			OutError = FString::Printf(TEXT("Could not construct widget '%s' as %s"), *NameString, *Type);
			return nullptr;
		}

		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if (Spec->TryGetObjectField(TEXT("properties"), Properties) && Properties && !ApplyEditableProperties(Widget, *Properties, OutError))
		{
			OutError = FString::Printf(TEXT("%s: %s"), *NameString, *OutError);
			return nullptr;
		}

		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (Spec->TryGetArrayField(TEXT("children"), Children) && Children && Children->Num() > 0)
		{
			UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
			if (!Panel)
			{
				OutError = FString::Printf(TEXT("Widget '%s' (%s) cannot contain children"), *NameString, *Type);
				return nullptr;
			}

			for (const TSharedPtr<FJsonValue>& ChildValue : *Children)
			{
				const TSharedPtr<FJsonObject> ChildSpec = ChildValue.IsValid() && ChildValue->Type == EJson::Object ? ChildValue->AsObject() : nullptr;
				UWidget* Child = BuildWidget(Context, ChildSpec, Depth + 1, OutError);
				if (!Child)
				{
					return nullptr;
				}

				UPanelSlot* Slot = Panel->AddChild(Child);
				if (!Slot)
				{
					OutError = FString::Printf(TEXT("Widget '%s' cannot accept child '%s'; single-child panels accept only one child"), *NameString, *Child->GetName());
					return nullptr;
				}

				const TSharedPtr<FJsonObject>* SlotProperties = nullptr;
				if (ChildSpec->TryGetObjectField(TEXT("slot"), SlotProperties) && SlotProperties && !ApplySlotProperties(Slot, *SlotProperties, OutError))
				{
					OutError = FString::Printf(TEXT("Slot for '%s': %s"), *Child->GetName(), *OutError);
					return nullptr;
				}
			}
		}

		return Widget;
	}

	static TSharedRef<FJsonObject> SerializeSlot(const UPanelSlot* Slot)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!Slot)
		{
			return Result;
		}
		Result->SetStringField(TEXT("class"), Slot->GetClass()->GetPathName());
		if (const UCanvasPanelSlot* Canvas = Cast<UCanvasPanelSlot>(Slot))
		{
			const FAnchors Anchors = Canvas->GetAnchors();
			const FMargin Offsets = Canvas->GetOffsets();
			const FVector2D Alignment = Canvas->GetAlignment();
			TSharedRef<FJsonObject> AnchorObject = MakeShared<FJsonObject>();
			AnchorObject->SetArrayField(TEXT("min"), {MakeShared<FJsonValueNumber>(Anchors.Minimum.X), MakeShared<FJsonValueNumber>(Anchors.Minimum.Y)});
			AnchorObject->SetArrayField(TEXT("max"), {MakeShared<FJsonValueNumber>(Anchors.Maximum.X), MakeShared<FJsonValueNumber>(Anchors.Maximum.Y)});
			Result->SetObjectField(TEXT("anchors"), AnchorObject);
			Result->SetArrayField(TEXT("offsets"), {MakeShared<FJsonValueNumber>(Offsets.Left), MakeShared<FJsonValueNumber>(Offsets.Top), MakeShared<FJsonValueNumber>(Offsets.Right), MakeShared<FJsonValueNumber>(Offsets.Bottom)});
			Result->SetArrayField(TEXT("alignment"), {MakeShared<FJsonValueNumber>(Alignment.X), MakeShared<FJsonValueNumber>(Alignment.Y)});
			Result->SetBoolField(TEXT("auto_size"), Canvas->GetAutoSize());
			Result->SetNumberField(TEXT("z_order"), Canvas->GetZOrder());
		}

		struct FSlotField
		{
			const TCHAR* PropertyName;
			const TCHAR* JsonName;
		};
		static const FSlotField Fields[] = {
			{TEXT("Padding"), TEXT("padding")},
			{TEXT("HorizontalAlignment"), TEXT("horizontal_alignment")},
			{TEXT("VerticalAlignment"), TEXT("vertical_alignment")},
			{TEXT("Size"), TEXT("size")},
			{TEXT("Row"), TEXT("row")},
			{TEXT("Column"), TEXT("column")},
			{TEXT("RowSpan"), TEXT("row_span")},
			{TEXT("ColumnSpan"), TEXT("column_span")},
			{TEXT("Layer"), TEXT("layer")},
			{TEXT("Nudge"), TEXT("nudge")},
			{TEXT("FillEmptySpace"), TEXT("fill_empty_space")},
			{TEXT("FillSpanWhenLessThan"), TEXT("fill_span_when_less_than")},
		};

		for (const FSlotField& Field : Fields)
		{
			FProperty* Property = FindFProperty<FProperty>(Slot->GetClass(), Field.PropertyName);
			if (!Property)
			{
				continue;
			}

			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Slot);
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct->GetFName() == TEXT("Margin"))
				{
					const FMargin& Margin = *static_cast<const FMargin*>(ValuePtr);
					Result->SetArrayField(Field.JsonName, {
						MakeShared<FJsonValueNumber>(Margin.Left), MakeShared<FJsonValueNumber>(Margin.Top),
						MakeShared<FJsonValueNumber>(Margin.Right), MakeShared<FJsonValueNumber>(Margin.Bottom),
					});
					continue;
				}
				if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
				{
					const FVector2D& Vector = *static_cast<const FVector2D*>(ValuePtr);
					Result->SetArrayField(Field.JsonName, {
						MakeShared<FJsonValueNumber>(Vector.X), MakeShared<FJsonValueNumber>(Vector.Y),
					});
					continue;
				}
				if (StructProperty->Struct->GetFName() == TEXT("SlateChildSize"))
				{
					const FSlateChildSize& ChildSize = *static_cast<const FSlateChildSize*>(ValuePtr);
					Result->SetStringField(TEXT("size_rule"), ChildSize.SizeRule == ESlateSizeRule::Fill ? TEXT("fill") : TEXT("auto"));
					Result->SetNumberField(TEXT("fill_value"), ChildSize.Value);
					continue;
				}
			}

			TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(Property, ValuePtr);
			if (Value.IsValid() && Value->Type == EJson::String
				&& (FCString::Strcmp(Field.JsonName, TEXT("horizontal_alignment")) == 0
					|| FCString::Strcmp(Field.JsonName, TEXT("vertical_alignment")) == 0))
			{
				FString Alignment = Value->AsString();
				Alignment.RemoveFromStart(TEXT("HAlign_"));
				Alignment.RemoveFromStart(TEXT("VAlign_"));
				Alignment.ToLowerInline();
				Value = MakeShared<FJsonValueString>(Alignment);
			}
			if (Value.IsValid())
			{
				Result->SetField(Field.JsonName, Value);
			}
		}
		return Result;
	}

	static TSharedRef<FJsonObject> SerializeWidget(const UWidget* Widget)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!Widget)
		{
			return Result;
		}
		Result->SetStringField(TEXT("name"), Widget->GetName());
		Result->SetStringField(TEXT("type"), Widget->GetClass()->GetName());
		Result->SetStringField(TEXT("class"), Widget->GetClass()->GetPathName());
		Result->SetBoolField(TEXT("is_variable"), Widget->bIsVariable != 0);
		Result->SetStringField(TEXT("visibility"), StaticEnum<ESlateVisibility>()->GetNameStringByValue(static_cast<int64>(Widget->GetVisibility())));
		Result->SetBoolField(TEXT("enabled"), Widget->GetIsEnabled());
		Result->SetNumberField(TEXT("render_opacity"), Widget->GetRenderOpacity());
		if (Widget->Slot)
		{
			Result->SetObjectField(TEXT("slot"), SerializeSlot(Widget->Slot));
		}

		TArray<TSharedPtr<FJsonValue>> Children;
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (const UWidget* Child = Panel->GetChildAt(Index))
				{
					Children.Add(JsonObjectValue(SerializeWidget(Child)));
				}
			}
		}
		Result->SetArrayField(TEXT("children"), Children);
		return Result;
	}

	static TSharedRef<FJsonObject> MakeDesignerSnapshot(UWidgetBlueprint* Blueprint)
	{
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("blueprint"), Blueprint ? Blueprint->GetPathName() : FString());
		Data->SetBoolField(TEXT("python_used"), false);
		const UWidgetTree* Tree = Blueprint ? Blueprint->WidgetTree : nullptr;
		Data->SetBoolField(TEXT("has_root"), Tree && Tree->RootWidget);
		if (Tree && Tree->RootWidget)
		{
			Data->SetObjectField(TEXT("root"), SerializeWidget(Tree->RootWidget));
			int32 Count = 0;
			Tree->ForEachWidget([&Count](UWidget*) { ++Count; });
			Data->SetNumberField(TEXT("widget_count"), Count);
		}
		else
		{
			Data->SetNumberField(TEXT("widget_count"), 0);
		}
		return Data;
	}
}

FAutomationResult FUMGDesignerService::DescribeSchema() const
{
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("api"), TEXT("native_cpp_umg_designer"));
	Data->SetBoolField(TEXT("requires_python"), false);
	Data->SetNumberField(TEXT("max_widget_count"), MaxWidgetCount);
	Data->SetNumberField(TEXT("max_depth"), MaxWidgetDepth);

	TArray<TSharedPtr<FJsonValue>> Types;
	for (const FWidgetTypeDefinition& Definition : WidgetTypes)
	{
		TSharedRef<FJsonObject> Type = MakeShared<FJsonObject>();
		Type->SetStringField(TEXT("type"), Definition.Name);
		Type->SetStringField(TEXT("class"), Definition.ClassPath);
		Type->SetBoolField(TEXT("panel"), Definition.bPanel);
		Type->SetStringField(TEXT("purpose"), Definition.Purpose);
		Types.Add(JsonObjectValue(Type));
	}
	Data->SetArrayField(TEXT("widget_types"), Types);
	Data->SetArrayField(TEXT("common_properties"), {
		MakeShared<FJsonValueString>(TEXT("visibility")), MakeShared<FJsonValueString>(TEXT("enabled")),
		MakeShared<FJsonValueString>(TEXT("render_opacity")), MakeShared<FJsonValueString>(TEXT("render_transform")),
		MakeShared<FJsonValueString>(TEXT("render_transform_pivot")), MakeShared<FJsonValueString>(TEXT("tool_tip_text")),
		MakeShared<FJsonValueString>(TEXT("is_variable")),
	});
	Data->SetArrayField(TEXT("slot_properties"), {
		MakeShared<FJsonValueString>(TEXT("anchors")), MakeShared<FJsonValueString>(TEXT("offsets")),
		MakeShared<FJsonValueString>(TEXT("alignment")), MakeShared<FJsonValueString>(TEXT("position")),
		MakeShared<FJsonValueString>(TEXT("size")), MakeShared<FJsonValueString>(TEXT("auto_size")),
		MakeShared<FJsonValueString>(TEXT("z_order")), MakeShared<FJsonValueString>(TEXT("padding")),
		MakeShared<FJsonValueString>(TEXT("horizontal_alignment")), MakeShared<FJsonValueString>(TEXT("vertical_alignment")),
		MakeShared<FJsonValueString>(TEXT("size_rule")), MakeShared<FJsonValueString>(TEXT("fill_value")),
		MakeShared<FJsonValueString>(TEXT("row")), MakeShared<FJsonValueString>(TEXT("column")),
		MakeShared<FJsonValueString>(TEXT("row_span")), MakeShared<FJsonValueString>(TEXT("column_span")),
		MakeShared<FJsonValueString>(TEXT("layer")), MakeShared<FJsonValueString>(TEXT("nudge")),
	});
	Data->SetStringField(TEXT("property_contract"), TEXT("Widget properties use snake_case aliases of editable UMG properties; delegates, navigation objects, ownership fields, and transient properties are rejected."));
	return FAutomationResult::Ok(JsonObjectValue(Data));
}

FAutomationResult FUMGDesignerService::CreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Request) const
{
	if (!Request.IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_json"), TEXT("Request body is required"), 400);
	}

	FString PackagePath;
	FString Name;
	Request->TryGetStringField(TEXT("path"), PackagePath);
	Request->TryGetStringField(TEXT("name"), Name);
	PackagePath.TrimStartAndEndInline();
	Name.TrimStartAndEndInline();
	if (PackagePath.IsEmpty() || Name.IsEmpty())
	{
		return FAutomationResult::Error(TEXT("missing_fields"), TEXT("Body must include 'path' and 'name'"), 400);
	}

	const FString PackageName = PackagePath / Name;
	if (!PackagePath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackageName))
	{
		return FAutomationResult::Error(TEXT("invalid_path"), TEXT("Widget Blueprint path must be a valid /Game/... package path"), 400);
	}
	if (LoadWidgetBlueprint(PackageName))
	{
		return FAutomationResult::Error(TEXT("already_exists"), FString::Printf(TEXT("Asset already exists: %s"), *PackageName), 409);
	}

	FString ParentPath = TEXT("/Script/UMG.UserWidget");
	Request->TryGetStringField(TEXT("parent"), ParentPath);
	UClass* ParentClass = StaticLoadClass(UUserWidget::StaticClass(), nullptr, *ParentPath);
	if (!ParentClass || !ParentClass->IsChildOf(UUserWidget::StaticClass()) || ParentClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return FAutomationResult::Error(TEXT("invalid_parent"), TEXT("parent must resolve to a non-abstract UUserWidget class"), 400);
	}

	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ParentClass;
	UObject* Created = FAssetToolsModule::GetModule().Get().CreateAsset(Name, PackagePath, UWidgetBlueprint::StaticClass(), Factory);
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Created);
	if (!Blueprint)
	{
		return FAutomationResult::Error(TEXT("create_failed"), TEXT("Unreal failed to create the Widget Blueprint"), 500);
	}

	const BAT::BlueprintCompileDiagnostics::FDiagnostics Diagnostics = BAT::BlueprintCompileDiagnostics::Compile(Blueprint);
	bool bSave = true;
	Request->TryGetBoolField(TEXT("save"), bSave);
	bool bSaved = false;
	FString SaveError;
	if (Diagnostics.bCompileSucceeded && bSave)
	{
		bSaved = SaveBlueprint(Blueprint, SaveError);
	}

	TSharedRef<FJsonObject> Data = MakeDesignerSnapshot(Blueprint);
	Data->SetObjectField(TEXT("compileDiagnostics"), BAT::BlueprintCompileDiagnostics::MakeDiagnosticsObject(Diagnostics));
	Data->SetBoolField(TEXT("saved"), bSaved);
	if (!SaveError.IsEmpty())
	{
		Data->SetStringField(TEXT("save_error"), SaveError);
	}
	return FAutomationResult::Ok(JsonObjectValue(Data), 201);
}

FAutomationResult FUMGDesignerService::ReadDesigner(const TSharedPtr<FJsonObject>& Request) const
{
	FString Path;
	if (!Request.IsValid() || !Request->TryGetStringField(TEXT("blueprint"), Path) || Path.TrimStartAndEnd().IsEmpty())
	{
		return FAutomationResult::Error(TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'"), 400);
	}
	UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(Path);
	if (!Blueprint)
	{
		return FAutomationResult::Error(TEXT("not_found"), TEXT("Widget Blueprint not found"), 404);
	}
	return FAutomationResult::Ok(JsonObjectValue(MakeDesignerSnapshot(Blueprint)));
}

FAutomationResult FUMGDesignerService::ApplyDesigner(const TSharedPtr<FJsonObject>& Request) const
{
	FString Path;
	if (!Request.IsValid() || !Request->TryGetStringField(TEXT("blueprint"), Path) || Path.TrimStartAndEnd().IsEmpty())
	{
		return FAutomationResult::Error(TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'"), 400);
	}
	UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(Path);
	if (!Blueprint)
	{
		return FAutomationResult::Error(TEXT("not_found"), TEXT("Widget Blueprint not found"), 404);
	}
	return ApplyDesignerToBlueprint(Blueprint, Request);
}

FAutomationResult FUMGDesignerService::ApplyDesignerToBlueprint(UWidgetBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Request) const
{
	if (!Blueprint || !Blueprint->WidgetTree || !Request.IsValid())
	{
		return FAutomationResult::Error(TEXT("invalid_widget_blueprint"), TEXT("A valid Widget Blueprint and request are required"), 400);
	}

	const TSharedPtr<FJsonObject>* RootSpec = nullptr;
	if (!Request->TryGetObjectField(TEXT("root"), RootSpec) || !RootSpec || !RootSpec->IsValid())
	{
		return FAutomationResult::Error(TEXT("missing_root"), TEXT("Body must include a declarative 'root' widget object"), 400);
	}

	// Build into a fresh WidgetTree. Besides keeping validation atomic, using a
	// distinct outer preserves exact widget names when the same declarative
	// layout is applied repeatedly (no automatic _1 suffixes).
	UWidgetTree* CandidateTree = NewObject<UWidgetTree>(Blueprint, NAME_None, RF_Transactional);
	FBuildContext BuildContext;
	BuildContext.Tree = CandidateTree;
	FString BuildError;
	UWidget* NewRoot = BuildWidget(BuildContext, *RootSpec, 0, BuildError);
	if (!NewRoot)
	{
		return FAutomationResult::Error(TEXT("invalid_widget_tree"), BuildError, 422);
	}
	CandidateTree->RootWidget = NewRoot;

	bool bCompile = true;
	bool bSave = true;
	Request->TryGetBoolField(TEXT("compile"), bCompile);
	Request->TryGetBoolField(TEXT("save"), bSave);
	if (bSave)
	{
		bCompile = true;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("BAT: Apply UMG Designer Layout")));
	Blueprint->Modify();
	UWidgetTree* OldTree = Blueprint->WidgetTree;
	OldTree->Modify();
	CandidateTree->Modify();
	Blueprint->WidgetTree = CandidateTree;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	BAT::BlueprintCompileDiagnostics::FDiagnostics Diagnostics;
	if (bCompile)
	{
		Diagnostics = BAT::BlueprintCompileDiagnostics::Compile(Blueprint);
		if (!Diagnostics.bCompileSucceeded)
		{
			Blueprint->WidgetTree = OldTree;
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			BAT::BlueprintCompileDiagnostics::Compile(Blueprint);

			TSharedRef<FJsonObject> ErrorData = MakeShared<FJsonObject>();
			ErrorData->SetObjectField(TEXT("compileDiagnostics"), BAT::BlueprintCompileDiagnostics::MakeDiagnosticsObject(Diagnostics));
			ErrorData->SetBoolField(TEXT("rolled_back"), true);
			return FAutomationResult::ErrorWithData(TEXT("compile_failed"), TEXT("Widget Blueprint compilation failed; the previous Designer root was restored"), 422, JsonObjectValue(ErrorData));
		}
	}
	else
	{
		Diagnostics.CompileStatus = TEXT("not_requested");
	}

	bool bSaved = false;
	if (bSave)
	{
		FString SaveError;
		if (!SaveBlueprint(Blueprint, SaveError))
		{
			Blueprint->WidgetTree = OldTree;
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			BAT::BlueprintCompileDiagnostics::Compile(Blueprint);
			return FAutomationResult::Error(TEXT("save_failed"), SaveError + TEXT("; the previous Designer root was restored"), 500);
		}
		bSaved = true;
	}

	TSharedRef<FJsonObject> Data = MakeDesignerSnapshot(Blueprint);
	Data->SetObjectField(TEXT("compileDiagnostics"), BAT::BlueprintCompileDiagnostics::MakeDiagnosticsObject(Diagnostics));
	Data->SetBoolField(TEXT("saved"), bSaved);
	Data->SetBoolField(TEXT("replaced_root"), true);
	return FAutomationResult::Ok(JsonObjectValue(Data));
}
