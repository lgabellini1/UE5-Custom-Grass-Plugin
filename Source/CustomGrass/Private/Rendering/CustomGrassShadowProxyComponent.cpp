#include "CustomGrassShadowProxyComponent.h"
#include "CustomGrassDataAsset.h"
#include "CustomGrassPrimitiveComponent.h"
#include "StaticMeshDescription.h"
#include "Landscape.h"
#include "StaticMeshOperations.h"

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

UCustomGrassShadowProxyComponent* UCustomGrassShadowProxyComponent::Make(const FInitConfig& Config,
	const UCustomGrassPrimitiveComponent* ParentGrassTile)
{
	auto* ShadowProxy = NewObject<UCustomGrassShadowProxyComponent>(
		GetOwner(),
		UCustomGrassShadowProxyComponent::StaticClass(),
		*FString::Printf(TEXT("CustomGrassShadowProxy_[%d]"), Config.TileIndex)
	);

	ShadowProxy->PlaneResolution = Config.PlaneMeshResolution;
	ShadowProxy->SetMaterial(0, Config.Material);

	ShadowProxy->ParentGrassTile = ParentGrassTile;

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
	const auto LandscapeComponent = ParentGrassTile->GetAssociatedLandscapeTile();

	const auto MeshBuilder = FCustomGrassShadowProxyMeshBuilder(LandscapeComponent, PlaneResolution);
	UStaticMesh* Mesh = MeshBuilder.Build(this);

	UMaterialInterface* Material = GetMaterial(0);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material));
	
	SetStaticMesh(Mesh);
}

FCustomGrassShadowProxyMeshBuilder::FCustomGrassShadowProxyMeshBuilder(
	ULandscapeComponent* LandscapeTile,
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
	check(LandscapeTile);

	UStaticMeshDescription* Desc = Mesh->CreateStaticMeshDescription(Outer);
    const FPolygonGroupID PolyGroup = Desc->CreatePolygonGroup();

	const int32 NumQuads = MeshResolution;
	const int32 NumVerts = NumQuads + 1;

	const int32 LandscapeQuads = LandscapeTile->ComponentSizeQuads;

	const FLandscapeComponentDataInterface LandscapeDataInterface(LandscapeTile);

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
