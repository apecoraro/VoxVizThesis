#version 330

uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;
uniform mat4 WorldTransform;
uniform sampler1D ColorLookupSampler;
uniform int ColorIndex;

in vec4 glVertex;
out vec4 VtxColor;

void main()
{
    vec4 worldVtx = WorldTransform * glVertex;
    gl_Position = ProjectionMatrix * ModelViewMatrix * worldVtx;
    VtxColor = texelFetch(ColorLookupSampler, ColorIndex, 0);
}
