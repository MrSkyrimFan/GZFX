            source += R"GZFX(    float3 bounce = indirectSum;
    bounce *= saturate(indirectNormalizer * 5.0);
    bounce = ApplySaturation(bounce, IndirectSaturation);

    output.ao = AoEnabled != 0 ? occlusion : 0.0;
    output.indirect = float4(
        IndirectEnabled != 0 ? bounce : 0.0,
        abs(center.z));

    return output;
}

struct FilterOutput
{
    float ao : SV_TARGET0;
    float4 indirect : SV_TARGET1;
};

FilterOutput DenoisePS(VSOutput input)
{
    FilterOutput output;
    float4 centerIndirect = IndirectInput.SampleLevel(
        PointSampler,
        input.uv,
        0);

    float centerDepth = centerIndirect.a;
    static const float weights[5] =
    {
        0.06136,
        0.24477,
        0.38774,
        0.24477,
        0.06136
    };

    float aoSum = 0.0;
    float3 indirectSum = 0.0;
    float weightSum = 0.0;

    [unroll]
    for (int offset = -2; offset <= 2; ++offset)
    {
        float2 sampleUv = saturate(
            input.uv +
            FilterAxis * HalfInvSize * (float)offset);

        float4 indirectSample = IndirectInput.SampleLevel(
            PointSampler,
            sampleUv,
            0);

        float aoSample = AoInput.SampleLevel(
            PointSampler,
            sampleUv,
            0);

        float relativeDepth =
            abs(indirectSample.a - centerDepth) /
            max(centerDepth, 1.0);

        float bilateral = exp2(-relativeDepth * 128.0);
        float weight = weights[offset + 2] * bilateral;

        aoSum += aoSample * weight;
        indirectSum += indirectSample.rgb * weight;
        weightSum += weight;
    }

    weightSum = max(weightSum, 1e-5);
    output.ao = aoSum / weightSum;
    output.indirect = float4(
        indirectSum / weightSum,
        centerDepth);
    return output;
}

float4 CompositePS(VSOutput input) : SV_TARGET
{
    float4 original = SceneColor.SampleLevel(
        LinearSampler,
        input.uv,
        0);

    if (Mode != 1 && Mode != 2)
    {
        return original;
    }

    if (Mode == 1 && input.uv.x < 0.5)
    {
        return original;
    }

    float fullDepth = DepthAt(input.uv);
    float fullViewDepth = fullDepth >= 0.99999
        ? 1e6
        : abs(ReconstructView(input.uv, fullDepth).z);

    static const float2 offsets[4] =
    {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),
        float2( 0.5,  0.5)
    };

    float aoSum = 0.0;
    float3 indirectSum = 0.0;
    float weightSum = 0.0;

    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        float2 sampleUv = saturate(
            input.uv + offsets[i] * HalfInvSize);

        float4 indirectSample = IndirectInput.SampleLevel(
            PointSampler,
            sampleUv,
            0);

        float aoSample = AoInput.SampleLevel(
            PointSampler,
            sampleUv,
            0);

        float relativeDepth =
            abs(indirectSample.a - fullViewDepth) /
            max(fullViewDepth, 1.0);

        float depthWeight = exp2(-relativeDepth * 160.0);
        float spatialWeight = 1.0 / (1.0 + dot(offsets[i], offsets[i]));
        float weight = max(depthWeight * spatialWeight, 1e-4);

        aoSum += aoSample * weight;
        indirectSum += indirectSample.rgb * weight;
        weightSum += weight;
    }

    weightSum = max(weightSum, 1e-4);
    float ao = aoSum / weightSum;
    float3 indirect = indirectSum / weightSum;
    float3 color = max(original.rgb, 0.0);

    float centerLuminance = Luminance(color);
    float highlightProtection = saturate(
        (centerLuminance - 0.75) / 3.25);

    float aoAmount =
        saturate(AoStrength) *
        lerp(1.0, 0.30, highlightProtection);

    float visibility = pow(
        saturate(1.0 - ao),
        aoAmount * 1.35);

    color *= visibility;

    if (IndirectEnabled != 0 && IndirectStrength > 0.0)
    {
        float bounceLuminance = Luminance(indirect);
        float maximumEnergy =
            0.02 +
            max(centerLuminance, 0.02) *
            IndirectEnergyLimit;

        float limiter = min(
            1.0,
            maximumEnergy /
            max(bounceLuminance, 1e-4));

        color += indirect * IndirectStrength * limiter;
    }

    return float4(color, original.a);
}
)GZFX";
