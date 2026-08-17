// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#pragma once
#include "Engine/DeveloperSettings.h"
#include "Engine/StaticMesh.h"
#include "LaneShape.h"
#include "LaneMarkStyle.h"
#include "RoadActor.h"
#include "RoadPreset.h"
#include "RoadBuilderSettings.generated.h"

UENUM(BlueprintType)
enum class ERoadTrafficHandedness : uint8
{
	RightHandTraffic	UMETA(DisplayName = "Right-Hand Traffic"),
	LeftHandTraffic		UMETA(DisplayName = "Left-Hand Traffic"),
};

UCLASS(config = RoadBuilder)
class ROADBUILDER_API USettings_Base : public UObject
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif
};

UCLASS()
class ROADBUILDER_API USettings_Global : public USettings_Base
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(config, EditAnywhere, Category = Traffic, meta = (DisplayName = "Project Traffic Handedness"))
	ERoadTrafficHandedness DefaultTrafficHandedness = ERoadTrafficHandedness::RightHandTraffic;

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = Traffic, meta = (DisplayName = "Apply Active RoadScene"))
	void ApplyActiveRoadSceneTrafficHandedness();

	UFUNCTION(CallInEditor, Category = Traffic, meta = (DisplayName = "Apply All Loaded RoadScenes"))
	void ApplyAllLoadedRoadScenesTrafficHandedness();
#endif

	UPROPERTY(config, EditAnywhere, Category = Build)
	TSoftObjectPtr<ULaneShape> DefaultDrivingShape;

	UPROPERTY(config, EditAnywhere, Category = Build)
	TSoftObjectPtr<ULaneShape> DefaultMedianShape;

	UPROPERTY(config, EditAnywhere, Category = Build)
	TSoftObjectPtr<ULaneShape> DefaultSidewalkShape;

	UPROPERTY(config, EditAnywhere, Category = Build)
	TSoftObjectPtr<ULaneMarkStyle> DefaultDashStyle;

	UPROPERTY(config, EditAnywhere, Category = Build)
	TSoftObjectPtr<ULaneMarkStyle> DefaultSolidStyle;

	UPROPERTY(config, EditAnywhere, Category = Ground)
	TSoftObjectPtr<UMaterialInterface> DefaultGroundMaterial;

	UPROPERTY(config, EditAnywhere, Category = Marking)
	TSoftObjectPtr<UPolygonMarkStyle> DefaultGoreMarking;

	UPROPERTY(config, EditAnywhere, Category = Build)
	double UVScale = 0.001;

	UPROPERTY(config, EditAnywhere, Category = Build)
	uint32 BuildJunctions : 1;

	UPROPERTY(config, EditAnywhere, Category = Build)
	uint32 BuildProps : 1;

	UPROPERTY(config, EditAnywhere, Category = MassGraph)
	uint32 BuildMassGraph : 1;

	UPROPERTY(config, EditAnywhere, Category = MassGraph, meta = (EditCondition = "BuildMassGraph"))
	TSoftObjectPtr<UStaticMesh> NoTurnSignMesh;

	UPROPERTY(config, EditAnywhere, Category = TrafficControl)
	uint32 AutoGenerateTrafficControl : 1;

	UPROPERTY(config, EditAnywhere, Category = TrafficControl)
	uint32 AutoGenerateTurnArrows : 1;

	UPROPERTY(config, EditAnywhere, Category = TrafficControl, meta = (EditCondition = "AutoGenerateTurnArrows"))
	TSoftObjectPtr<UStaticMesh> TurnArrowMesh;

	UPROPERTY(config, EditAnywhere, Category = TrafficControl, meta = (EditCondition = "AutoGenerateTrafficControl"))
	TSoftObjectPtr<UStaticMesh> TrafficLightMesh;

	UPROPERTY(config, EditAnywhere, Category = TrafficControl, meta = (EditCondition = "AutoGenerateTrafficControl"))
	TSoftObjectPtr<UStaticMesh> StopSignMesh;

	UPROPERTY(config, EditAnywhere, Category = TrafficControl, meta = (EditCondition = "AutoGenerateTrafficControl"))
	TSoftObjectPtr<UStaticMesh> YieldSignMesh;

	UPROPERTY(config, EditAnywhere, Category = Debug)
	uint32 DisplayGateRadianPoints : 1;
};

UCLASS()
class ROADBUILDER_API USettings_File : public USettings_Base
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = Export)
	void Xodr();
#endif
};

UCLASS()
class ROADBUILDER_API USettings_RoadPlan : public USettings_Base
{
	GENERATED_BODY()
public:
	USettings_RoadPlan(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = Styles)
	void Apply();

	UFUNCTION(CallInEditor, Category = Styles)
	void Create();
#endif
	UPROPERTY(config, EditAnywhere, Category = Road)
	double BaseHeight = 0;

	UPROPERTY(config, EditAnywhere, Category = "Road (Simple)")
	TSoftObjectPtr<URoadPreset> Preset;

	UPROPERTY(config, EditAnywhere, Category = "Road (Advanced)")
	TSoftObjectPtr<URoadStyle> Style;
};

UCLASS()
class ROADBUILDER_API USettings_RoadSplit : public USettings_Base
{
public:
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, Category = Split)
	double JunctionSize = 3200;

	UPROPERTY(config, EditAnywhere, Category = Split)
	double CornerRadius = 50;

	UPROPERTY(config, EditAnywhere, Category = Split)
	double ShoulderWidth = 50;
};

UCLASS()
class ROADBUILDER_API USettings_OSM : public USettings_Base
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(config, EditAnywhere, Category = OSM)
	double Resample = 1000;

	UPROPERTY(config, EditAnywhere, Category = OSM)
	double LayerHeight = 1000;

	UPROPERTY(config, EditAnywhere, Category = OSM)
	uint32 ConnectRoads : 1;

	UPROPERTY(config, EditAnywhere, Category = OSM)
	TSoftObjectPtr<URoadStyle> RoadStyle;
};

UCLASS()
class ROADBUILDER_API USettings_SVGImport : public USettings_Base
{
	GENERATED_BODY()
public:
	UPROPERTY(config, EditAnywhere, Category = SVG)
	TSoftObjectPtr<UMaterialInterface> Material;

	UPROPERTY(config, EditAnywhere, Category = SVG)
	bool ResetOrigin = false;

	UPROPERTY(config, EditAnywhere, Category = SVG)
	FVector2D Anchor = FVector2D::ZeroVector;

	UPROPERTY(config, EditAnywhere, Category = SVG)
	FVector2D Scale = FVector2D::UnitVector;

	UPROPERTY(config, EditAnywhere, Category = SVG)
	double Rotation = 0;
};
