// Source: https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Model/Shaders/SkyboxPS.hlsl
// License: MIT
// Original author: Microsoft / Minigraph (James Stanard)
// Profile: ps_5_0
// Stage: pixel
// Notes: IBL skybox pixel shader; samples a TextureCube at a specified mip level for HDR radiance display
// verified: pending

//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):	James Stanard

#include "Common.hlsli"

cbuffer PSConstants : register(b0)
{
    float TextureLevel;
};

TextureCube<float3> radianceIBLTexture      : register(t10);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 viewDir : TEXCOORD3;
};

[RootSignature(Renderer_RootSig)]
float4 main(VSOutput vsOutput) : SV_Target0
{
    return float4(radianceIBLTexture.SampleLevel(defaultSampler, vsOutput.viewDir, TextureLevel), 1);
}
