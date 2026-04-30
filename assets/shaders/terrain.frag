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
uniform int uOverlayOnly;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform float uAmbientStrength;
uniform int uColorByHeight;
uniform float uHeightColorMin;
uniform float uHeightColorMax;
uniform vec3 uLowHeightColor;
uniform vec3 uMidHeightColor;
uniform vec3 uHighHeightColor;

out vec4 FragColor;

void main()
{
    vec3 lightDir = normalize(uSunDirection);
    vec3 normal = normalize(vNormal);
    float slope = clamp(normal.y, 0.0, 1.0);
    float directional = clamp((dot(normal, lightDir) * 0.5) + 0.5, 0.0, 1.0);
    float hillshade = mix(0.72, 1.0, directional) * mix(0.82, 1.0, slope);
    float lighting = mix(1.0, hillshade, 0.42);
    lighting = max(lighting + (uAmbientStrength * 0.18), 0.72);

    float heightSpan = max(uHeightColorMax - uHeightColorMin, 1.0);
    float heightTint = clamp((vWorldPos.y - uHeightColorMin) / heightSpan, 0.0, 1.0);
    vec3 heightColor = heightTint < 0.5 ?
        mix(uLowHeightColor, uMidHeightColor, heightTint * 2.0) :
        mix(uMidHeightColor, uHighHeightColor, (heightTint - 0.5) * 2.0);
    vec3 baseColor = uColorByHeight == 1 ? heightColor : uMidHeightColor;
    vec3 litColor = baseColor * lighting * mix(vec3(1.0), uSunColor, 0.18);
    vec3 finalColor = uColorByHeight == 1 ? mix(baseColor, litColor, 0.55) : litColor;

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
                vec3 overlayColor = max(texture(uOverlayTexture, uv).rgb, vec3(0.08));
                vec3 shadedOverlay = overlayColor * mix(vec3(1.0), vec3(lighting), 0.55);
                if (uOverlayOnly == 1)
                {
                    FragColor = vec4(shadedOverlay, clamp(uOverlayOpacity, 0.0, 1.0));
                    return;
                }
                finalColor = mix(finalColor, shadedOverlay, clamp(uOverlayOpacity, 0.0, 1.0));
            }
        }
    }

    if (uOverlayOnly == 1)
    {
        discard;
    }

    FragColor = vec4(finalColor, 1.0);
}
