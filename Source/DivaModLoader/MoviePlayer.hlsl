#if defined (__INTELLISENSE__)
#define BT709
#define FULL
#endif

Texture2D<float> luminance : register(t0);
Texture2D<float2> chrominance : register(t1);
SamplerState Sampler : register(s0);

#if defined(BT601)
static const float Kb = 0.114;
static const float Kr = 0.299;
static const float Kg = 1 - Kb - Kr;
#elif defined(BT709)
static const float Kb = 0.0722;
static const float Kr = 0.2126;
static const float Kg = 1 - Kb - Kr;
#elif defined(BT2020)
static const float Kb = 0.0593;
static const float Kr = 0.2627;
static const float Kg = 1 - Kb - Kr;
#endif

static const float3x3 YCbCrRgbMatrix =
{
    1, 0, 2 - 2 * Kr,
    1, -(Kb / Kg) * (2 - 2 * Kb), -(Kr / Kg) * (2 - 2 * Kr),
    1, 2 - 2 * Kb, 0
};

void vs(in uint vertexId : SV_VertexID, out float4 position : SV_Position, out float2 texCoord : TEXCOORD)
{
    texCoord = float2((vertexId << 1) & 2, vertexId & 2);
    position = float4(texCoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

void ps(in float4 position : SV_Position, in float2 texCoord : TEXCOORD, out float4 color : SV_TARGET)
{
    float Y = luminance.SampleLevel(Sampler, texCoord, 0);
    float2 CbCr = chrominance.SampleLevel(Sampler, texCoord, 0);

#if defined(FULL)
    CbCr = CbCr - (128.0 / 255.0);
#elif defined (LIMITED)
    Y = Y * (255.0 / 219.0) - (16.0 / 219.0);
    CbCr = CbCr * (255.0 / 224.0) - (128.0 / 224.0);
#endif

    float3 RGB = mul(YCbCrRgbMatrix, float3(Y, CbCr));
    color = float4(RGB, 1.0);
}