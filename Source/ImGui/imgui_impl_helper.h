#pragma once

#include "../Thirdparty/imgui/imgui.h"
#include "../Thirdparty/nameof/include/nameof.hpp"

namespace ImGui
{
	template <typename T>
	bool EnumRadioButton(const T& inEnum, T* inPointer)
	{
		return ImGui::RadioButton(nameof::nameof_enum(inEnum).data(), (int*)inPointer, (int)inEnum);
	}

	inline bool InputDouble3(const char* label, double v[3], const char* format = "%.3f", ImGuiInputTextFlags flags = 0)
	{
		flags |= ImGuiInputTextFlags_CharsScientific;
		return InputScalarN(label, ImGuiDataType_Double, v, 3, NULL, NULL, format, flags);
	}

	inline bool SliderDouble(const char* label, double* v, double v_min, double v_max, const char* format = "%.3f")
	{
		return SliderScalar(label, ImGuiDataType_Double, v, &v_min, &v_max, format);
	}

	inline bool SliderDouble3(const char* label, double* v, double v_min, double v_max, const char* format = "%.3f")
	{
		return SliderScalarN(label, ImGuiDataType_Double, v, 3, &v_min, &v_max, format);
	}

	inline bool SliderUint(const char* label, unsigned int* v, unsigned int v_min, unsigned int v_max, const char* format = "%d")
	{
		return SliderScalar(label, ImGuiDataType_U32, v, &v_min, &v_max, format);
	}
}
