#include "ImGuizmoExt.h"

#pragma warning(push)
#pragma warning(disable: 4505) // unreferenced function with internal linkage has been removed
#include "../Thirdparty/ImGuizmo/ImGuizmo.cpp"
#pragma warning(pop)

namespace ImGuizmo
{
	ImDrawList* GetDrawlist()
	{
		return gContext.mDrawList ? gContext.mDrawList : ImGui::GetWindowDrawList();
	}
}
