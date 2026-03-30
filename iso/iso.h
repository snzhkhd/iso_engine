#pragma once
// ╔═══════════════════════════════════════════════════════╗
// ║  iso_engine — isometric engine for RayLib             ║
// ║  C++20 / MSVC / Header-only                          ║
// ║                                                       ║
// ║  Features:                                            ║
// ║  • Multi-floor isometric map with height support      ║
// ║  • Chunk-based map system for large worlds            ║
// ║  • Entity system with float positioning               ║
// ║  • 3D-aware collision detection with wall sliding     ║
// ║  • Z-sorting for correct draw order (PZ-style)        ║
// ║  • Isometric camera with smooth follow & zoom         ║
// ║  • Unified .isomap file format                        ║
// ╚═══════════════════════════════════════════════════════╝

#include "core.h"
#include "map.h"
#include "entity.h"
#include "chunk.h"
#include "collision.h"
#include "zsort.h"
#include "camera.h"
#include "map_format.h"

namespace iso {

	/// Library version
	inline constexpr int VERSION_MAJOR = 2;
	inline constexpr int VERSION_MINOR = 0;
	inline constexpr int VERSION_PATCH = 0;

} // namespace iso