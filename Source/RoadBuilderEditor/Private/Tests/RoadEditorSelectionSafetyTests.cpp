// Copyright 2024. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "RoadActor.h"
#include "RoadEdMode.h"
#include "RoadScene.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderRoadPointSelectionSafetyTest,
	"RoadBuilder.Editor.RoadPointSelectionSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderRoadPointSelectionSafetyTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("An editor world is available"), World);
	if (!World)
	{
		return false;
	}

	ARoadActor* Road = World->SpawnActor<ARoadActor>();
	TestNotNull(TEXT("A transient road can be created"), Road);
	if (!Road)
	{
		return false;
	}

	Road->SetFlags(RF_Transient);
	Road->Init(0.0);
	Road->RoadPoints = {
		{ FVector2D(0.0, 0.0), 0.0 },
		{ FVector2D(1000.0, 0.0), 1000.0 }
	};
	Road->HeightPoints = {
		{ 0.0, 0.0 },
		{ 1000.0, 0.0 }
	};

	const int32 InsertedRoadPoint = Road->AddPoint(500.0);
	TestEqual(TEXT("AddPoint returns the inserted point index"), InsertedRoadPoint, 1);
	TestTrue(
		TEXT("The inserted road point is a valid editor selection"),
		FRoadTool::IsValidRoadPointSelection(Road, InsertedRoadPoint));
	TestFalse(
		TEXT("A stale road point index is rejected"),
		FRoadTool::IsValidRoadPointSelection(Road, Road->RoadPoints.Num()));
	TestEqual(
		TEXT("Invalid road point lookup safely returns the fallback position"),
		Road->GetPos(Road->RoadPoints.Num()),
		FVector::ZeroVector);

	int32 StaleRoadPointIndex = INDEX_NONE;
	const int32 PointCountBeforeAppend = Road->RoadPoints.Num();
	Road->InsertPoint(FVector2D(2000.0, 0.0), StaleRoadPointIndex);
	TestEqual(TEXT("An invalid selection safely extends the nearest road end"), StaleRoadPointIndex, PointCountBeforeAppend);
	TestEqual(TEXT("The safe extension appends exactly one point"), Road->RoadPoints.Num(), PointCountBeforeAppend + 1);
	TestEqual(TEXT("The appended point keeps the requested position"), Road->RoadPoints.Last().Pos, FVector2D(2000.0, 0.0));

	StaleRoadPointIndex = Road->RoadPoints.Num() + 100;
	Road->InsertPoint(FVector2D(-1000.0, 0.0), StaleRoadPointIndex);
	TestEqual(TEXT("A stale positive selection safely extends the start"), StaleRoadPointIndex, 0);
	TestEqual(TEXT("The prepended point keeps the requested position"), Road->RoadPoints[0].Pos, FVector2D(-1000.0, 0.0));

	const int32 InsertedHeightPoint = Road->AddHeight(500.0);
	TestEqual(TEXT("AddHeight returns the inserted point index"), InsertedHeightPoint, 1);
	TestTrue(
		TEXT("The inserted height point is a valid editor selection"),
		FRoadTool::IsValidHeightPointSelection(Road, InsertedHeightPoint));
	TestFalse(
		TEXT("A stale height point index is rejected"),
		FRoadTool::IsValidHeightPointSelection(Road, Road->HeightPoints.Num()));

	Road->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderLHTSplitReconnectTest,
	"RoadBuilder.Editor.LHTSplitReconnect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderLHTSplitReconnectTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("An editor world is available"), World);
	if (!World)
	{
		return false;
	}

	URoadStyle* MainStyle = LoadObject<URoadStyle>(
		nullptr,
		TEXT("/RoadBuilder/RoadStyles/Highway-Main-6-Lanes.Highway-Main-6-Lanes"));
	TestNotNull(TEXT("The highway main style is available"), MainStyle);
	if (!MainStyle)
	{
		return false;
	}

	ARoadScene* Scene = World->SpawnActor<ARoadScene>();
	TestNotNull(TEXT("A transient LHT RoadScene can be created"), Scene);
	if (!Scene)
	{
		return false;
	}
	Scene->SetFlags(RF_Transient);
	Scene->bOverrideTrafficHandedness = true;
	Scene->TrafficHandedness = ERoadTrafficHandedness::LeftHandTraffic;

	ARoadActor* FirstRoad = Scene->AddRoad(MainStyle, 0.0);
	FirstRoad->SetFlags(RF_Transient);
	TestTrue(
		TEXT("The LHT main road can be initialized"),
		FirstRoad->RuntimeSetRoadPoints(
			{ FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0), FVector(20000.0, 0.0, 0.0), FVector(30000.0, 0.0, 0.0) },
			false));

	ARoadActor* MiddleRoad = FirstRoad->Chop(10000.0);
	TestNotNull(TEXT("The LHT road can be chopped at the split start"), MiddleRoad);
	ARoadActor* Continuation = MiddleRoad ? MiddleRoad->Chop(10000.0) : nullptr;
	TestNotNull(TEXT("The LHT road can be chopped again at the merge end"), Continuation);
	ARoadActor* SplitRoad = nullptr;
	if (MiddleRoad && Continuation)
	{
		MiddleRoad->SetFlags(RF_Transient);
		Continuation->SetFlags(RF_Transient);
		TArray<URoadLane*> ForwardLanes = MiddleRoad->GetLanes(Scene->GetForwardTrafficSide(), { ELaneType::Driving });
		TestTrue(TEXT("The physical-left LHT side contains driving lanes"), ForwardLanes.Num() > 0);
		if (ForwardLanes.Num() > 0)
		{
			URoadBoundary* InnerBoundaryOfOuterLane = ForwardLanes.Last()->GetBoundary(RD_RIGHT);
			SplitRoad = MiddleRoad->Split(InnerBoundaryOfOuterLane);
			TestNotNull(TEXT("A physical-left LHT lane can be split"), SplitRoad);
		}
	}

	if (SplitRoad && MiddleRoad && Continuation)
	{
		SplitRoad->SetFlags(RF_Transient);
		TestTrue(
			TEXT("The geometrically reversed split still travels toward increasing world X in LHT"),
			(SplitRoad->GetDir(0.0) * Scene->GetTrafficDirectionSignForSide(RD_RIGHT)).X > 0.9);
		TestEqual(
			TEXT("The split lane reconnects to the downstream world-X road on its LHT travel end"),
			SplitRoad->ConnectedParents[0],
			Continuation);
		TestEqual(
			TEXT("The split lane keeps the upstream world-X road on its LHT travel start"),
			SplitRoad->ConnectedParents[1],
			FirstRoad);
		TestTrue(
			TEXT("The inherited downstream merge remains on the continuation's X-axis start"),
			FMath::IsNearlyEqual(SplitRoad->RoadPoints[0].Pos.X, Continuation->RoadPoints[0].Pos.X, 1.0));
		TestTrue(
			TEXT("The inherited upstream split remains on the first road's X-axis end"),
			FMath::IsNearlyEqual(SplitRoad->RoadPoints.Last().Pos.X, FirstRoad->RoadPoints.Last().Pos.X, 1.0));
		for (const FRoadPoint& Point : SplitRoad->RoadPoints)
		{
			TestTrue(TEXT("Every LHT split control point stays finite"), FMath::IsFinite(Point.Pos.X) && FMath::IsFinite(Point.Pos.Y));
		}
	}

	TArray<AActor*> AttachedActors;
	Scene->GetAttachedActors(AttachedActors, true, true);
	for (int32 ActorIndex = AttachedActors.Num() - 1; ActorIndex >= 0; --ActorIndex)
	{
		if (IsValid(AttachedActors[ActorIndex]))
		{
			AttachedActors[ActorIndex]->Destroy();
		}
	}
	Scene->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderCrossRoadSceneConnectionTest,
	"RoadBuilder.Editor.CrossRoadSceneConnection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderCrossRoadSceneConnectionTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("An editor world is available"), World);
	if (!World)
	{
		return false;
	}

	ARoadScene* ParentScene = World->SpawnActor<ARoadScene>();
	ARoadScene* ChildScene = World->SpawnActor<ARoadScene>();
	TestNotNull(TEXT("The parent RoadScene can be created"), ParentScene);
	TestNotNull(TEXT("The child RoadScene can be created"), ChildScene);
	if (!ParentScene || !ChildScene)
	{
		return false;
	}
	ParentScene->SetFlags(RF_Transient);
	ChildScene->SetFlags(RF_Transient);

	ARoadActor* ParentRoad = ParentScene->AddRoad(nullptr, 0.0);
	ARoadActor* ChildRoad = ChildScene->AddRoad(nullptr, 0.0);
	ParentRoad->SetFlags(RF_Transient);
	ChildRoad->SetFlags(RF_Transient);
	TestTrue(
		TEXT("The parent road points are valid"),
		ParentRoad->RuntimeSetRoadPoints(
			{ FVector(0.0, 0.0, 0.0), FVector(20000.0, 0.0, 0.0) }, false));
	TestTrue(
		TEXT("The child road points are valid"),
		ChildRoad->RuntimeSetRoadPoints(
			{ FVector(20000.0, 0.0, 0.0), FVector(30000.0, 0.0, 0.0) }, false));

	ChildRoad->ConnectTo(ParentRoad, FVector2D(ParentRoad->Length(), 0.0), 0);
	TestTrue(TEXT("A soft cross-RoadScene seam is stored"), ChildRoad->HasCrossRoadSceneConnection(0));
	TestNull(TEXT("The seam does not create a hard ConnectedParents reference"), ChildRoad->ConnectedParents[0]);
	TestEqual(TEXT("The parent does not gain a hard child reference"), ParentRoad->ConnectedChildren.Num(), 0);
	TestEqual(TEXT("Exactly one seam record is present"), ChildRoad->CrossRoadSceneConnections.Num(), 1);
	if (ChildRoad->CrossRoadSceneConnections.Num() == 1)
	{
		const FCrossRoadSceneConnection& Connection = ChildRoad->CrossRoadSceneConnections[0];
		TestEqual(TEXT("The soft seam resolves while both cells are loaded"), Connection.Parent.Get(), ParentRoad);
		TestEqual(TEXT("The seam remains pinned to the target end"), Connection.ParentEndpoint, 1);
	}

	ParentRoad->RoadPoints.Last().Pos = FVector2D(21000.0, 500.0);
	ParentRoad->UpdateCurve();
	ChildRoad->UpdateCurve();
	TestTrue(
		TEXT("Refreshing follows the moved target endpoint"),
		ChildRoad->RoadPoints[0].Pos.Equals(ParentRoad->RoadPoints.Last().Pos, 0.1));

	int32 ChildPointIndex = 0;
	ChildRoad->DisconnectAll(ChildPointIndex);
	TestFalse(TEXT("Disconnect removes the soft seam"), ChildRoad->HasCrossRoadSceneConnection(0));

	ChildRoad->Destroy();
	ParentRoad->Destroy();
	ChildScene->Destroy();
	ParentScene->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderRoadChopSafetyTest,
	"RoadBuilder.Editor.RoadChopSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderRoadChopSafetyTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("An editor world is available"), World);
	if (!World)
	{
		return false;
	}

	ARoadScene* Scene = World->SpawnActor<ARoadScene>();
	TestNotNull(TEXT("A transient RoadScene can be created"), Scene);
	if (!Scene)
	{
		return false;
	}
	Scene->SetFlags(RF_Transient);

	ARoadActor* FirstRoad = Scene->AddRoad(nullptr, 0.0);
	FirstRoad->SetFlags(RF_Transient);
	TestTrue(
		TEXT("The source road points are valid"),
		FirstRoad->RuntimeSetRoadPoints(
			{ FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0), FVector(20000.0, 0.0, 0.0) },
			false));
	FCrossRoadSceneConnection& StartConnection = FirstRoad->CrossRoadSceneConnections.AddDefaulted_GetRef();
	StartConnection.Index = 0;
	FCrossRoadSceneConnection& EndConnection = FirstRoad->CrossRoadSceneConnections.AddDefaulted_GetRef();
	EndConnection.Index = 1;

	const int32 InitialRoadCount = Scene->Roads.Num();
	const double OriginalLength = FirstRoad->Length();
	TestNull(TEXT("A chop at the start is rejected"), FirstRoad->Chop(0.0));
	TestNull(TEXT("A chop at the end is rejected"), FirstRoad->Chop(OriginalLength));
	TestEqual(TEXT("Rejected chops do not create actors"), Scene->Roads.Num(), InitialRoadCount);

	const double ChopDistance = OriginalLength * 0.5;
	const FVector2D ExpectedSeam = FirstRoad->GetPos(ChopDistance);
	ARoadActor* SecondRoad = FirstRoad->Chop(ChopDistance);
	TestNotNull(TEXT("A valid middle chop creates the second half"), SecondRoad);
	if (SecondRoad)
	{
		SecondRoad->SetFlags(RF_Transient);
		TestEqual(TEXT("Exactly one road actor is added"), Scene->Roads.Num(), InitialRoadCount + 1);
		TestTrue(TEXT("The first half remains a valid curve"), FirstRoad->RoadPoints.Num() >= 2 && FirstRoad->HeightPoints.Num() >= 2);
		TestTrue(TEXT("The second half remains a valid curve"), SecondRoad->RoadPoints.Num() >= 2 && SecondRoad->HeightPoints.Num() >= 2);
		TestTrue(TEXT("The first half ends at the requested seam"), FirstRoad->RoadPoints.Last().Pos.Equals(ExpectedSeam, 0.1));
		TestTrue(TEXT("The second half starts at the requested seam"), SecondRoad->RoadPoints[0].Pos.Equals(ExpectedSeam, 0.1));
		TestTrue(TEXT("The first half keeps the second half as its seam parent"), FirstRoad->ConnectedParents[1] == SecondRoad);
		TestTrue(TEXT("The second half keeps the first half as its seam parent"), SecondRoad->ConnectedParents[0] == FirstRoad);
		TestEqual(TEXT("The first half keeps only its original start soft connection"), FirstRoad->CrossRoadSceneConnections.Num(), 1);
		TestEqual(TEXT("The second half keeps only its original end soft connection"), SecondRoad->CrossRoadSceneConnections.Num(), 1);
		if (FirstRoad->CrossRoadSceneConnections.Num() == 1)
		{
			TestEqual(TEXT("The first soft connection remains on endpoint 0"), FirstRoad->CrossRoadSceneConnections[0].Index, 0);
		}
		if (SecondRoad->CrossRoadSceneConnections.Num() == 1)
		{
			TestEqual(TEXT("The second soft connection remains on endpoint 1"), SecondRoad->CrossRoadSceneConnections[0].Index, 1);
		}

		const FConnectInfo* ForwardSeam = FirstRoad->ConnectedChildren.FindByPredicate([SecondRoad](const FConnectInfo& Info)
		{
			return Info.Child == SecondRoad && Info.Index == 0;
		});
		const FConnectInfo* ReverseSeam = SecondRoad->ConnectedChildren.FindByPredicate([FirstRoad](const FConnectInfo& Info)
		{
			return Info.Child == FirstRoad && Info.Index == 1;
		});
		TestTrue(TEXT("The forward seam is geometry-preserving"), ForwardSeam && ForwardSeam->bPreserveGeometry);
		TestTrue(TEXT("The reverse seam is geometry-preserving"), ReverseSeam && ReverseSeam->bPreserveGeometry);

		const int32 FirstPointCount = FirstRoad->RoadPoints.Num();
		const int32 SecondPointCount = SecondRoad->RoadPoints.Num();
		FirstRoad->UpdateCurve();
		SecondRoad->UpdateCurve();
		TestEqual(TEXT("Rebuilding does not add a direction point to the first half"), FirstRoad->RoadPoints.Num(), FirstPointCount);
		TestEqual(TEXT("Rebuilding does not add a direction point to the second half"), SecondRoad->RoadPoints.Num(), SecondPointCount);
		TestTrue(TEXT("Rebuilding preserves the first seam position"), FirstRoad->RoadPoints.Last().Pos.Equals(ExpectedSeam, 0.1));
		TestTrue(TEXT("Rebuilding preserves the second seam position"), SecondRoad->RoadPoints[0].Pos.Equals(ExpectedSeam, 0.1));
		TestTrue(
			TEXT("The two road lengths still cover the original road"),
			FMath::IsNearlyEqual(FirstRoad->Length() + SecondRoad->Length(), OriginalLength, 0.1));
	}

	if (SecondRoad)
	{
		SecondRoad->Destroy();
	}
	FirstRoad->Destroy();
	Scene->Destroy();
	return true;
}

#endif
