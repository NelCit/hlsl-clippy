// Source: https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample/missShaderLib.hlsl
// License: MIT
// Original author: Microsoft
// Profile: lib_6_3
// Stage: miss
// Notes: Miss shader for primary and shadow rays; writes black background for non-reflection rays
// verified: pending

//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#define HLSL
#include "ModelViewerRaytracing.h"

[shader("miss")]
void Miss(inout RayPayload payload)
{
    if (!payload.SkipShading && !IsReflection)
    {
        g_screenOutput[DispatchRaysIndex().xy] = float4(0, 0, 0, 1);
    }
}
