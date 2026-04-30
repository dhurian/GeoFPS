#version 330 core

layout (location = 0) in vec3 aPosition;

out vec3 vDirection;

uniform mat4 uView;
uniform mat4 uProjection;

void main()
{
    vDirection  = aPosition;
    vec4 pos    = uProjection * uView * vec4(aPosition, 1.0);
    // Set z = w so that after perspective divide depth = 1.0 (far plane).
    // This means any real geometry (depth < 1.0) will occlude the sky.
    gl_Position = pos.xyww;
}
