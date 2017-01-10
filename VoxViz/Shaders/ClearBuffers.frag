#version 330

uniform vec4 ClearColor;
uniform float ClearDepth;

out vec4 FragColor;
out float gl_FragDepth;

void main()
{
    FragColor = vec4(1.0f, 0.0f, 0.0f, 1.0f);
    gl_FragDepth = 1.0f;
}
