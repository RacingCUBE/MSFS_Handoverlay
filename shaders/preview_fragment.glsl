#version 330 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D uTexture;

void main()
{
    // Preview texture contains only the left camera (not stereo)
    // Sample it directly with no coordinate adjustment
    FragColor = texture(uTexture, TexCoord);
}
