#pragma once
// ╔═══════════════════════════════════════════════════════╗
// ║  iso_engine — isometric engine for RayLib             ║
// ║  C++20 / MSVC / Header-only                          ║
// ║                                                       ║
// ║  Features:                                            ║
// ║  • Multi-floor isometric map with height support      ║
// ║  • 3D-aware collision detection with wall sliding     ║
// ║  • Z-sorting for correct draw order (PZ-style)        ║
// ║  • Isometric camera with smooth follow & zoom         ║
// ╚═══════════════════════════════════════════════════════╝

#include "core.h"
#include "map.h"
#include "collision.h"
#include "zsort.h"
#include "camera.h"

namespace iso {

	/// Library version
	inline constexpr int VERSION_MAJOR = 1;
	inline constexpr int VERSION_MINOR = 0;
	inline constexpr int VERSION_PATCH = 0;

} // namespace iso