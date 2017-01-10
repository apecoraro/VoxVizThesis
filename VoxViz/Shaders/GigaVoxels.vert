#version 330

uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;
uniform vec3 VolTranslation;
uniform vec3 VolScale;

in vec4 glVertex;
out vec4 RayPosition;

void main()
{
    gl_Position = ProjectionMatrix * ModelViewMatrix * glVertex;
    
    vec3 rayPosition = glVertex.xyz - VolTranslation;
    rayPosition /= VolScale;
    rayPosition += 0.5f;
    RayPosition = vec4(rayPosition, 1);
}
