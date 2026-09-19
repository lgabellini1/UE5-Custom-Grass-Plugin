#include "CustomGrassShadowProxyComponent.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassPrimitiveComponent.h"
#include "StaticMeshDescription.h"
#include "StaticMeshOperations.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Landscape.h"
#include "Constants.h"
#include "CustomGrassWorldSubsystem.h"
#include "Utilities.h"

UCustomGrassShadowProxyComponent::UCustomGrassShadowProxyComponent(const FObjectInitializer& ObjectInitializer)
	: UStaticMeshComponent(ObjectInitializer)
{
	SetVisibility(false);

	CastShadow		   = true;
	bCastHiddenShadow  = true;
	bCastContactShadow = false;
	bCastDynamicShadow = true;

	// Let WPO cast shadows
	bEvaluateWorldPositionOffset = true;
	bEvaluateWorldPositionOffsetInRayTracing = true;
}

UCustomGrassShadowProxyComponent* UCustomGrassShadowProxyComponent::Make(
	const UCustomGrassDataAsset& DataAsset,
	int32 TileIndex,
	const UCustomGrassPrimitiveComponent& ParentGrassTile)
{
	auto* ShadowProxy = NewObject<UCustomGrassShadowProxyComponent>(
		ParentGrassTile.GetOwner(),
		UCustomGrassShadowProxyComponent::StaticClass(),
		*FString::Printf(TEXT("CustomGrassShadowProxy_[%d]"), TileIndex)
	);

	ShadowProxy->PlaneResolution = DataAsset.ShadowProxyResolution;
	ShadowProxy->SetMaterial(0, CreateMID(DataAsset, ParentGrassTile));

	ShadowProxy->ParentGrassTile = &ParentGrassTile;

	return ShadowProxy;
}

void UCustomGrassShadowProxyComponent::UpdateRenderSettings(const UCustomGrassDataAsset& DataAsset)
{
	PlaneResolution = DataAsset.ShadowProxyResolution;
	BuildMesh();

	SetCastShadow(DataAsset.bShadowsEnabled);
	SetVisibility(DataAsset.bDebugShowShadowProxyMesh);	
}

void UCustomGrassShadowProxyComponent::BuildMesh()
{
	ULandscapeComponent& LandscapeTile = ParentGrassTile->GetAssociatedLandscapeTile();

	const auto MeshBuilder = FCustomGrassShadowProxyMeshBuilder(LandscapeTile, PlaneResolution);
	UStaticMesh* Mesh = MeshBuilder.Build(this);

	Mesh->GetStaticMaterials().Add(FStaticMaterial(GetMaterial(0)));
	
	SetStaticMesh(Mesh);
}

UMaterialInstanceDynamic* UCustomGrassShadowProxyComponent::CreateMID(
	const UCustomGrassDataAsset& DataAsset,
	const UCustomGrassPrimitiveComponent& ParentGrassTile)
{
	auto* World = ParentGrassTile.GetWorld();
	
	auto* ShadowProxyMID = UMaterialInstanceDynamic::Create(
		DataAsset.ShadowProxyMaterial, World);

	const auto Subsystem = World->GetSubsystemChecked<UCustomGrassWorldSubsystem>();
	
	ShadowProxyMID->SetTextureParameterValue(
		TEXT("ShadowWPOTextureAtlas"), Subsystem->GetShadowMapTextureAtlas());
	ShadowProxyMID->SetScalarParameterValue(
		TEXT("AtlasGridSize"), CustomGrass::MaxRenderedTiles / 2);

	const auto* LandscapeActor = ParentGrassTile.GetAssociatedLandscapeTile().GetLandscapeActor();
	
	ShadowProxyMID->SetVectorParameterValue(
		TEXT("LandscapeWorldOrigin"), FLinearColor(LandscapeActor->GetActorLocation()));
	ShadowProxyMID->SetScalarParameterValue(
		TEXT("LandscapeWorldSize"), CustomGrass::GetLandscapeExtentInWorldUnits(*LandscapeActor).X);
	
	ShadowProxyMID->SetScalarParameterValue(
		TEXT("MaxGrassHeight"), CustomGrass::MaxGrassBladeHeight);	
		
	return ShadowProxyMID;	
}

FCustomGrassShadowProxyMeshBuilder::FCustomGrassShadowProxyMeshBuilder(
	ULandscapeComponent& LandscapeTile,
	int32 MeshResolution)
: LandscapeTile(LandscapeTile), MeshResolution(MeshResolution)
{}

