#version 330

uniform mat4 ProjectionMatrix;

in vec4 glVertex;

void main()
{
    gl_Position = ProjectionMatrix * glVertex;
    //gl_Position = glVertex;
}
