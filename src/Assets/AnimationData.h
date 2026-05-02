#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace GeoFPS
{

// ---------------------------------------------------------------------------
//  Skeleton
// ---------------------------------------------------------------------------

/// One joint in the skeleton, in the order defined by tinygltf::Skin::joints[].
struct JointData
{
    std::string name;
    int         parentIndex      {-1};          // -1 = root joint
    glm::mat4   inverseBindMatrix{1.0f};         // model-space → joint local space
};

struct SkeletonData
{
    std::vector<JointData> joints;               // indexed 0 … N-1
    int                    rootJointIndex {-1};  // index of the root joint in joints[]
};

// ---------------------------------------------------------------------------
//  Animation clips
// ---------------------------------------------------------------------------

/// One property track for one joint.
struct AnimationChannel
{
    int         jointIndex    {-1};   // index into SkeletonData::joints[]
    std::string path;                 // "translation" | "rotation" | "scale"
    std::string interpolation;        // "LINEAR" | "STEP" | "CUBICSPLINE"

    std::vector<float>     times;         // keyframe times in seconds
    std::vector<glm::vec3> valuesVec3;    // translation or scale values
    std::vector<glm::quat> valuesQuat;    // rotation values (normalised quaternion)
};

struct AnimationClip
{
    std::string                  name;
    float                        duration {0.0f};  // max keyframe time in seconds
    std::vector<AnimationChannel> channels;
};

// ---------------------------------------------------------------------------
//  Per-asset runtime playback state
// ---------------------------------------------------------------------------

struct AnimationState
{
    int   activeClipIndex {-1};    // -1 = bind pose (no animation)
    float currentTime     {0.0f};  // seconds into the active clip
    float playbackSpeed   {1.0f};  // multiplier; 1.0 = real-time
    bool  isPlaying       {false};
    bool  loop            {true};

    /// Final bone palette uploaded to the GPU each frame.
    /// Populated by Application::UpdateAnimationState().
    /// Size = SkeletonData::joints.size().
    std::vector<glm::mat4> skinningMatrices;
};

// ---------------------------------------------------------------------------
//  Node-transform animation  (rigid-body object animation — no skin needed)
// ---------------------------------------------------------------------------

/// One node in the scene hierarchy, stored per-asset for node animation.
struct NodeData
{
    std::string name;
    int         parentIndex {-1};               // index into ImportedAssetData::nodes[]; -1 = root
    // Rest-pose local transform stored as TRS components (not a matrix) so we can
    // cleanly override individual components from animation channels at runtime.
    glm::vec3   translation {0.0f, 0.0f, 0.0f};
    glm::quat   rotation    {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3   scale       {1.0f, 1.0f, 1.0f};
    /// True when the glTF node specified a raw 4×4 matrix instead of TRS.
    /// In this case we cannot override individual TRS channels, so animation
    /// channels targeting this node are ignored.
    bool        hasMatrix   {false};
    glm::mat4   matrix      {1.0f};
};

/// One property track targeting a scene node (not a skin joint).
struct NodeAnimationChannel
{
    int         nodeIndex     {-1};  // index into ImportedAssetData::nodes[]
    std::string path;                // "translation" | "rotation" | "scale"
    std::string interpolation;       // "LINEAR" | "STEP"
    std::vector<float>     times;
    std::vector<glm::vec3> valuesVec3;   // for translation / scale
    std::vector<glm::quat> valuesQuat;   // for rotation
};

/// One named action that drives one or more node transforms (rigid-body clips).
struct NodeAnimationClip
{
    std::string                       name;
    float                             duration {0.0f};
    std::vector<NodeAnimationChannel> channels;
};

/// Per-asset runtime playback state for node-transform animation.
/// All clips play simultaneously — useful for multi-part objects like
/// wind turbines where each blade has its own clip.
struct NodeAnimationState
{
    float currentTime   {0.0f};
    float playbackSpeed {1.0f};
    bool  isPlaying     {false};
    bool  loop          {true};

    /// Animated world-space transform for every node, recomputed each frame.
    /// Indexed by the same node index used in NodeData and ImportedPrimitiveData.
    std::vector<glm::mat4> nodeWorldTransforms;
};

} // namespace GeoFPS
