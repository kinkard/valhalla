#ifndef VALHALLA_MJOLNIR_ADMIN_H_
#define VALHALLA_MJOLNIR_ADMIN_H_

#include <valhalla/midgard/aabb2.h>
#include <valhalla/midgard/pointll.h>
#include <valhalla/mjolnir/graphtilebuilder.h>
#include <valhalla/mjolnir/sqlite3.h>

#include <cstdint>
#include <unordered_map>

struct GEOSContextHandle_HS;
struct GEOSGeom_t;
struct GEOSPrepGeom_t;
struct GEOSWKBReader_t;

namespace valhalla {
namespace mjolnir {

// GEOS thread-safe API requires a context handle for each operation, that should be unique for every
// thread. See https://libgeos.org/usage/c_api/#reentrantthreadsafe-api for more details.
typedef std::shared_ptr<GEOSContextHandle_HS> geos_context_type;

// RAII wrapper for GEOSGeometry
struct Geometry {
  geos_context_type context;
  GEOSGeom_t* geometry;
  const GEOSPrepGeom_t* prepared;

  Geometry(geos_context_type ctx, GEOSGeom_t* geom);
  ~Geometry();

  // This class cannot be copied, but can be moved
  Geometry(Geometry const&) = delete;
  Geometry& operator=(Geometry const&) = delete;
  Geometry(Geometry&& other) noexcept
      : context(std::move(other.context)), geometry(other.geometry), prepared(other.prepared) {
    other.geometry = nullptr;
    other.prepared = nullptr;
  }
  Geometry& operator=(Geometry&& other) noexcept {
    // move and swap idiom via local variable
    Geometry local = std::move(other);
    std::swap(geometry, local.geometry);
    std::swap(context, local.context);
    return *this;
  }

  // Returns true if the geometry intersects the given point
  bool intersects(const midgard::PointLL& ll) const;
  // Creates a clone of the current geometry
  Geometry clone() const;
};

typedef std::vector<std::tuple<Geometry, std::vector<std::string>, bool>> language_poly_index;

class AdminDB {
  // A parsed geometry cached across the tiles, prepared for fast intersection tests
  struct CachedGeometry {
    GEOSGeom_t* geometry;
    const GEOSPrepGeom_t* prepared;
  };

  Sqlite3 db;
  geos_context_type geos_context;
  GEOSWKBReader_t* wkb_reader;

  // Parsed geometries of the admins and tz_world tables, keyed by table rowid. Tiles in the
  // same region intersect the same polygons, so caching parsed geometries across the tiles
  // avoids re-reading and re-parsing them for every tile
  std::unordered_map<int64_t, CachedGeometry> admin_geometries_;
  std::unordered_map<int64_t, CachedGeometry> tz_geometries_;
  size_t cached_coordinates_ = 0;

  // Constructor is private, use `AdminDB::open()` instead.
  AdminDB(Sqlite3&& db);

  const CachedGeometry*
  get_geometry(std::unordered_map<int64_t, CachedGeometry>& cache, const char* table, int64_t rowid);
  void clear_geometry_cache();

public:
  // Tries to open an AdminDB from the given path. Returns std::nullopt if failed.
  static std::optional<AdminDB> open(const std::string& path);
  ~AdminDB();

  // This class cannot be copied, but can be moved
  AdminDB(AdminDB const&) = delete;
  AdminDB& operator=(AdminDB const&) = delete;
  AdminDB(AdminDB&& other) noexcept
      : db(std::move(other.db)), geos_context(std::move(other.geos_context)),
        wkb_reader(other.wkb_reader), admin_geometries_(std::move(other.admin_geometries_)),
        tz_geometries_(std::move(other.tz_geometries_)),
        cached_coordinates_(other.cached_coordinates_) {
    other.wkb_reader = nullptr;
    other.admin_geometries_.clear();
    other.tz_geometries_.clear();
  }
  AdminDB& operator=(AdminDB&& other) noexcept {
    // move and swap idiom via local variable
    AdminDB local = std::move(other);
    std::swap(db, local.db);
    std::swap(geos_context, local.geos_context);
    std::swap(wkb_reader, local.wkb_reader);
    std::swap(admin_geometries_, local.admin_geometries_);
    std::swap(tz_geometries_, local.tz_geometries_);
    std::swap(cached_coordinates_, local.cached_coordinates_);
    return *this;
  }

