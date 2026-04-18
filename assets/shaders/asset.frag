#version 330 core

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vUV;

uniform vec3 uCameraPos;
uniform vec3 uTintColor;
uniform vec4 uBaseColorFactor;
uniform int uUseBaseColorTexture;
uniform sampler2D uBaseColorTexture;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform float uAmbientStrength;

out vec4 FragColor;

void main()
{
    vec3 normal = normalize(vWorldNormal);
    vec3 lightDirection = normalize(uSunDirection);
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float lighting = max((diffuse * uSunIntensity) + uAmbientStrength, 0.05);

    vec3 viewDirection = normalize(uCameraPos - vWorldPosition);
    vec3 halfVector = normalize(lightDirection + viewDirection);
    float specular = pow(max(dot(normal, halfVector), 0.0), 24.0);

    vec4 textureColor = uUseBaseColorTexture == 1 ? texture(uBaseColorTexture, vUV) : vec4(1.0);
    vec3 albedo = textureColor.rgb * uBaseColorFactor.rgb * uTintColor;
    vec3 color = (albedo * lighting * uSunColor) + (vec3(specular * 0.15) * uSunIntensity);
    FragColor = vec4(color, textureColor.a * uBaseColorFactor.a);
}
