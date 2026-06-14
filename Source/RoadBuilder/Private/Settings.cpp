// Publisher: Fullike (https://github.com/fullike)
// Copyright 2024. All Rights Reserved.

#include "RoadBuilderSettings.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/SoftObjectPath.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#include "RoadBuilderEditor/Public/RoadEdMode.h"
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
		Road->InitWithStyle(Data->Style.LoadSynchronous());
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