  sqlite3* get() {
    return db.get();
  }

  // Return the parsed geometry of an admins/tz_world table row, reading and parsing the blob
  // only when it is not cached yet. May return nullptr if the row has no geometry. The pointer
  // is only valid until the next call as the cache is cleared when it grows too large.
  const CachedGeometry* get_admin_geometry(int64_t rowid);
  const CachedGeometry* get_tz_geometry(int64_t rowid);

  // Same test ST_Intersects(geom, BuildMBR(bbox)) does, on the cached geometry
  bool intersects_bbox(const CachedGeometry& geom, const midgard::AABB2<midgard::PointLL>& bbox);

  // Clips the cached geometry to the given bounding box.
  Geometry clip(const CachedGeometry& geom, const midgard::AABB2<midgard::PointLL>& bbox);
};

/**
 * Get the polygon index.  Used by tz and admin areas.  Checks if the pointLL is covered_by the
 * poly.
 * @param  polys      unordered map of polys.
 * @param  ll         point that needs to be checked.
 * @param  graphtile  graphtilebuilder that is used to determine if we are a country poly or not.
 */
uint32_t GetMultiPolyId(const std::multimap<uint32_t, Geometry>& polys,
                        const midgard::PointLL& ll,
                        GraphTileBuilder& graphtile);

/**
 * Get the polygon index.  Used by tz and admin areas.  Checks if the pointLL is covered_by the
 * poly.
 * @param  polys      unordered map of polys.
 * @param  ll         point that needs to be checked.
 */
uint32_t GetMultiPolyId(const std::multimap<uint32_t, Geometry>& polys, const midgard::PointLL& ll);

/**
 * Get the vector of languages for this LL.  Used by admin areas.  Checks if the pointLL is covered_by
 * the poly.
 * @param  language_polys      tuple that contains a language, poly, is_default_language.
 * @param  ll         point that needs to be checked.
 * @return  Returns the vector of pairs {language, is_default_language}
 */
std::vector<std::pair<std::string, bool>>
GetMultiPolyIndexes(const language_poly_index& language_ploys, const midgard::PointLL& ll);

/**
 * Get the timezone polys from the db
 * @param  db           sqlite3 db handle
 * @param  aabb         bb of the tile
 */
std::multimap<uint32_t, Geometry> GetTimeZones(AdminDB& db,
                                               const midgard::AABB2<midgard::PointLL>& aabb);

/**
 * Get the admin polys that intersect with the tile bounding box.
 * @param  db               sqlite3 db handle
 * @param  drive_on_right   unordered map that indicates if a country drives on right side of the
 * road
 * @param  allow_intersection_names   unordered map that indicates if we call out intersections
 * names for this country
 * @param  default_languages ordered map that is used for lower admins that have an
 * default language set
 * @param  language_polys    ordered map that is used for lower admins that have an
 * default language set
 * @param  aabb              bb of the tile
 * @param  tilebuilder       Graph tile builder
 */
std::multimap<uint32_t, Geometry>
GetAdminInfo(AdminDB& db,
             std::unordered_map<uint32_t, bool>& drive_on_right,
             std::unordered_map<uint32_t, bool>& allow_intersection_names,
             language_poly_index& language_polys,
             const midgard::AABB2<midgard::PointLL>& aabb,
             GraphTileBuilder& tilebuilder);

/**
 * Get all the country access records from the db and save them to a map.
 * @param  db    sqlite3 db handle
 */
std::unordered_map<std::string, std::vector<int>> GetCountryAccess(AdminDB& db);

} // namespace mjolnir
} // namespace valhalla

#endif // VALHALLA_MJOLNIR_ADMIN_H_
