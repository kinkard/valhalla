file(GLOB headers ${VALHALLA_SOURCE_DIR}/valhalla/midgard/*.h)

set(sources
  linesegment2.cc
  tiles.cc
  polyline2.cc
  obb2.cc
  pointll.cc
  point_tile_index.cc
  aabb2.cc
  point2.cc
  util.cc
  ellipse.cc
  logging.cc)

valhalla_module(NAME midgard
  SOURCES ${sources}
  HEADERS ${headers}
  INCLUDE_DIRECTORIES
    PUBLIC
      ${VALHALLA_SOURCE_DIR}
      ${VALHALLA_SOURCE_DIR}/valhalla
  SYSTEM_INCLUDE_DIRECTORIES
    PUBLIC
      $<$<BOOL:${WIN32}>:${dirent_include_dir}>
    PRIVATE
      ${unordered_dense_include_dir}
  DEPENDS
    Boost::boost)
