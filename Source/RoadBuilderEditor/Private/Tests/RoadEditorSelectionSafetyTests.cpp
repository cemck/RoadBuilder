// Copyright 2024. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "RoadActor.h"
#include "RoadBuilderWorldPartition.h"
#include "RoadEdMode.h"
#include "RoadMesh.h"
#include "RoadScene.h"
#include "Components/StaticMeshComponent.h"

namespace
{
	bool HasGeneratedJunctionSurface(const AJunctionActor* Junction)
	{
		const UStaticMeshComponent* Component = Junction
			? Cast<UStaticMeshComponent>(Junction->GetRootComponent())
			: nullptr;
		return Junction && Junction->LastBuildTriangleCount > 0 &&
			Component && Component->GetStaticMesh();
	}
}

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
	FRoadBuilderMarkingClearSafetyTest,
	"RoadBuilder.Editor.MarkingClearSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderMarkingClearSafetyTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("An editor world is available"), World);
	if (!World)
	{
		return false;
	}

	ARoadActor* Road = World->SpawnActor<ARoadActor>();
	TestNotNull(TEXT("A transient marking owner can be created"), Road);
	if (!Road)
	{
		return false;
	}
	Road->SetFlags(RF_Transient);

	UMarkingCurve* Marking = Road->AddMarkingCurve(true);
	TestNotNull(TEXT("An authored marking can be created"), Marking);
	Road->Markings.Add(Marking); // Reproduce an older duplicated owner reference.
	TestEqual(TEXT("The duplicate marking reference is present before clear"), Road->Markings.Num(), 2);
	Road->DeleteMarking(Marking);
	TestEqual(TEXT("Clear removes every duplicate owner reference"), Road->Markings.Num(), 0);
	TestFalse(TEXT("Clear does not begin destroying a marking still referenced by editor state"),
		Marking->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed));

	UMarkingPoint* Point = Road->AddMarkingPoint(FVector2D::ZeroVector);
	UMarkingCurve* Curve = Road->AddMarkingCurve();
	Road->DeleteAllMarkings();
	TestEqual(TEXT("Clear all detaches every marking"), Road->Markings.Num(), 0);
	TestFalse(TEXT("Clear all leaves point lifetime to GC"), Point->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed));
	TestFalse(TEXT("Clear all leaves curve lifetime to GC"), Curve->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed));

	Road->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderBoundarySegmentSelectionSafetyTest,
	"RoadBuilder.Editor.BoundarySegmentSelectionSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderBoundarySegmentSelectionSafetyTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("An editor world is available"), World);
	if (!World)
	{
		return false;
	}

	ARoadActor* Road = World->SpawnActor<ARoadActor>();
	TestNotNull(TEXT("A transient boundary owner can be created"), Road);
	if (!Road)
	{
		return false;
	}
	Road->SetFlags(RF_Transient);
	Road->Init(0.0);
	URoadBoundary* Boundary = Road->BaseCurve;
	Boundary->Segments.Add({ 500.0, nullptr, nullptr });

	TestTrue(TEXT("A live boundary segment can be exposed to structure details"),
		FRoadTool::IsValidBoundarySegmentSelection(Boundary, 1));
	Boundary->Segments[1].LaneMarking = nullptr;
	TestTrue(TEXT("Clearing LaneMarking keeps the live segment selection valid"),
		FRoadTool::IsValidBoundarySegmentSelection(Boundary, 1));
	Boundary->DeleteSegment(1);
	TestFalse(TEXT("A segment index invalidated by RemoveAt is rejected before editor reuse"),
		FRoadTool::IsValidBoundarySegmentSelection(Boundary, 1));
	URoadBoundary* DetachedBoundary = NewObject<URoadBoundary>(Road);
	DetachedBoundary->Segments.AddDefaulted();
	TestFalse(TEXT("A detached boundary is rejected even if its local index exists"),
		FRoadTool::IsValidBoundarySegmentSelection(DetachedBoundary, 0));

	Road->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderGeometryActionSafetyTest,
	"RoadBuilder.Editor.GeometryActionSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderGeometryActionSafetyTest::RunTest(const FString& Parameters)
{
	FString FailureReason;
	TestFalse(
		TEXT("Generated actor mutation rejects a missing world"),
		RoadBuilderWorldPartition::CanMutateGeneratedActorGraph(nullptr, nullptr, &FailureReason));
	TestFalse(TEXT("A rejected mutation supplies a failure reason"), FailureReason.IsEmpty());

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
	ARoadActor* Road = Scene->AddRoad(nullptr, 0.0);
	TestNotNull(TEXT("A road can be created in the safe scene"), Road);
	if (!Road)
	{
		Scene->Destroy();
		return false;
	}
	Road->SetFlags(RF_Transient);
	TestTrue(
		TEXT("A finite runtime road is accepted"),
		Road->RuntimeSetRoadPoints(
			{ FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0), FVector(10000.0, 10000.0, 0.0) },
			false));
	TestTrue(TEXT("A valid authored curve is safe to rebuild"), Road->HasSafeAuthoredCurve(&FailureReason));
	const int32 ValidSegmentCount = Road->RoadSegments.Num();

	Road->RoadPoints[1].MaxRadius = 0.0;
	TestFalse(TEXT("A zero-radius turn is rejected before rebuilding"), Road->HasSafeAuthoredCurve(&FailureReason));
	Road->UpdateCurve();
	TestEqual(
		TEXT("Rejected curve changes retain the previous render-safe derived geometry"),
		Road->RoadSegments.Num(),
		ValidSegmentCount);
	Scene->Rebuild();
	TestEqual(
		TEXT("The scene rejects the unsafe action before regenerating actor children"),
		Road->RoadSegments.Num(),
		ValidSegmentCount);

	Road->RoadPoints[1].MaxRadius = 50000.0;
	Road->UpdateCurve();
	Scene->Rebuild();
	TestTrue(TEXT("The repaired road accepts rebuilding again"), Road->HasSafeDerivedGeometry(&FailureReason));

	// Detached World Partition geometry actors are selectable at the Outliner
	// root.  Moving one must not apply a second transform to world-space mesh
	// vertices or make a junction appear to lose its generated mesh.
	Road->SetLockLocation(false);
	Road->SetActorTransform(FTransform(FRotator(0.0, 25.0, 0.0), FVector(1234.0, -5678.0, 900.0), FVector(2.0)));
	RoadBuilderWorldPartition::PrepareGeneratedGeometryActor(Road);
	TestTrue(TEXT("Generated geometry actors are restored to the world-space identity transform"), Road->GetActorTransform().Equals(FTransform::Identity));
	TestTrue(TEXT("Generated geometry actors reject accidental editor movement"), Road->IsLockLocation());

	// Height points may be dragged only inside their neighboring interval.  A
	// crossed point used to leave decreasing derived distances, causing every
	// connected junction to reject its mesh rebuild.
	FHeightPoint CrossedHeightPoint;
	CrossedHeightPoint.Dist = Road->Length() * 2.0;
	CrossedHeightPoint.Height = 250.0;
	CrossedHeightPoint.Range = 0.0;
	Road->HeightPoints.Insert(CrossedHeightPoint, 1);
	Road->UpdateCurve();
	TestTrue(TEXT("A crossed height point is clamped before derived geometry is generated"),
		Road->HeightPoints[1].Dist > Road->HeightPoints[0].Dist &&
		Road->HeightPoints[1].Dist < Road->HeightPoints[2].Dist);
	TestTrue(TEXT("A zero-range crossed height edit still produces safe derived geometry"), Road->HasSafeDerivedGeometry(&FailureReason));
	for (int32 SegmentIndex = 1; SegmentIndex < Road->HeightSegments.Num(); ++SegmentIndex)
	{
		TestTrue(TEXT("Derived height segment distances remain strictly increasing"),
			Road->HeightSegments[SegmentIndex].Dist > Road->HeightSegments[SegmentIndex - 1].Dist + UE_DOUBLE_SMALL_NUMBER);
	}

	FStaticRoadMesh Mesh;
	Mesh.AddTriangles(nullptr, { FIndex3i(0, 1, 3) }, { FVector::ZeroVector, FVector(100.0, 0.0, 0.0), FVector(0.0, 100.0, 0.0) }, FVector::UpVector);
	TestEqual(TEXT("Invalid triangle indices are rejected before mesh creation"), Mesh.GetTriangleCount(), 0);

	Road->Destroy();
	Scene->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRoadBuilderJunctionMeshSafetyTest,
	"RoadBuilder.Editor.JunctionMeshSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoadBuilderJunctionMeshSafetyTest::RunTest(const FString& Parameters)
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
	TestNotNull(TEXT("The junction test road style is available"), MainStyle);
	if (!MainStyle)
	{
		return false;
	}

	ARoadScene* Scene = World->SpawnActor<ARoadScene>();
	TestNotNull(TEXT("A transient junction RoadScene can be created"), Scene);
	if (!Scene)
	{
		return false;
	}
	Scene->SetFlags(RF_Transient);

	ARoadActor* EastWestRoad = Scene->AddRoad(MainStyle, 0.0);
	ARoadActor* NorthSouthRoad = Scene->AddRoad(MainStyle, 0.0);
	TestNotNull(TEXT("The east-west junction road can be created"), EastWestRoad);
	TestNotNull(TEXT("The north-south junction road can be created"), NorthSouthRoad);
	if (!EastWestRoad || !NorthSouthRoad)
	{
		RoadBuilderWorldPartition::DestroyAttachedGeneratedActors(Scene);
		Scene->Destroy();
		return false;
	}
	EastWestRoad->SetFlags(RF_Transient);
	NorthSouthRoad->SetFlags(RF_Transient);
	TestTrue(TEXT("The east-west road accepts its crossing geometry"), EastWestRoad->RuntimeSetRoadPoints(
		{ FVector(-10000.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0) }, false));
	TestTrue(TEXT("The north-south road accepts its crossing geometry"), NorthSouthRoad->RuntimeSetRoadPoints(
		{ FVector(0.0, -10000.0, 0.0), FVector(0.0, 10000.0, 0.0) }, false));
	Scene->Rebuild();

	TestTrue(TEXT("A crossing creates at least one junction"), Scene->Junctions.Num() > 0);
	AJunctionActor* Junction = Scene->Junctions.IsEmpty() ? nullptr : Scene->Junctions[0];
	TestNotNull(TEXT("The crossing exposes a valid junction actor"), Junction);
	UStaticMeshComponent* JunctionMeshComponent = Junction
		? Cast<UStaticMeshComponent>(Junction->GetRootComponent())
		: nullptr;
	TestNotNull(TEXT("The junction has a static mesh root component"), JunctionMeshComponent);
	TestTrue(TEXT("The scene rebuild emits junction source triangles"),
		Junction && Junction->LastBuildTriangleCount > 0);
	TestNotNull(TEXT("The scene rebuild assigns the generated junction mesh"),
		JunctionMeshComponent ? JunctionMeshComponent->GetStaticMesh().Get() : nullptr);

	if (Junction)
	{
		Junction->SetLockLocation(false);
		Junction->SetActorTransform(FTransform(FRotator(10.0, 20.0, 30.0), FVector(4000.0, 5000.0, 6000.0), FVector(1.5)));
		Junction->Build();
		TestTrue(TEXT("A moved junction is restored to the generated world-space transform"),
			Junction->GetActorTransform().Equals(FTransform::Identity));
		TestTrue(TEXT("A rebuilt junction is locked against later editor movement"), Junction->IsLockLocation());
		JunctionMeshComponent = Cast<UStaticMeshComponent>(Junction->GetRootComponent());
		TestTrue(TEXT("The restored junction still has a generated surface mesh"),
			HasGeneratedJunctionSurface(Junction));
	}

	FHeightPoint CrossedHeightPoint;
	CrossedHeightPoint.Dist = EastWestRoad->Length() * 2.0;
	CrossedHeightPoint.Height = 400.0;
	CrossedHeightPoint.Range = 0.0;
	EastWestRoad->HeightPoints.Insert(CrossedHeightPoint, 1);
	EastWestRoad->UpdateCurve();
	Scene->RebuildHeightOnly(EastWestRoad);
	FString FailureReason;
	TestTrue(TEXT("A crossed road-height edit is repaired before the junction rebuild"),
		EastWestRoad->HasSafeDerivedGeometry(&FailureReason));
	JunctionMeshComponent = Junction ? Cast<UStaticMeshComponent>(Junction->GetRootComponent()) : nullptr;
	TestTrue(TEXT("The junction surface survives the repaired height edit"),
		HasGeneratedJunctionSurface(Junction));

	RoadBuilderWorldPartition::DestroyAttachedGeneratedActors(Scene);
	Scene->Destroy();
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
