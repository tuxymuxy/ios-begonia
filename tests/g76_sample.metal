#include <metal_stdlib>
using namespace metal;
struct TriangleOutput { float4 position [[position]]; float2 uv; };
vertex TriangleOutput begonia_vertex(uint index [[vertex_id]]) {
    const float2 vertices[3] = {float2(-1,-1),float2(1,-1),float2(0,1)};
    TriangleOutput output;
    output.position=float4(vertices[index],0,1);
    output.uv=vertices[index]*0.5f+0.5f;
    return output;
}
fragment half4 begonia_fragment(TriangleOutput input [[stage_in]]) {
    return half4(half2(input.uv),0.25h,1.0h);
}
kernel void begonia_compute(device uint *output [[buffer(0)]],
                            uint index [[thread_position_in_grid]]) {
    output[index]=index*3u+7u;
}
