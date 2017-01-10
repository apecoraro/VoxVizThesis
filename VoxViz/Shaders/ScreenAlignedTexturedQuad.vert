#version 330

in vec4 glVertex;
out vec2 TexCoord;

void main()
{
    gl_Position = glVertex;
    
    TexCoord = vec2(glVertex * 0.5f + 0.5f);
}
