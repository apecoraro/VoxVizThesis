#version 330

uniform usampler2D InputTextureSampler;

layout(pixel_center_integer) in ivec4 gl_FragCoord;

out uvec4 Output;

void main()
{
    //the viewport for this fragment shader should be 
    //half the size of the InputTextureSampler's texture
    ivec2 texCoord = ivec2(gl_FragCoord.xy * 2);
    
    //for each pixel in the output image we
    //compute the sum of the four texels in 
    //the higher res mipmap image
    uint texelValue = texelFetch(InputTextureSampler, texCoord, 0).r;

    texCoord.x += 1;
    texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;

    texCoord.y += 1;
    texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
   
    texCoord.x -= 1;
    texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;

    texCoord.y += 1;
    ivec2 texSize = textureSize(InputTextureSampler, 0);
    if(texCoord.y == texSize.y-1)
    {
        texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
        texCoord.x += 1;
        texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
        texCoord.x += 1;
        if(texCoord.x == texSize.x-1)
        {
            texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
            texCoord.y -= 1;
            texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
            texCoord.y -= 1;
            texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
        }
    }
    else if((texCoord.x += 2) == texSize.x-1)
    {
        texCoord.y -= 1;
        texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
        texCoord.y -= 1;
        texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
    }

    Output = uvec4(texelValue, 0u, 0u, 1u);
}
