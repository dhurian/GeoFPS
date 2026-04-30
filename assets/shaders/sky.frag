#version 330 core

in  vec3 vDirection;
out vec4 FragColor;

// --- Sky gradient ---
uniform vec3  uZenithColor;       // sky color at the top
uniform vec3  uHorizonColor;      // sky color at the horizon
uniform float uHorizonSharpness;  // gradient exponent — higher = sharper horizon band

// --- Sun ---
uniform vec3  uSunDirection;      // normalized sun direction (world space)
uniform vec3  uSunColor;          // sun light color
uniform float uSunDiskSize;       // dot-product threshold for sun disk (0 = disabled)
uniform float uSunDiskIntensity;  // brightness multiplier applied to the sun disk

// --- Clouds ---
uniform int   uCloudsEnabled;    // 1 = render clouds, 0 = skip
uniform float uCloudCoverage;    // 0 = clear, 1 = fully overcast
uniform float uCloudDensity;     // maximum cloud opacity (0–1)
uniform float uCloudScale;       // cloud formation size (1 = natural, lower = bigger)
uniform vec2  uCloudSpeed;       // wind vector (scrolls the noise domain)
uniform vec3  uCloudColor;       // base cloud colour (pre-tinted by CPU for sunset)
uniform float uTime;             // elapsed time in seconds for animation
uniform float uCloudAltitude;    // cloud layer height above ground in metres

// ---------------------------------------------------------------------------
//  Hash-based smooth 2D value noise
// ---------------------------------------------------------------------------
float hash21(vec2 p)
{
    p  = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

float smoothNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);   // smoothstep interpolation
    return mix(mix(hash21(i),              hash21(i + vec2(1.0, 0.0)), u.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), u.x), u.y);
}

// 5-octave FBM for natural, billowing cloud shapes
float cloudFBM(vec2 p)
{
    float value     = 0.0;
    float amplitude = 0.52;
    for (int i = 0; i < 5; ++i)
    {
        value     += amplitude * smoothNoise(p);
        p         *= 2.1;
        amplitude *= 0.48;
    }
    return value;
}

// ---------------------------------------------------------------------------
//  Main
// ---------------------------------------------------------------------------
void main()
{
    vec3 dir = normalize(vDirection);

    // --- Sky gradient: horizon → zenith ---
    float upT   = clamp(dir.y, 0.0, 1.0);
    float gradT = pow(upT, uHorizonSharpness);
    vec3  sky   = mix(uHorizonColor, uZenithColor, gradT);

    // --- Below-horizon ground color (dark underside) ---
    if (dir.y < 0.0)
    {
        float t = clamp(1.0 + dir.y * 8.0, 0.0, 1.0);
        sky = mix(uHorizonColor * 0.4, sky, t);
    }

    // --- Procedural cloud layer ---
    if (uCloudsEnabled > 0 && dir.y > 0.0)
    {
        // Physical altitude factor: higher clouds appear proportionally farther away,
        // so the same ground area maps to a wider UV spread.
        // Reference altitude is 1500 m (cumulus base); ratio scales the projection.
        float altFactor = max(uCloudAltitude, 50.0) / 1500.0;

        // Project sky direction onto a top-down plane and scale by altitude
        vec2 cloudUV = (dir.xz / max(dir.y, 0.02)) * altFactor;

        // Animate by scrolling the noise domain with wind
        vec2 uv = cloudUV * uCloudScale + uTime * uCloudSpeed;

        // Primary FBM sample for cloud shape
        float n = cloudFBM(uv);

        // Soft coverage threshold — smoothstep gives feathered cloud edges
        float cloud = smoothstep(1.0 - uCloudCoverage,
                                 1.0 - uCloudCoverage + 0.25,
                                 n);

        // Horizon fade: higher clouds sit closer to the horizon angle, so the fade
        // threshold is narrower.  At 1500 m reference → 0.10; at 500 m → 0.30;
        // at 6000 m → 0.025.  Clamped so neither extreme breaks rendering.
        float horizonFade = clamp((1500.0 / max(uCloudAltitude, 50.0)) * 0.10, 0.005, 0.35);
        cloud *= smoothstep(0.0, horizonFade, dir.y);

        // Second FBM sample (offset domain) for per-cloud top/bottom variation
        float thickness = cloudFBM(uv * 1.7 + vec2(3.4, 1.1));

        // Sun-driven shading: lit bright tops, shadowed dark undersides
        float sunHeight  = clamp(dot(normalize(uSunDirection), vec3(0.0, 1.0, 0.0)), 0.0, 1.0);
        vec3  cloudLit   = uCloudColor * mix(0.55, 1.0,  sunHeight);
        vec3  cloudShade = uCloudColor * mix(0.25, 0.55, sunHeight);
        vec3  cloudFinal = mix(cloudShade, cloudLit, thickness);

        sky = mix(sky, cloudFinal, cloud * uCloudDensity);
    }

    // --- Sun disk (drawn after clouds so it can peek through gaps) ---
    if (uSunDiskSize > 0.0)
    {
        float cosAngle = dot(dir, normalize(uSunDirection));
        if (cosAngle > 1.0 - uSunDiskSize)
        {
            float t = (cosAngle - (1.0 - uSunDiskSize)) / uSunDiskSize;
            sky += uSunColor * uSunDiskIntensity * smoothstep(0.0, 1.0, t);
        }
    }

    FragColor = vec4(sky, 1.0);
}
