//
// RT64
//

#pragma once

#include "rt64_hlsl.h"

#ifdef HLSL_CPU
namespace interop {
#endif
    struct RDPParams {
        float4 primColor;
        float2 primLOD;
        float2 primDepth;
        float4 envColor;
        float4 fogColor;
        float4 blendColor;
        float3 keyCenter;
        float keyCenterPadding;
        float3 keyScale;
        int convertK[6];
        int padding0;
        int padding1;
        int padding2;
    };
#ifdef HLSL_CPU
};
#endif