#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;

uniform vec3 uCameraPos;
uniform sampler2D uOverlayTexture;
uniform vec2 uOverlayOrigin;
uniform vec2 uOverlayAxisU;
uniform vec2 uOverlayAxisV;
uniform float uOverlayOpacity;
uniform int uUseOverlay;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform float uAmbientStrength;

out vec4 FragColor;

void main()
{
    vec3 lightDir = normalize(uSunDirection);
    float diffuse = max(dot(normalize(vNormal), lightDir), 0.0);
    float lighting = max((diffuse * uSunIntensity) + uAmbientStrength, 0.05);

    float heightTint = clamp(vWorldPos.y * 0.05 + 0.5, 0.0, 1.0);
    vec3 lowColor = vec3(0.18, 0.32, 0.16);
    vec3 highColor = vec3(0.45, 0.42, 0.35);
    vec3 baseColor = mix(lowColor, highColor, heightTint);
    vec3 finalColor = baseColor * lighting * uSunColor;

    if (uUseOverlay == 1)
    {
        vec2 relative = vWorldPos.xz - uOverlayOrigin;
        float determinant = uOverlayAxisU.x * uOverlayAxisV.y - uOverlayAxisU.y * uOverlayAxisV.x;
        if (abs(determinant) > 0.0001)
        {
            vec2 uv;
            uv.x = (relative.x * uOverlayAxisV.y - relative.y * uOverlayAxisV.x) / determinant;
            uv.y = (uOverlayAxisU.x * relative.y - uOverlayAxisU.y * relative.x) / determinant;

            if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0)
            {
                vec3 overlayColor = texture(uOverlayTexture, uv).rgb;
                finalColor = mix(finalColor, overlayColor * lighting * uSunColor, clamp(uOverlayOpacity, 0.0, 1.0));
            }
        }
    }

    FragColor = vec4(finalColor, 1.0);
}
