#include <vector>
#include <iomanip>
#include "osmx/storage.h"
#include "s2/s2latlng.h"
#include "s2/s2region_coverer.h"
#include "s2/s2latlng_rect.h"
#include "roaring64map.hh"

using namespace std;

// Example of a very simple program to get OSM objects in a region
// and print them out as WKT.
// see way_wkt for a simpler example.
// This program does not handle Relations at all,
// so it can't be used to find all Polygons in a region, since they may be Multipolygon relations.
// Usage: ./bbox_wkt OSMX_FILE MIN_LON MIN_LAT MAX_LON MAX_LAT

int main(int argc, char* argv[]) {
  vector<string> args(argv, argv+argc);

  MDB_env* env = osmx::db::createEnv(args[1]);
  MDB_txn* txn;
  CHECK(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn));

  // Create a S2LatLngRect.
  auto lo = S2LatLng::FromDegrees(stof(args[3]),stof(args[2]));
  auto hi = S2LatLng::FromDegrees(stof(args[5]),stof(args[4]));
  auto bbox = S2LatLngRect{lo,hi};

  // Find the cell covering for the LatLngRect,
  // with a maximum cell level of 16.
  // Although nodes in the database are stored at level=16,
  // Cells with levels less than 16 will be correctly handled by the traverseCell function.
  // This allows for more compact representations of large regions.

  S2RegionCoverer::Options options;
  options.set_max_level(16);
  S2RegionCoverer coverer(options);
  S2CellUnion covering = coverer.GetCovering(bbox);

  cerr << "Cell covering size: " << covering.size() << endl;

  // Get all node_ids that match the given region.
  Roaring64Map node_ids;
  MDB_dbi dbi;
  MDB_cursor *cursor;
  CHECK(mdb_dbi_open(txn, "cell_node", MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP, &dbi));
  CHECK(mdb_cursor_open(txn,dbi,&cursor));
  for (auto cell_id : covering.cell_ids()) {
    osmx::db::traverseCell(cursor,cell_id,node_ids);
  }
  mdb_cursor_close(cursor);

  cerr << "Nodes in region: " << node_ids.cardinality() << endl;

  // Get all way_ids that are referred to by node_ids.
  Roaring64Map way_ids;
  CHECK(mdb_dbi_open(txn, "node_way", MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP, &dbi));
  CHECK(mdb_cursor_open(txn,dbi,&cursor));
  for (auto const &node_id : node_ids) {
    osmx::db::traverseReverse(cursor,node_id,way_ids);
  }
  mdb_cursor_close(cursor);

  cerr << "Ways in region: " << way_ids.cardinality() << endl;

  osmx::db::Locations locations(txn);
  osmx::db::Elements ways(txn,"ways");

  for (auto way_id : way_ids) {
    // Fetch a Way element by ID.
    auto message = ways.getReader(way_id);
    auto way = message.getRoot<Way>();

    // Tags are stored as a vector of key,value.
    // Iterate through all tags and print the value if key = name.
    auto tags = way.getTags();
    for (int i = 0; i < tags.size() / 2; i++) {
      if (tags[i*2] == "name") cout << tags[i*2+1].cStr();
    }

    // Assemble a WKT LineString geometry.
    cout << "\tLINESTRING (";
    cout << std::fixed << std::setprecision(7); // the output should have 7 decimal places.
    auto nodes = way.getNodes();
    for (int i = 0; i < nodes.size(); i++) {
      auto location = locations.get(nodes[i]);
      if (i > 0) cout << ",";
      cout << location.coords.lon() << " " << location.coords.lat();
    }
    cout << ")" << endl;
  }

  mdb_env_close(env); // close the database.
}
