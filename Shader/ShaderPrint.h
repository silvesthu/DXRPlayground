#pragma once

// NOTE: UE's ShaderPrint parse shader source and generate string table, see ShaderPreprocessor.cpp
// NOTE: https://therealmjp.github.io/posts/hlsl-printf. StrLen need to be expanded, see https://github.com/TheRealMJP/EarlyZTest/blob/main/SampleFramework12/v1.04/Shaders/ShaderDebug.hlsl

#include "HLSLHelper.h"
#include "Binding.h"
#include "Context.h"

namespace ShaderPrint
{
	static bool sEnabled;

	void Initialize(PixelContext ioPixelContext)
	{
		sEnabled = false;
#if SHADER_DEBUG
		sEnabled = !ioPixelContext.mOutputDepth &&
			ioPixelContext.mPixelIndex.x == mConstants.mPixelDebugCoord.x && 
			ioPixelContext.mPixelIndex.y == mConstants.mPixelDebugCoord.y;
#endif // SHADER_DEBUG
	}

	uint _Allocate(int inUIntCount)
	{
		USING_RESOURCE(RWStructuredBuffer<uint>, ShaderPrintUAV);

		if (!sEnabled) { return 0; }

		uint offset = 0;
#if SHADER_DEBUG
		InterlockedAdd(ShaderPrintUAV[0], inUIntCount, offset);
#endif // SHADER_DEBUG
		return offset;
	}

	uint _MakeEntryHeader(uint inType, uint inOption)
	{
		return (inType & 0xffff) | (inOption << 16);
	}

	void _PrintFloat(float4 inValue, uint inN, uint inOption)
	{
		USING_RESOURCE(RWStructuredBuffer<uint>, ShaderPrintUAV);

		if (!sEnabled) { return; }
		uint offset = _Allocate(1 + inN);
		ShaderPrintUAV[offset] = _MakeEntryHeader((uint)ShaderPrintEntryType::Float1 + inN - 1, inOption);
		for (uint i = 0; i < inN; i++)
		{
			ShaderPrintUAV[offset + 1 + i] = asuint(inValue[i]);
		}
	}

	void _PrintUInt(uint4 inValue, uint inN, uint inOption)
	{
		USING_RESOURCE(RWStructuredBuffer<uint>, ShaderPrintUAV);

		if (!sEnabled) { return; }
		uint offset = _Allocate(1 + inN);
		ShaderPrintUAV[offset] = _MakeEntryHeader((uint)ShaderPrintEntryType::UInt1 + inN - 1, inOption);
		for (uint i = 0; i < inN; i++)
		{
			ShaderPrintUAV[offset + 1 + i] = inValue[i];
		}
	}

	template<typename T> uint CharToUint(in T c)
	{
        if (c == '\t')
            return 9;
        if (c == '\n')
            return 10;
        if (c == ' ')
            return 32;
        if (c == '!')
            return 33;
        if (c == '\"')
            return 34;
        if (c == '#')
            return 35;
        if (c == '$')
            return 36;
        if (c == '%')
            return 37;
        if (c == '&')
            return 38;
        if (c == '\'')
            return 39;
        if (c == '(')
            return 40;
        if (c == ')')
            return 41;
        if (c == '*')
            return 42;
        if (c == '+')
            return 43;
        if (c == ',')
            return 44;
        if (c == '-')
            return 45;
        if (c == '.')
            return 46;
        if (c == '/')
            return 47;
        if (c == '0')
            return 48;
        if (c == '1')
            return 49;
        if (c == '2')
            return 50;
        if (c == '3')
            return 51;
        if (c == '4')
            return 52;
        if (c == '5')
            return 53;
        if (c == '6')
            return 54;
        if (c == '7')
            return 55;
        if (c == '8')
            return 56;
        if (c == '9')
            return 57;
        if (c == ':')
            return 58;
        if (c == ';')
            return 59;
        if (c == '<')
            return 60;
        if (c == '=')
            return 61;
        if (c == '>')
            return 62;
        if (c == '?')
            return 63;
        if (c == '@')
            return 64;
        if (c == 'A')
            return 65;
        if (c == 'B')
            return 66;
        if (c == 'C')
            return 67;
        if (c == 'D')
            return 68;
        if (c == 'E')
            return 69;
        if (c == 'F')
            return 70;
        if (c == 'G')
            return 71;
        if (c == 'H')
            return 72;
        if (c == 'I')
            return 73;
        if (c == 'J')
            return 74;
        if (c == 'K')
            return 75;
        if (c == 'L')
            return 76;
        if (c == 'M')
            return 77;
        if (c == 'N')
            return 78;
        if (c == 'O')
            return 79;
        if (c == 'P')
            return 80;
        if (c == 'Q')
            return 81;
        if (c == 'R')
            return 82;
        if (c == 'S')
            return 83;
        if (c == 'T')
            return 84;
        if (c == 'U')
            return 85;
        if (c == 'V')
            return 86;
        if (c == 'W')
            return 87;
        if (c == 'X')
            return 88;
        if (c == 'Y')
            return 89;
        if (c == 'Z')
            return 90;
        if (c == '[')
            return 91;
        if (c == '\\')
            return 92;
        if (c == ']')
            return 93;
        if (c == '^')
            return 94;
        if (c == '_')
            return 95;
        if (c == '`')
            return 96;
        if (c == 'a')
            return 97;
        if (c == 'b')
            return 98;
        if (c == 'c')
            return 99;
        if (c == 'd')
            return 100;
        if (c == 'e')
            return 101;
        if (c == 'f')
            return 102;
        if (c == 'g')
            return 103;
        if (c == 'h')
            return 104;
        if (c == 'i')
            return 105;
        if (c == 'j')
            return 106;
        if (c == 'k')
            return 107;
        if (c == 'l')
            return 108;
        if (c == 'm')
            return 109;
        if (c == 'n')
            return 110;
        if (c == 'o')
            return 111;
        if (c == 'p')
            return 112;
        if (c == 'q')
            return 113;
        if (c == 'r')
            return 114;
        if (c == 's')
            return 115;
        if (c == 't')
            return 116;
        if (c == 'u')
            return 117;
        if (c == 'v')
            return 118;
        if (c == 'w')
            return 119;
        if (c == 'x')
            return 120;
        if (c == 'y')
            return 121;
        if (c == 'z')
            return 122;
        if (c == '{')
            return 123;
        if (c == '|')
            return 124;
        if (c == '}')
            return 125;
        if (c == '~')
            return 126;
        return 0;
	}
};

