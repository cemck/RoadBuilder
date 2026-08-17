// Copyright 2024. All Rights Reserved.

#include "RoadPreset.h"
#include "RoadBuilderSettings.h"

URoadPreset::URoadPreset()
{
	switch (RoadType)
	{
	case ERoadPresetType::Highway:
	case ERoadPresetType::Elevated:
		bHasSidewalks = false;
		bHasShoulder = true;
		break;
	case ERoadPresetType::Ramp:
		bHasSidewalks = false;
		LanesPerSide = 1;
		break;
	default:
		break;
	}
}

ULaneShape* URoadPreset::CreateDrivingShape()
{
	ULaneShape* Shape = NewObject<ULaneShape>(this);
	FLaneCrossSection& Section = Shape->CrossSections.AddDefaulted_GetRef();
	Section.Material = RoadMaterial;
	Section.Alignment = ELaneAlignment::Up;
	Section.UVScale = FVector2D(1.0, 1.0);
	Section.Points.Add(FVector2D(0, 0));
	Section.Points.Add(FVector2D(1, 0));
	Section.ClampZ = 0;
	return Shape;
}

ULaneShape* URoadPreset::CreateSidewalkShape()
{
	ULaneShape* Shape = NewObject<ULaneShape>(this);

	// Top surface
	FLaneCrossSection& Top = Shape->CrossSections.AddDefaulted_GetRef();
	Top.Material = SidewalkMaterial ? SidewalkMaterial : RoadMaterial;
	Top.Alignment = ELaneAlignment::Up;
	Top.UVScale = FVector2D(1.0, 1.0);
	Top.Points.Add(FVector2D(0.0, 0.15));
	Top.Points.Add(FVector2D(1.0, 0.15));
	Top.ClampZ = 0;

	// Curb face
	FLaneCrossSection& Curb = Shape->CrossSections.AddDefaulted_GetRef();
	Curb.Material = CurbMaterial ? CurbMaterial : (SidewalkMaterial ? SidewalkMaterial : RoadMaterial);
	Curb.Alignment = ELaneAlignment::Up;
	Curb.UVScale = FVector2D(1.0, 1.0);
	Curb.Points.Add(FVector2D(0.0, 0.0));
	Curb.Points.Add(FVector2D(0.0, 0.15));
	Curb.ClampZ = 0;

	return Shape;
}

ULaneShape* URoadPreset::CreateShoulderShape()
{
	ULaneShape* Shape = NewObject<ULaneShape>(this);
	FLaneCrossSection& Section = Shape->CrossSections.AddDefaulted_GetRef();
	Section.Material = RoadMaterial;
	Section.Alignment = ELaneAlignment::Up;
	Section.UVScale = FVector2D(1.0, 1.0);
	Section.Points.Add(FVector2D(0, 0));
	Section.Points.Add(FVector2D(1, 0));
	Section.ClampZ = 0;
	return Shape;
}

ULaneShape* URoadPreset::CreateMedianShape()
{
	// Try loading the built-in median shape first
	ULaneShape* Shape = LoadObject<ULaneShape>(nullptr, TEXT("/RoadBuilder/LaneShapes/Median.Median"));
	if (!Shape)
	{
		Shape = NewObject<ULaneShape>(this);
		FLaneCrossSection& Section = Shape->CrossSections.AddDefaulted_GetRef();
		Section.Material = RoadMaterial;
		Section.Alignment = ELaneAlignment::Up;
		Section.UVScale = FVector2D(1.0, 1.0);
		Section.Points.Add(FVector2D(0, 0.05));
		Section.Points.Add(FVector2D(1, 0.05));
		Section.ClampZ = 0;
	}
	return Shape;
}

ULaneMarkStyle* URoadPreset::CreateLaneMarkStyle(ELineColor Color, ELaneMarkType Type)
{
	// Try to find a matching built-in style
	FString ColorStr = (Color == ELineColor::Yellow) ? TEXT("Yellow") : TEXT("White");
	FString TypeStr;
	switch (Type)
	{
	case ELaneMarkType::Dash: TypeStr = TEXT("Dash"); break;
	case ELaneMarkType::Solid: TypeStr = TEXT("Solid"); break;
	case ELaneMarkType::SolidSolid: TypeStr = TEXT("SolidSolid"); break;
	case ELaneMarkType::DashDash: TypeStr = TEXT("DashDash"); break;
	case ELaneMarkType::DashSolid: TypeStr = TEXT("DashSolid"); break;
	case ELaneMarkType::SolidDash: TypeStr = TEXT("SolidDash"); break;
	}
	FString Path = FString::Printf(TEXT("/RoadBuilder/MarkStyles/LaneMark/%s%s.%s%s"), *ColorStr, *TypeStr, *ColorStr, *TypeStr);
	ULaneMarkStyle* Style = LoadObject<ULaneMarkStyle>(nullptr, *Path);
	if (Style)
		return Style;

	// Create one if built-in not found
	Style = NewObject<ULaneMarkStyle>(this);
	Style->MarkType = Type;
	Style->Material = (Color == ELineColor::Yellow) ? YellowLineMaterial : WhiteLineMaterial;
	Style->Width = 12.5;
	Style->Separation = 12.5;
	Style->DashLength = 150.0;
	Style->DashSpacing = 150.0;
	return Style;
}

