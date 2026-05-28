#include "CustomGrassShadowProxyComponent.h"

#include "CustomGrassDataAsset.h"
#include "StaticMeshDescription.h"
#include "Landscape.h"
#include "StaticMeshOperations.h"

UCustomGrassShadowProxyComponent::UCustomGrassShadowProxyComponent(const FObjectInitializer& ObjectInitializer)
	: UStaticMeshComponent(ObjectInitializer)
{
	SetVisibility(false);
	
	SetCastShadow(true);
	bCastHiddenShadow = true;
	bCastContactShadow = false;

	// Let WPO cast shadows
	bEvaluateWorldPositionOffset = true;
	bEvaluateWorldPositionOffsetInRayTracing = true;
}

void UCustomGrassShadowProxyComponent::BuildMesh(ULandscapeComponent* LandscapeComponent,
	const UCustomGrassDataAsset* DataAsset)
{
	const auto LandscapeComponentDataInterface = FLandscapeComponentDataInterface(LandscapeComponent);
	
	float Size = LandscapeComponent->ComponentSizeQuads * 1.f;
	
	int32 Quads = PlaneResolution;
	int32 Verts = Quads + 1;
	
	int32 TotalVerts = Verts * Verts;

    UStaticMesh* Mesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
    Mesh->InitResources();
    Mesh->SetLightingGuid();
	
    UStaticMeshDescription* Desc = Mesh->CreateStaticMeshDescription(this);
    FPolygonGroupID PolyGroup = Desc->CreatePolygonGroup();
 
    TArray<FVertexID> Vertices;
    Vertices.Reserve(TotalVerts);
	
    for (int32 Row = 0; Row < Verts; ++Row)
    {
        float V  = Row / static_cast<float>(Quads);
        float PY = V * Size;
 
        for (int32 Col = 0; Col < Verts; ++Col)
        {
            float U  = Col / static_cast<float>(Quads);
        	float PX = U * Size;

        	float LX = U * LandscapeComponent->ComponentSizeQuads;
        	float LY = V * LandscapeComponent->ComponentSizeQuads;

        	int32 LocalX = FMath::Clamp(FMath::RoundToInt(LX), 0, LandscapeComponent->ComponentSizeQuads);
        	int32 LocalY = FMath::Clamp(FMath::RoundToInt(LY), 0, LandscapeComponent->ComponentSizeQuads);

        	float HeightZ = LandscapeComponentDataInterface.GetLocalHeight(LocalX, LocalY);
 
            FVertexID Vertex = Desc->CreateVertex();
            Desc->SetVertexPosition(Vertex, FVector(PX, PY, HeightZ));
            Vertices.Add(Vertex);
        }
    }
	
    TVertexInstanceAttributesRef<FVector2f> UVAttr = Desc->VertexInstanceAttributes().GetAttributesRef<FVector2f>(
    	MeshAttribute::VertexInstance::TextureCoordinate);
 
    auto MakeInstance = [&](FVertexID Vertex, float U, float V) -> FVertexInstanceID
    {
        FVertexInstanceID IID = Desc->CreateVertexInstance(Vertex);
        UVAttr.Set(IID, 0, FVector2f(U, V));
        return IID;
    };
 
    TArray<FEdgeID> NewEdgeIDs;
 
    for (int32 Row = 0; Row < Quads; ++Row)
    {
        for (int32 Col = 0; Col < Quads; ++Col)
        {
            int32 BL_Idx = Row * Verts + Col;
            int32 BR_Idx = BL_Idx + 1;
            int32 TL_Idx = BL_Idx + Verts;
            int32 TR_Idx = TL_Idx + 1;
 
            float U0 =  Col		 / static_cast<float>(Quads);
            float U1 = (Col + 1) / static_cast<float>(Quads);
        	
            float V0 =  Row		 / static_cast<float>(Quads);
            float V1 = (Row + 1) / static_cast<float>(Quads);
 
            // Triangle 1: BL, TL, BR
            {
                NewEdgeIDs.Reset();
                TArray<FVertexInstanceID> Tri = {
                    MakeInstance(Vertices[BL_Idx], U0, V0),
                    MakeInstance(Vertices[TL_Idx], U0, V1),
                    MakeInstance(Vertices[BR_Idx], U1, V0)
                };
                Desc->CreateTriangle(PolyGroup, Tri, NewEdgeIDs);
            }
 
            // Triangle 2: BR, TL, TR
            {
                NewEdgeIDs.Reset();
                TArray<FVertexInstanceID> Tri = {
                    MakeInstance(Vertices[BR_Idx], U1, V0),
                    MakeInstance(Vertices[TL_Idx], U0, V1),
                    MakeInstance(Vertices[TR_Idx], U1, V1)
                };
                Desc->CreateTriangle(PolyGroup, Tri, NewEdgeIDs);
            }
        }
    }
 
    FStaticMeshOperations::ComputeTriangleTangentsAndNormals(Desc->GetMeshDescription());
    FStaticMeshOperations::ComputeTangentsAndNormals(Desc->GetMeshDescription(),
    	EComputeNTBsFlags::Normals | EComputeNTBsFlags::Tangents);
 
    Mesh->GetStaticMaterials().Add(FStaticMaterial(DataAsset->ShadowProxyMaterial));
 
    TArray<UStaticMeshDescription*> Descriptions = { Desc };
    Mesh->BuildFromStaticMeshDescriptions(Descriptions, false);
	
	Mesh->CalculateExtendedBounds();
	SetBoundsScale(5.f);

	Mesh->bAllowCPUAccess = false;
	Mesh->NeverStream = true;
 
    SetStaticMesh(Mesh);
}
