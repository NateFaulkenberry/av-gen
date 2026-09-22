#pragma once

// Phase C §37/§38/§82: the motion database as a stored artefact.
//
// **Before this existed a database lived only in memory.** `buildMotionDatabase` poses every frame of
// every clip, and nothing wrote the result anywhere, so the only way for anything to search a
// database was to run the offline half -- feature extraction, the trajectory look-ahead, the
// normalization pass -- in the process that wanted to search it. §37 draws the line the other way:
// the runtime starts from a MotionPack and a search structure, and does not re-extract anything
// that could have been baked.
//
// ---- where it lives (§38) -----------------------------------------------------------------------
//
//   character.motionpack/
//     pack.json, skeleton.bin, clips.bin, meta.bin      (Phase A, unchanged)
//     databases/<name>.motiondb                         (Phase C, one file per feature config)
//
// **The pack format is not changed.** A database is an optional file beside the pack, discovered by
// path, so a Phase A reader and every pack already on disk are untouched, and a pack may carry more
// than one database (§43: a character-specific feature config beside a shared one). `pack.json`
// gains nothing: listing a database there would be a second statement of a fact the directory
// already states, and the two would drift.
//
// **One file, not one per array**, so publishing a rebuilt database is one rename (§40): a reader
// sees the old file or the new one, never a features array from one build beside a sample table
// from another.
//
//   u32 magic 'AVMD'   u32 file version   u64 header bytes   header (JSON)
//   u64 payload bytes  payload (the per-sample arrays and the normalization, little-endian)
//
// The header is JSON because it is small and a human debugging a mismatch reads it; the payload is
// raw because it is large and a reader must not parse a million floats from text.
//
// ---- what a load checks (§36: do not silently load incompatible databases) --------------------
//
// Every one of these is a refusal with a message, never a warning: the file version, the database
// version, the feature schema recomputed from the stored config against the one recorded, the
// dimension against the config, every array's length against the sample count, every
// continuation and clip index against its range, the payload identity recomputed against the one
// recorded, and -- when the caller supplies them -- the skeleton digest and the source pack's
// content digest.

#include "core/error.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace avgen::scene {

// Bumped when the on-disk layout of a `.motiondb` changes. Separate from `MotionDatabase::kVersion`,
// which versions what the arrays *mean*; this versions how they are laid out in a file.
inline constexpr std::uint32_t kMotionDatabaseFileVersion = 2; // 2: sampleRoot and clipTravels (§71)
inline constexpr char kMotionDatabaseExtension[] = ".motiondb";

// ---- digests (§36/§82) --------------------------------------------------------------------------

// Everything `buildMotionDatabase` reads from a pack: the skeleton, every clip's keys, its loop
// flag, tags, phase and contact spans, and the provenance processing chain -- which is where a
// retarget profile is recorded, so the same source retargeted differently digests differently.
[[nodiscard]] std::string motionPackContentDigest(const MotionPack& pack);
// What the dimensions of a feature vector are: the joints, the contact joints, the trajectory
// horizons and which optional blocks are present. **Not the weights**: a weight changes what a
// dimension is multiplied by, not what it means, and it is tunable without a rebuild.
[[nodiscard]] std::string motionFeatureSchemaDigest(const MotionFeatureConfig& config);
// §82's content address: equal inputs, equal key. Includes the weights (the build's `costSpread`
// statistic is weighted) and the tool version (a changed extractor must not hit an old cache).
[[nodiscard]] std::string motionDatabaseBuildKey(const std::string& sourcePackDigest,
                                                 const MotionFeatureConfig& config, float sampleRate,
                                                 const std::string& toolVersion);
// A digest of the searchable content. See `MotionDatabase::identity`.
[[nodiscard]] std::uint64_t motionDatabaseIdentity(const MotionDatabase& db);

// Fill `db.build` and `db.identity`. Called by `buildMotionDatabase`; exposed so a database
// assembled any other way (a test, a future importer) can be stamped the same way.
void stampMotionDatabase(MotionDatabase& db, const MotionPack& pack,
                         const MotionDatabaseOptions& options);

// ---- the file --------------------------------------------------------------------------------------

// `<pack>/databases/<name>.motiondb`.
[[nodiscard]] std::filesystem::path motionDatabasePath(const std::filesystem::path& packDirectory,
                                                       const std::string& name);

// Written to a temporary beside `file` and renamed over it, so a reader never sees half a file.
[[nodiscard]] Result<void> writeMotionDatabase(const MotionDatabase& db,
                                               const std::filesystem::path& file);

// What the caller knows the database must match. Empty fields are not checked.
struct MotionDatabaseExpectations {
    std::string skeletonDigest;
    std::string sourcePackDigest;
    std::string featureSchema;
};

[[nodiscard]] Result<MotionDatabase> readMotionDatabase(const std::filesystem::path& file,
                                                        const MotionDatabaseExpectations& expect = {});

// The header alone: everything except the per-sample arrays and the normalization, which are left
// empty. For §82's cache check and for tools that want the provenance without the payload.
[[nodiscard]] Result<MotionDatabase> readMotionDatabaseHeader(const std::filesystem::path& file);

// The first field in which two databases differ, or empty when they are identical to the bit.
// Floats are compared by bit pattern, not by value: a round trip that turned -0 into 0 or changed
// the last bit of a feature is a round trip that changed the database.
[[nodiscard]] std::string firstMotionDatabaseDifference(const MotionDatabase& a,
                                                        const MotionDatabase& b);

// ---- §82: the build cache -------------------------------------------------------------------------

struct CachedBuildResult {
    MotionDatabase db;
    // True when `file` already held a database with this build's key and nothing was rebuilt.
    bool reused = false;
};

// Build `pack` into `file` unless `file` already holds a build with the same key, in which case it
// is loaded instead. The key check reads the header only, so a hit costs a file open, not a parse
// of the payload -- and a hit is still fully verified by the load that follows.
[[nodiscard]] Result<CachedBuildResult> buildMotionDatabaseCached(const MotionPack& pack,
                                                                  const MotionDatabaseOptions& options,
                                                                  const std::filesystem::path& file);

} // namespace avgen::scene