UStaticMesh* FCustomGrassShadowProxyMeshBuilder::Build(UObject* Outer) const
{
	auto* StaticMesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
	
	StaticMesh->InitResources();
	StaticMesh->SetLightingGuid();

	UStaticMeshDescription* Desc = BuildMeshDescription(StaticMesh, Outer);

	FStaticMeshOperations::ComputeTriangleTangentsAndNormals(Desc->GetMeshDescription());
	FStaticMeshOperations::ComputeTangentsAndNormals(Desc->GetMeshDescription(),
		EComputeNTBsFlags::Normals | EComputeNTBsFlags::Tangents);
	
	const TArray<UStaticMeshDescription*> Descriptions = { Desc };
	StaticMesh->BuildFromStaticMeshDescriptions(Descriptions, false);
	
	StaticMesh->CalculateExtendedBounds();

	StaticMesh->bAllowCPUAccess = false;
	StaticMesh->NeverStream = true;
	
	return StaticMesh;
}

UStaticMeshDescription* FCustomGrassShadowProxyMeshBuilder::BuildMeshDescription(
	const UStaticMesh* Mesh, 
	UObject* Outer) const
{
	UStaticMeshDescription* Desc = Mesh->CreateStaticMeshDescription(Outer);
    const FPolygonGroupID PolyGroup = Desc->CreatePolygonGroup();

	const int32 NumQuads = MeshResolution;
	const int32 NumVerts = NumQuads + 1;

	const int32 LandscapeQuads = LandscapeTile.ComponentSizeQuads;

	const FLandscapeComponentDataInterface LandscapeDataInterface(&LandscapeTile);

	TArray<FVertexID> Vertices;
	Vertices.Reserve(NumVerts * NumVerts);

	auto MakeVertex = [&](int32 Row, int32 Col) -> FVertexID
	{
		const float U = Col / static_cast<float>(NumQuads);
		const float V = Row / static_cast<float>(NumQuads);

		const float X = U * LandscapeQuads;
		const float Y = V * LandscapeQuads;

		const int32 LocalX = FMath::Clamp(FMath::RoundToInt(X), 0, LandscapeQuads);
		const int32 LocalY = FMath::Clamp(FMath::RoundToInt(Y), 0, LandscapeQuads);

		const float Z = LandscapeDataInterface.GetLocalHeight(LocalX, LocalY);

		const FVertexID Vertex = Desc->CreateVertex();

		Desc->SetVertexPosition(Vertex, FVector(X, Y, Z));
		return Vertex;
	};

	for (int32 Row = 0; Row < NumVerts; ++Row)
	{
		for (int32 Col = 0; Col < NumVerts; ++Col)
		{
			FVertexID Vertex = MakeVertex(Row, Col);
			Vertices.Add(Vertex);
		}
	}

	const auto UVAttr = Desc->VertexInstanceAttributes().GetAttributesRef<FVector2f>(
		MeshAttribute::VertexInstance::TextureCoordinate);	

	TArray<FEdgeID> Edges;
	TArray<FVertexInstanceID> Triangle;
	Triangle.Reserve(3);

	auto MakeInstance = [&](FVertexID Vertex, float U, float V) -> FVertexInstanceID
	{
		const FVertexInstanceID Instance = Desc->CreateVertexInstance(Vertex);

		UVAttr.Set(Instance, 0, FVector2f(U, V));

		return Instance;
	};

	for (int32 Row = 0; Row < NumQuads; ++Row)
	{
		for (int32 Col = 0; Col < NumQuads; ++Col)
		{
			const int32 BL = Row * NumVerts + Col;
			const int32 BR = BL + 1;
			const int32 TL = BL + NumVerts;
			const int32 TR = TL + 1;

			const float U0 =  Col 	   / static_cast<float>(NumQuads);
			const float U1 = (Col + 1) / static_cast<float>(NumQuads);

			const float V0 =  Row	   / static_cast<float>(NumQuads);
			const float V1 = (Row + 1) / static_cast<float>(NumQuads);

			Triangle.Reset();
			Triangle.Add(MakeInstance(Vertices[BL], U0, V0));
			Triangle.Add(MakeInstance(Vertices[TL], U0, V1));
			Triangle.Add(MakeInstance(Vertices[BR], U1, V0));

			Edges.Reset();
			Desc->CreateTriangle(PolyGroup, Triangle, Edges);

			Triangle.Reset();
			Triangle.Add(MakeInstance(Vertices[BR], U1, V0));
			Triangle.Add(MakeInstance(Vertices[TL], U0, V1));
			Triangle.Add(MakeInstance(Vertices[TR], U1, V1));

			Edges.Reset();
			Desc->CreateTriangle(PolyGroup, Triangle, Edges);
		}
	}
	
	return Desc;
}
