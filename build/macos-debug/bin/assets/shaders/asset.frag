#version 330 core

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vUV;

uniform vec3 uCameraPos;
uniform vec3 uTintColor;
uniform vec4 uBaseColorFactor;
uniform float uMetallicFactor;
uniform float uRoughnessFactor;
uniform vec3 uEmissiveFactor;
uniform float uAlphaCutoff;
uniform int uUseBaseColorTexture;
uniform int uUseMetallicRoughnessTexture;
uniform int uUseNormalTexture;
uniform int uUseEmissiveTexture;
uniform int uAlphaMode;
uniform sampler2D uBaseColorTexture;
uniform sampler2D uMetallicRoughnessTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uEmissiveTexture;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform float uAmbientStrength;

out vec4 FragColor;

void main()
{
    vec3 normal = normalize(vWorldNormal);
    if (uUseNormalTexture == 1)
    {
        vec3 sampledNormal = texture(uNormalTexture, vUV).rgb * 2.0 - 1.0;
        normal = normalize(mix(normal, normalize(sampledNormal), 0.35));
    }
    vec3 lightDirection = normalize(uSunDirection);
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float lighting = max((diffuse * uSunIntensity) + uAmbientStrength, 0.05);

    vec3 viewDirection = normalize(uCameraPos - vWorldPosition);
    vec3 halfVector = normalize(lightDirection + viewDirection);
    float specular = pow(max(dot(normal, halfVector), 0.0), 24.0);

    vec4 textureColor = uUseBaseColorTexture == 1 ? texture(uBaseColorTexture, vUV) : vec4(1.0);
    float alpha = textureColor.a * uBaseColorFactor.a;
    if (uAlphaMode == 1 && alpha < uAlphaCutoff)
    {
        discard;
    }

    vec4 metallicRoughness = uUseMetallicRoughnessTexture == 1 ? texture(uMetallicRoughnessTexture, vUV) : vec4(1.0);
    float roughness = clamp(uRoughnessFactor * metallicRoughness.g, 0.04, 1.0);
    float metallic = clamp(uMetallicFactor * metallicRoughness.b, 0.0, 1.0);
    vec3 emissive = uEmissiveFactor * (uUseEmissiveTexture == 1 ? texture(uEmissiveTexture, vUV).rgb : vec3(1.0));

    vec3 albedo = textureColor.rgb * uBaseColorFactor.rgb * uTintColor;
    vec3 dielectric = albedo * lighting * uSunColor;
    vec3 metalTint = mix(vec3(1.0), albedo, metallic);
    vec3 specularColor = metalTint * specular * mix(0.04, 0.35, metallic) * (1.0 - roughness) * uSunIntensity;
    vec3 color = mix(dielectric, albedo * max(diffuse, uAmbientStrength), metallic * 0.45) + specularColor + emissive;
    FragColor = vec4(color, uAlphaMode == 2 ? alpha : 1.0);
}
