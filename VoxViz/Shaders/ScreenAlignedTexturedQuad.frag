#version 330

uniform sampler2D ColorSampler;

in vec2 TexCoord;
out vec4 FragColor;

void main()
{
    vec4 color = texture2D(ColorSampler, TexCoord);

    FragColor = color;
}
