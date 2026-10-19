// upscale.hlsl - Presents the game image (rendered by Direct3D 7) on a Direct3D 11 swap chain.
// Compiled at build time with fxc (see build.bat).

Texture2D    Image   : register(t0);
SamplerState Linear  : register(s0);
SamplerState Point   : register(s1);

cbuffer Params : register(b0)
{
    float2 TexSize;     // size of the game image in pixels
    float2 Scale;       // output pixels per image pixel
    int    Filter;      // 0 = nearest, 1 = bilinear, 2 = bicubic (Catmull-Rom), 3 = sharp bilinear
    float3 Pad;
};

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

// 9-tap Catmull-Rom filter using bilinear hardware filtering.
float4 CatmullRom(float2 uv)
{
    float2 samplePos = uv * TexSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;
    float2 f = samplePos - texPos1;
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;
    float2 p0 = (texPos1 - 1.0f) / TexSize;
    float2 p3 = (texPos1 + 2.0f) / TexSize;
    float2 p12 = (texPos1 + offset12) / TexSize;
    float4 r = 0.0f;
    r += Image.SampleLevel(Linear, float2(p0.x,  p0.y),  0) * w0.x  * w0.y;
    r += Image.SampleLevel(Linear, float2(p12.x, p0.y),  0) * w12.x * w0.y;
    r += Image.SampleLevel(Linear, float2(p3.x,  p0.y),  0) * w3.x  * w0.y;
    r += Image.SampleLevel(Linear, float2(p0.x,  p12.y), 0) * w0.x  * w12.y;
    r += Image.SampleLevel(Linear, float2(p12.x, p12.y), 0) * w12.x * w12.y;
    r += Image.SampleLevel(Linear, float2(p3.x,  p12.y), 0) * w3.x  * w12.y;
    r += Image.SampleLevel(Linear, float2(p0.x,  p3.y),  0) * w0.x  * w3.y;
    r += Image.SampleLevel(Linear, float2(p12.x, p3.y),  0) * w12.x * w3.y;
    r += Image.SampleLevel(Linear, float2(p3.x,  p3.y),  0) * w3.x  * w3.y;
    return saturate(r);
}

// Sharp bilinear: nearest neighbour inside a pixel, linear blending only at pixel borders.
float4 SharpBilinear(float2 uv)
{
    float2 texel = uv * TexSize;
    float2 base = floor(texel - 0.5f) + 0.5f;
    float2 f = saturate((texel - base - 0.5f) * Scale + 0.5f);
    return Image.SampleLevel(Linear, (base + f) / TexSize, 0);
}

float4 PSMain(VSOut i) : SV_Target
{
    float4 c;
    if (Filter == 0)      c = Image.SampleLevel(Point, i.uv, 0);
    else if (Filter == 1) c = Image.SampleLevel(Linear, i.uv, 0);
    else if (Filter == 3) c = SharpBilinear(i.uv);
    else                  c = CatmullRom(i.uv);
    return float4(c.rgb, 1.0f);
}
