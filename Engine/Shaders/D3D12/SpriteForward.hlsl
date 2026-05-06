struct SpriteInstance
{
    float4 positionSize;
    float4 uvRect;
    float4 color;
    uint textureId;
    uint useTexture;
    float rotation;
    uint padding0;
    float2 pivot;
    float2 padding1;
};

struct VsOut
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint textureId : TEXCOORD1;
    nointerpolation uint useTexture : TEXCOORD2;
};

StructuredBuffer<SpriteInstance> g_sprites : register(t0);
Texture2D<float4> g_textures[] : register(t0, space1);

static const float2 k_corners[6] =
{
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f),
};

VsOut vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const SpriteInstance sprite = g_sprites[instanceId];
    const float2 corner = k_corners[vertexId];
    const float2 local = (corner - sprite.pivot) * sprite.positionSize.zw;

    float sineValue = 0.0f;
    float cosineValue = 1.0f;
    sincos(sprite.rotation, sineValue, cosineValue);

    const float2 rotated = float2(
        local.x * cosineValue - local.y * sineValue,
        local.x * sineValue + local.y * cosineValue);
    const float2 position =
        sprite.positionSize.xy + float2(rotated.x, -rotated.y);

    VsOut output;
    output.position = float4(position, 0.0f, 1.0f);
    output.texcoord = sprite.uvRect.xy + corner * sprite.uvRect.zw;
    output.color = sprite.color;
    output.textureId = sprite.textureId;
    output.useTexture = sprite.useTexture;
    return output;
}

float4 ps_main(VsOut input) : SV_Target0
{
    if (input.useTexture == 0)
    {
        return input.color;
    }

    const uint textureIndex = NonUniformResourceIndex(input.textureId);
    uint textureWidth = 1;
    uint textureHeight = 1;
    g_textures[textureIndex].GetDimensions(textureWidth, textureHeight);

    const float2 uv = saturate(input.texcoord);
    const uint2 texelCoord = uint2(
        min((uint)(uv.x * textureWidth), textureWidth - 1),
        min((uint)(uv.y * textureHeight), textureHeight - 1));
    const float4 textureColor =
        g_textures[textureIndex].Load(int3(texelCoord, 0));
    return textureColor * input.color;
}
