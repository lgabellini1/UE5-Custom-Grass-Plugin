#include "Utilities.h"
#include "Landscape.h"

FVector2D GetLandscapeExtentInWorldUnits(const ALandscape* Landscape)
{
	check(Landscape);
	
	const FIntRect LandscapeExtent = Landscape->GetLandscapeInfo()->GetCompleteLandscapeExtent();
		
	return FVector2D(LandscapeExtent.Width() * Landscape->GetActorScale3D().X,
		LandscapeExtent.Height() * Landscape->GetActorScale3D().Y);
}
