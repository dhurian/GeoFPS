#version 330 core

#define MAX_JOINTS 64

layout (location = 0) in vec3  aPosition;
layout (location = 1) in vec3  aNormal;
layout (location = 2) in vec2  aUV;
layout (location = 3) in uvec4 aJointIndices;
layout (location = 4) in vec4  aJointWeights;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

uniform int  uSkinned;                       // 0 = static mesh, 1 = GPU-skinned
uniform mat4 uBoneMatrices[MAX_JOINTS];      // skinning palette

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vUV;

void main()
{
    vec4 localPos;
    vec3 localNormal;

    if (uSkinned != 0)
    {
        mat4 skin = aJointWeights.x * uBoneMatrices[aJointIndices.x]
                  + aJointWeights.y * uBoneMatrices[aJointIndices.y]
                  + aJointWeights.z * uBoneMatrices[aJointIndices.z]
                  + aJointWeights.w * uBoneMatrices[aJointIndices.w];
        localPos    = skin * vec4(aPosition, 1.0);
        localNormal = normalize(mat3(skin) * aNormal);
    }
    else
    {
        localPos    = vec4(aPosition, 1.0);
        localNormal = aNormal;
    }

    vec4 worldPos  = uModel * localPos;
    vWorldPosition = worldPos.xyz;
    vWorldNormal   = normalize(mat3(transpose(inverse(uModel))) * localNormal);
    vUV            = aUV;
    gl_Position    = uProjection * uView * worldPos;
}
