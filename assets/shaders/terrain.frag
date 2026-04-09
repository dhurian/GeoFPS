#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;

uniform vec3 uCameraPos;

out vec4 FragColor;

void main()
{
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.35));
    float diffuse = max(dot(normalize(vNormal), lightDir), 0.15);

    float heightTint = clamp(vWorldPos.y * 0.05 + 0.5, 0.0, 1.0);
    vec3 lowColor = vec3(0.18, 0.32, 0.16);
    vec3 highColor = vec3(0.45, 0.42, 0.35);
    vec3 baseColor = mix(lowColor, highColor, heightTint);

    FragColor = vec4(baseColor * diffuse, 1.0);
}
