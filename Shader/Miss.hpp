
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

[shader("miss")]
void Miss(inout RayPayload payload)
{
	payload.mData = 0.1;
}
