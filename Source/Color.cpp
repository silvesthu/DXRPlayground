#include "Color.h"

namespace Color
{
    const Illuminant Illuminant::D65 = { glm::vec2(0.3127, 0.3290) };    
    const RGBColorSpace RGBColorSpace::Rec709 = ConstructRGBColorSpace(
        { glm::vec2(0.64, 0.33) },
        { glm::vec2(0.3, 0.6) },
        { glm::vec2(0.15, 0.06) },
        Illuminant::D65);

    RGBColorSpace ConstructRGBColorSpace(const CIExyY& inR, const CIExyY& inG, const CIExyY& inB, const Illuminant& inWhitePoint)
    {
        RGBColorSpace rgb_color_space;
        rgb_color_space.mR = inR;
        rgb_color_space.mG = inG;
        rgb_color_space.mB = inB;
        rgb_color_space.mWhitePoint = inWhitePoint;

        CIEXYZ R = xyYToXYZ(inR);
        CIEXYZ G = xyYToXYZ(inG);
        CIEXYZ B = xyYToXYZ(inB);
        CIEXYZ W = xyYToXYZ(inWhitePoint.mData);

        glm::dmat3x3 rgb = glm::mat3x3(R.mData, G.mData, B.mData);
        CIEXYZ C = { glm::inverse(rgb) * W.mData };
        glm::dmat3x3 diagnal = glm::mat3x3(0.0f);
        diagnal[0][0] = C.mData.x;
        diagnal[1][1] = C.mData.y;
        diagnal[2][2] = C.mData.z;
        rgb_color_space.mRGBToXYZ = rgb * diagnal;
        rgb_color_space.mXYZToRGB = glm::inverse(rgb_color_space.mRGBToXYZ);

        return rgb_color_space;
    }

    CIEXYZ xyYToXYZ(const CIExyY& inxyY, double inY)
    {
        if (inxyY.mData.y == 0)
            return { glm::vec3(0,0,0) };
        return CIEXYZ(glm::vec3(inxyY.mData.x * inY / inxyY.mData.y, inY, (1 - inxyY.mData.x - inxyY.mData.y) * inY / inxyY.mData.y));
    }

    CIEXYZ SpectrumToXYZ(const Spectrum& inSpectrum, bool inNormalize)
    {
        CIEXYZ xyz;
        for (int i = 0; i < LambdaCount; i++)
        {
            xyz.mData.x += CIE1931::X[i] * inSpectrum.mEnergy[i];
            xyz.mData.y += CIE1931::Y[i] * inSpectrum.mEnergy[i];
            xyz.mData.z += CIE1931::Z[i] * inSpectrum.mEnergy[i];
        }

        if (inNormalize)
        {
            xyz.mData.x /= CIE1931::YIntegral;
            xyz.mData.y /= CIE1931::YIntegral;
            xyz.mData.z /= CIE1931::YIntegral;
        }
        
        return xyz;
    }

    RGB XYZToRGB(const CIEXYZ& inXYZ, const RGBColorSpace& inRGBColorSpace)
    {
        return { inRGBColorSpace.mXYZToRGB * inXYZ.mData };
    }

    CIEXYZ XYZToRGB(const RGB& inRGB, const RGBColorSpace& inRGBColorSpace)
    {
        return { inRGBColorSpace.mRGBToXYZ * inRGB.mData };
    }
}