URoadProps* URoadPreset::CreateStreetLightProps()
{
	if (!StreetLightMesh)
		return nullptr;
	URoadProps* Props = NewObject<URoadProps>(this);
	FRoadProp& Prop = Props->Props.AddDefaulted_GetRef();
	Prop.Assets.Add(StreetLightMesh);
	Prop.Offset = FVector(0, 50, 0);
	Prop.Scale = FVector(1, 1, 1);
	Prop.Rotation = FRotator::ZeroRotator;
	Prop.Spacing = StreetLightSpacing;
	Prop.Start = 0;
	Prop.End = 1;
	Prop.Fill = 0;
	Prop.Select = 1;
	Prop.Base = 0;
	Prop.Of = 1;
	Prop.RandomOffset = FVector::ZeroVector;
	Prop.RandomScale = FVector::ZeroVector;
	Prop.RandomRotation = FRotator::ZeroRotator;
	return Props;
}

URoadStyle* URoadPreset::GenerateRoadStyle()
{
	URoadStyle* Style = NewObject<URoadStyle>(this);

	ULaneShape* DrivingShape = CreateDrivingShape();
	ULaneShape* SidewalkShape = bHasSidewalks ? CreateSidewalkShape() : nullptr;
	ULaneShape* ShoulderShape = bHasShoulder ? CreateShoulderShape() : nullptr;
	ULaneShape* MedianShape = bHasMedian ? CreateMedianShape() : nullptr;

	ULaneMarkStyle* DashMark = CreateLaneMarkStyle(LaneLineColor, ELaneMarkType::Dash);
	ULaneMarkStyle* SolidMark = CreateLaneMarkStyle(LaneLineColor, ELaneMarkType::Solid);
	ULaneMarkStyle* CenterMark = CreateLaneMarkStyle(CenterLineColor, ELaneMarkType::SolidSolid);

	URoadProps* LightProps = (bHasStreetLights) ? CreateStreetLightProps() : nullptr;

	Style->BaseCurveMark = CenterMark;
	Style->BaseCurveProps = nullptr;
	Style->bHasGround = bHasGround;

	// Build the physical right-side lanes from center to edge. Traffic direction
	// is resolved by the owning RoadScene's handedness setting.
	for (int32 i = 0; i < LanesPerSide; i++)
	{
		FRoadLaneStyle& Lane = Style->RightLanes.AddDefaulted_GetRef();
		Lane.Width = LaneWidth;
		Lane.LaneType = ELaneType::Driving;
		Lane.LaneShape = DrivingShape;
		Lane.LaneMarking = (i < LanesPerSide - 1) ? DashMark : SolidMark;
		Lane.Props = nullptr;
	}

	// Add shoulder on right side
	if (bHasShoulder && ShoulderShape)
	{
		FRoadLaneStyle& Shoulder = Style->RightLanes.AddDefaulted_GetRef();
		Shoulder.Width = ShoulderWidth;
		Shoulder.LaneType = ELaneType::Shoulder;
		Shoulder.LaneShape = ShoulderShape;
		Shoulder.LaneMarking = nullptr;
		Shoulder.Props = nullptr;
	}

	// Add sidewalk on right side
	if (bHasSidewalks && SidewalkShape)
	{
		FRoadLaneStyle& Sidewalk = Style->RightLanes.AddDefaulted_GetRef();
		Sidewalk.Width = SidewalkWidth;
		Sidewalk.LaneType = ELaneType::Sidewalk;
		Sidewalk.LaneShape = SidewalkShape;
		Sidewalk.LaneMarking = nullptr;
		Sidewalk.Props = LightProps;
	}

	// Build the physical left-side lanes from center to edge (mirrored).
	for (int32 i = 0; i < LanesPerSide; i++)
	{
		FRoadLaneStyle& Lane = Style->LeftLanes.AddDefaulted_GetRef();
		Lane.Width = LaneWidth;
		Lane.LaneType = ELaneType::Driving;
		Lane.LaneShape = DrivingShape;
		Lane.LaneMarking = (i < LanesPerSide - 1) ? DashMark : SolidMark;
		Lane.Props = nullptr;
	}

	// Add shoulder on left side
	if (bHasShoulder && ShoulderShape)
	{
		FRoadLaneStyle& Shoulder = Style->LeftLanes.AddDefaulted_GetRef();
		Shoulder.Width = ShoulderWidth;
		Shoulder.LaneType = ELaneType::Shoulder;
		Shoulder.LaneShape = ShoulderShape;
		Shoulder.LaneMarking = nullptr;
		Shoulder.Props = nullptr;
	}

	// Add sidewalk on left side
	if (bHasSidewalks && SidewalkShape)
	{
		FRoadLaneStyle& Sidewalk = Style->LeftLanes.AddDefaulted_GetRef();
		Sidewalk.Width = SidewalkWidth;
		Sidewalk.LaneType = ELaneType::Sidewalk;
		Sidewalk.LaneShape = SidewalkShape;
		Sidewalk.LaneMarking = nullptr;
		Sidewalk.Props = nullptr;
	}

	// Insert median at center if needed
	if (bHasMedian && MedianShape)
	{
		FRoadLaneStyle Median;
		Median.Width = MedianWidth;
		Median.LaneType = ELaneType::Median;
		Median.LaneShape = MedianShape;
		Median.LaneMarking = SolidMark;
		Median.Props = nullptr;
		Style->RightLanes.Insert(Median, 0);
	}

	CachedStyle = Style;
	return Style;
}

#if WITH_EDITOR
void URoadPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CachedStyle = nullptr;
}
#endif
