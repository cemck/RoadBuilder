// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#include "RoadBuilderSettings.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/SoftObjectPath.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#include "RoadBuilderEditor/Public/RoadEdMode.h"
#include "EngineUtils.h"
void USettings_Base::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.MemberProperty)
		SaveConfig();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

USettings_Global::USettings_Global(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	DefaultDrivingShape = TSoftObjectPtr<ULaneShape>(FSoftObjectPath(TEXT("/RoadBuilder/LaneShapes/Driving.Driving")));
	DefaultSidewalkShape = TSoftObjectPtr<ULaneShape>(FSoftObjectPath(TEXT("/RoadBuilder/LaneShapes/Sidewalk.Sidewalk")));
	DefaultMedianShape = TSoftObjectPtr<ULaneShape>(FSoftObjectPath(TEXT("/RoadBuilder/LaneShapes/Median.Median")));
	DefaultDashStyle = TSoftObjectPtr<ULaneMarkStyle>(FSoftObjectPath(TEXT("/RoadBuilder/MarkStyles/LaneMark/WhiteDash.WhiteDash")));
	DefaultSolidStyle = TSoftObjectPtr<ULaneMarkStyle>(FSoftObjectPath(TEXT("/RoadBuilder/MarkStyles/LaneMark/WhiteSolid.WhiteSolid")));
	DefaultGroundMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial")));
	DefaultGoreMarking = TSoftObjectPtr<UPolygonMarkStyle>(FSoftObjectPath(TEXT("/RoadBuilder/MarkStyles/PolygonMark/ChevronRegion.ChevronRegion")));
	BuildJunctions = 1;
	BuildProps = 1;
	BuildMassGraph = 0;
	AutoGenerateTrafficControl = 0;
	AutoGenerateTurnArrows = 0;
	DisplayGateRadianPoints = 0;
}

USettings_OSM::USettings_OSM(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ConnectRoads = 1;
}

USettings_RoadPlan::USettings_RoadPlan(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	Style = TSoftObjectPtr<URoadStyle>(FSoftObjectPath(TEXT("/RoadBuilder/RoadStyles/Street-Main-8-Lanes.Street-Main-8-Lanes")));
}

#if WITH_EDITOR
void USettings_Global::ApplyActiveRoadSceneTrafficHandedness()
{
	if (FEdModeRoad* RoadMode = FEdModeRoad::Get())
	{
		if (ARoadScene* Scene = RoadMode->Scene)
		{
			const FScopedTransaction Transaction(FText::FromString(TEXT("Apply RoadScene Traffic Handedness")));
			Scene->Modify();
			Scene->ApplyTrafficHandedness();
		}
	}
}

void USettings_Global::ApplyAllLoadedRoadScenesTrafficHandedness()
{
	FEdModeRoad* RoadMode = FEdModeRoad::Get();
	ARoadScene* ActiveScene = RoadMode ? RoadMode->Scene : nullptr;
	UWorld* World = IsValid(ActiveScene) ? ActiveScene->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Apply Loaded RoadScene Traffic Handedness")));
	for (TActorIterator<ARoadScene> SceneIt(World); SceneIt; ++SceneIt)
	{
		ARoadScene* Scene = *SceneIt;
		if (IsValid(Scene))
		{
			Scene->Modify();
			Scene->ApplyTrafficHandedness();
		}
	}
}

void USettings_File::Xodr()
{
	FEdModeRoad::Get()->Scene->ExportXodr();
}

void USettings_RoadPlan::Apply()
{
	if (ARoadActor* Road = FEdModeRoad::Get()->SelectedRoad)
	{
		const FScopedTransaction Transaction(FText::FromName(TEXT("RoadPlan")));
		USettings_RoadPlan* Data = GetMutableDefault<USettings_RoadPlan>();
		Road->Modify();
		Road->ClearLanes();
		URoadStyle* ResolvedStyle = nullptr;
		if (URoadPreset* PresetObj = Data->Preset.LoadSynchronous())
			ResolvedStyle = PresetObj->GenerateRoadStyle();
		else
			ResolvedStyle = Data->Style.LoadSynchronous();
		Road->InitWithStyle(ResolvedStyle);
		Road->UpdateCurve();
		Road->GetScene()->Rebuild();
	}
}

void USettings_RoadPlan::Create()
{
	if (ARoadActor* Road = FEdModeRoad::Get()->SelectedRoad)
		Road->CreateStyle();
}
#endif
