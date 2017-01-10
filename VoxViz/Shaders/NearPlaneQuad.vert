#version 330

uniform mat4 ProjectionMatrix;
uniform mat4 InverseModelViewMatrix;
uniform vec3 VolTranslation;
uniform vec3 VolScale;

in vec4 glVertex;
out vec4 RayPosition;

void main()
{
    //the near plane quad is already in view space
    gl_Position = ProjectionMatrix * glVertex;
    
    RayPosition = InverseModelViewMatrix * glVertex;
    RayPosition.xyz -= VolTranslation;
    RayPosition.xyz /= VolScale;
    RayPosition.xyz += 0.5f;
}
