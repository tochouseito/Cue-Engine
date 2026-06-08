// 初期検証用の単純 forward shader。
// GPU-driven static mesh path 以前の mesh 表示確認に使う最小描画で、
// position から疑似色を作るだけで material/light は扱わない。

struct VsInput
{
    float4 position : POSITION;
};

struct VsOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR0;
};

VsOutput vs_main(VsInput input)
{
    VsOutput output;
    output.position = float4(input.position.xyz, 1.0f);
    output.color = abs(input.position.xyz);
    return output;
}

float4 ps_main(VsOutput input) : SV_Target0
{
    return float4(saturate(input.color + float3(0.15f, 0.2f, 0.25f)), 1.0f);
}
