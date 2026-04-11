#version 330 core

in vec3 vWorldPosition;
in vec3 vWorldNormal;

uniform vec3 uCameraPos;
uniform vec3 uBaseColor;

out vec4 FragColor;

void main()
{
    vec3 normal = normalize(vWorldNormal);
    vec3 lightDirection = normalize(vec3(0.45, 1.0, 0.2));
    float diffuse = max(dot(normal, lightDirection), 0.15);

    vec3 viewDirection = normalize(uCameraPos - vWorldPosition);
    vec3 halfVector = normalize(lightDirection + viewDirection);
    float specular = pow(max(dot(normal, halfVector), 0.0), 24.0);

    vec3 color = (uBaseColor * diffuse) + vec3(0.18) + vec3(specular * 0.15);
    FragColor = vec4(color, 1.0);
}
