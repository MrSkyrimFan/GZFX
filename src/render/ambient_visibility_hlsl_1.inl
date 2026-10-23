            source += R"GZFX(
#define GZFX_PI 3.14159265358979323846
#define GZFX_HALF_PI 1.57079632679489661923

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOutput output;
    output.uv = uv;
    output.position = float4(
        uv * float2(2.0, -2.0) + float2(-1.0, 1.0),
        0.0,
        1.0);
    return output;
}

cbuffer GZFXAmbientVisibilityParameters : register(b0)
{
    float2 FullInvSize;
    float2 HalfInvSize;

    float AoStrength;
    float AoRadius;
    float IndirectStrength;
    float IndirectRadius;

    float IndirectSaturation;
    uint Quality;
    uint Mode;
    uint AoEnabled;

    uint IndirectEnabled;
    float AoBias;
    float AoPower;
    float IndirectEnergyLimit;

    float2 FilterAxis;
    float2 ParameterPadding;
};

cbuffer SkyrimPerFrame : register(b12)
{
    row_major float4x4 CameraView : packoffset(c0);
    row_major float4x4 CameraProj : packoffset(c4);
    row_major float4x4 CameraViewProj : packoffset(c8);
    row_major float4x4 CameraViewProjUnjittered : packoffset(c12);
    row_major float4x4 CameraPreviousViewProjUnjittered : packoffset(c16);
    row_major float4x4 CameraProjUnjittered : packoffset(c20);
    row_major float4x4 CameraProjUnjitteredInverse : packoffset(c24);
    row_major float4x4 CameraViewInverse : packoffset(c28);
    row_major float4x4 CameraViewProjInverse : packoffset(c32);
    row_major float4x4 CameraProjInverse : packoffset(c36);
    float4 CameraPosAdjust : packoffset(c40);
    float4 CameraPreviousPosAdjust : packoffset(c41);
    float4 FrameParams : packoffset(c42);
    float4 DynamicResolutionParams1 : packoffset(c43);
    float4 DynamicResolutionParams2 : packoffset(c44);
};

Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SceneDepth : register(t1);
Texture2D<float> AoInput : register(t2);
Texture2D<float4> IndirectInput : register(t3);

SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);

float2 DepthUv(float2 uv)
{
    return saturate(uv * DynamicResolutionParams1.xy);
}

float DepthAt(float2 uv)
{
    return SceneDepth.SampleLevel(
        PointSampler,
        DepthUv(saturate(uv)),
        0).x;
}

float3 ReconstructView(float2 uv, float depth)
{
    float4 clip = float4(
        2.0 * float2(uv.x, 1.0 - uv.y) - 1.0,
        depth,
        1.0);

    float4 view = mul(CameraProjInverse, clip);
    return view.xyz / max(abs(view.w), 1e-6);
}

float3 PositionAt(float2 uv)
{
    return ReconstructView(saturate(uv), DepthAt(uv));
}

float3 ReconstructNormal(float2 uv, float centerDepth)
{
    float3 center = ReconstructView(uv, centerDepth);
    float3 left = PositionAt(uv - float2(FullInvSize.x, 0.0));
    float3 right = PositionAt(uv + float2(FullInvSize.x, 0.0));
    float3 top = PositionAt(uv - float2(0.0, FullInvSize.y));
    float3 bottom = PositionAt(uv + float2(0.0, FullInvSize.y));

    float3 leftDelta = center - left;
    float3 rightDelta = right - center;
    float3 topDelta = center - top;
    float3 bottomDelta = bottom - center;

    float3 dx = dot(leftDelta, leftDelta) < dot(rightDelta, rightDelta)
        ? leftDelta
        : rightDelta;

    float3 dy = dot(topDelta, topDelta) < dot(bottomDelta, bottomDelta)
        ? topDelta
        : bottomDelta;

    float3 normal = normalize(cross(dy, dx));
    if (dot(normal, -center) < 0.0)
    {
        normal = -normal;
    }

    return normal;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 ApplySaturation(float3 color, float saturation)
{
    return lerp(Luminance(color).xxx, color, saturation);
}

float2 Hash22(float2 value)
{
    float3 p3 = frac(float3(value.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

uint SliceCount()
{
    if (Quality == 0) return 2;
    if (Quality == 1) return 3;
    if (Quality == 2) return 4;
    return 6;
}

uint StepCount()
{
    if (Quality == 0) return 3;
    if (Quality == 1) return 3;
    return 4;
}

struct AnalyzeOutput
{
    float ao : SV_TARGET0;
    float4 indirect : SV_TARGET1;
};

float IntegrateArc(float horizon, float normalAngle, float cosNormal)
{
    return (
        cosNormal +
        2.0 * horizon * sin(normalAngle) -
        cos(2.0 * horizon - normalAngle)
    ) * 0.25;
}

AnalyzeOutput AnalyzePS(VSOutput input)
{
    AnalyzeOutput output;
    float centerDepth = DepthAt(input.uv);

    if (centerDepth >= 0.99999)
    {
        output.ao = 0.0;
        output.indirect = float4(0.0, 0.0, 0.0, 1e6);
        return output;
    }

    float3 center = ReconstructView(input.uv, centerDepth);
    float3 viewVector = normalize(-center);
    float3 normal = ReconstructNormal(input.uv, centerDepth);
    float3 centerColor = max(
        SceneColor.SampleLevel(LinearSampler, input.uv, 0).rgb,
        0.0);

    float3 pixelRight = PositionAt(input.uv + float2(FullInvSize.x, 0.0));
    float3 pixelDown = PositionAt(input.uv + float2(0.0, FullInvSize.y));
    float viewUnitsPerPixel = max(
        length(pixelRight - center),
        length(pixelDown - center));

    // A neighboring sample must rise measurably above the receiver's
    // tangent plane before it may contribute to the horizon. This rejects
    // constant-depth planes and prevents smooth terrain slopes from being
    // treated as universal ambient occluders.
    float receiverPlaneBias = max(
        viewUnitsPerPixel * 0.75,
        abs(center.z) * 0.00015);

)GZFX";
