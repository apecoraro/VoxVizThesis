#version 330

uniform usampler2D SelectionMaskSampler;

layout(pixel_center_integer) in ivec4 gl_FragCoord;

out uvec4 Output;

void main()
{
    ivec2 texCoord = ivec2(gl_FragCoord.xy);

    //red component contains a bit vector, set bits indicate
    //that an item in the corresponding node list should be kept
    //if there are not set bits then this pixel is not active
    //this will be true for any pixels/rays that did not hit the volume
    //or for pixels that had the same nodes in their list as its neighbor
    //pixels (see GenerateSelectionMask.frag)
    uint selectionBits = texelFetch(SelectionMaskSampler, texCoord, 0).r;

    if(selectionBits == 0u)
        Output = uvec4(0u, 0u, 0u, 0u);
    else
        Output = uvec4(1u, 0u, 0u, 1u);
}
