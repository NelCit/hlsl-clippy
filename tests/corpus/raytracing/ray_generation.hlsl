// Source: https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample/RayGenerationShaderLib.hlsl
// License: MIT
// Original author: Microsoft
// Profile: lib_6_3
// Stage: raygen
// Notes: Primary ray generation shader; generates camera rays per pixel and calls TraceRay, writing to a screen UAV
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


[shader("raygeneration")]
void RayGen()
{
    float3 origin, direction;
    GenerateCameraRay(DispatchRaysIndex().xy, origin, direction);

    RayDesc rayDesc = { origin,
        0.0f,
        direction,
        FLT_MAX };
    RayPayload payload;
    payload.SkipShading = false;
    payload.RayHitT = FLT_MAX;
    TraceRay(g_accel, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0,0,1,0, rayDesc, payload);
}
