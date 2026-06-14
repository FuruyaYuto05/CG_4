#include "Object3d.hlsli"


struct Material
{
    float32_t4 color;
};
ConstantBuffer<Material> gMaterial : register(b0);

struct Camera
{
    float32_t3 worldPosition;
};

ConstantBuffer<Camera> gCamera : register(b1);

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);
TextureCube<float32_t4> gEnvironmentTexture : register(t1);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float32_t4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    output.color = gMaterial.color * textureColor;

    float32_t3 cameraToPosition =
        normalize(input.worldPosition - gCamera.worldPosition);

    float32_t3 reflectedVector =
        reflect(cameraToPosition, normalize(input.normal));

    float32_t4 environmentColor =
        gEnvironmentTexture.Sample(gSampler, reflectedVector);

    output.color.rgb += environmentColor.rgb;

    return output;
}