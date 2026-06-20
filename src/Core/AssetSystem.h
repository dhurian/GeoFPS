#pragma once

#include "Core/AssetJobs.h"
#include "Core/AssetModel.h"
#include "Core/DiagnosticsState.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace GeoFPS
{
// Upload a primitive's CPU-side texture pixels to the GPU and release the
// pixel buffers.  Pure asset-domain helper, shared by the async load path
// (AssetSystem::ProcessLoadJobs) and the synchronous LoadImportedAsset.
void UploadImportedPrimitiveTextures(ImportedPrimitiveData& primitive);

// Everything ProcessLoadJobs needs from outside the asset store: the diagnostics
// sink it accumulates mesh-upload counters into, and the status string it sets.
// No orchestration callbacks and no upload budget — uploading a ready asset's
// primitives is unconditional, so this stays tiny.
struct AssetJobContext
{
    DiagnosticsState& diagnostics;
    std::string&      statusMessage;
};

// Owns the imported-asset store: the placed assets, which one is active, the
// copy/paste clipboard and its offset, and the in-flight async load jobs.
// First step of lifting assets out of the Application god-class.
//
// Scope is deliberately narrow for now.  Application still drives all the
// orchestration — loading, placing, deleting, snapping, rendering, persistence
// — reaching the store through these accessors.  Moving that logic in here (and
// the load-job draining loop) comes in later steps.
class AssetSystem
{
  public:
    [[nodiscard]] std::vector<ImportedAsset>& assets() { return m_Assets; }
    [[nodiscard]] const std::vector<ImportedAsset>& assets() const { return m_Assets; }

    [[nodiscard]] int activeIndex() const { return m_ActiveIndex; }
    void setActiveIndex(int index) { m_ActiveIndex = index; }

    [[nodiscard]] std::vector<AssetClipboardEntry>& clipboard() { return m_Clipboard; }
    [[nodiscard]] const std::vector<AssetClipboardEntry>& clipboard() const { return m_Clipboard; }

    [[nodiscard]] glm::vec3& pasteOffset() { return m_PasteOffset; }
    [[nodiscard]] const glm::vec3& pasteOffset() const { return m_PasteOffset; }

    [[nodiscard]] std::vector<AssetLoadJob>& loadJobs() { return m_LoadJobs; }
    [[nodiscard]] const std::vector<AssetLoadJob>& loadJobs() const { return m_LoadJobs; }

    // Harvest finished async asset loads: resolve ready futures, upload each
    // asset's meshes/skins/textures, compute its picking AABB, and mark it
    // loaded.  Everything it needs from Application arrives through ctx.  Called
    // once per frame from Application::ProcessBackgroundJobs.
    void ProcessLoadJobs(AssetJobContext& ctx);

  private:
    std::vector<ImportedAsset> m_Assets;
    int m_ActiveIndex {0};
    std::vector<AssetClipboardEntry> m_Clipboard;
    glm::vec3 m_PasteOffset {2.0f, 0.0f, 2.0f};
    std::vector<AssetLoadJob> m_LoadJobs;
};
} // namespace GeoFPS