void Print(float inValue, bool inNewLine = true) { ShaderPrint::_PrintFloat(inValue.xxxx, 1, inNewLine ? 1 : 0); }
void Print(float2 inValue, bool inNewLine = true) { ShaderPrint::_PrintFloat(inValue.xyxx, 2, inNewLine ? 1 : 0); }
void Print(float3 inValue, bool inNewLine = true) { ShaderPrint::_PrintFloat(inValue.xyzx, 3, inNewLine ? 1 : 0); }
void Print(float4 inValue, bool inNewLine = true) { ShaderPrint::_PrintFloat(inValue.xyzw, 4, inNewLine ? 1 : 0); }
void Print(uint inValue, bool inNewLine = true) { ShaderPrint::_PrintUInt(inValue.xxxx, 1, inNewLine ? 1 : 0); }
void Print(uint2 inValue, bool inNewLine = true) { ShaderPrint::_PrintUInt(inValue.xyxx, 2, inNewLine ? 1 : 0); }
void Print(uint3 inValue, bool inNewLine = true) { ShaderPrint::_PrintUInt(inValue.xyzx, 3, inNewLine ? 1 : 0); }
void Print(uint4 inValue, bool inNewLine = true) { ShaderPrint::_PrintUInt(inValue.xyzw, 4, inNewLine ? 1 : 0); }

#define PrintString(inString)																                \
if (ShaderPrint::sEnabled)																					\
{																											\
	USING_RESOURCE(RWStructuredBuffer<uint>, ShaderPrintUAV);												\
	uint byte_count = 0;																					\
	for (;; byte_count++)																					\
	{																										\
		if (inString[byte_count] == "\0"[0]) break;															\
	}																										\
	uint uint_count = (byte_count + 3) / 4;																	\
	uint offset = ShaderPrint::_Allocate(1 + uint_count);													\
	ShaderPrintUAV[offset] = ShaderPrint::_MakeEntryHeader((uint)ShaderPrintEntryType::String, byte_count);	\
	for (uint uint_index = 0; uint_index < uint_count; uint_index++)                                        \
	{                                                                                                       \
		uint data = 0;                                                                                      \
        for (uint local_byte_index = 0; local_byte_index < 4; local_byte_index++)                           \
        {                                                                                                   \
            uint byte_index = uint_index * 4 + local_byte_index;                                            \
            if (byte_index >= byte_count) { break; }                                                        \
            data |= (ShaderPrint::CharToUint(inString[byte_index]) << (local_byte_index * 8));              \
        }                                                                                                   \
        ShaderPrintUAV[offset + 1 + uint_index] = data;                                                     \
	}                                                                                                       \
}

#define PrintNewLine() PrintString("\n")
#define PrintNameValueLine(inString, inValue) PrintString(inString); Print(inValue);
