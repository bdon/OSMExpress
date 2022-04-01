#include <vector>
#include <iomanip>
#include <fstream>
#include "osmx/storage.h"
#include "osmx/extract.h"
#include "s2/s2latlng.h"
#include "s2/s2region_coverer.h"
#include "s2/s2latlng_rect.h"
#include "roaring64map.hh"
#include "nlohmann/json.hpp"
#include "osmium/area/assembler.hpp"
#include "osmium/osm/way.hpp"
#include "flatgeobuf/feature_generated.h"
#include "flatgeobuf/header_generated.h"

using namespace std;

// Example of a very simple program to get OSM objects in a region
// and print them out as WKT.
// see way_wkt for a simpler example.
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

  Roaring64Map node_ids;
  Roaring64Map way_ids;
  Roaring64Map relation_ids;
  ExportProgress prog;
  entitiesForRegion(txn,covering,node_ids,way_ids,relation_ids, false, prog);

  // start multithreaded part

  char rs{ 30 };
  // fgb
  flatbuffers::FlatBufferBuilder fbBuilder;
  std::ofstream ofile("output.fgb", std::ios::binary);
  uint8_t magicbytes[] = { 0x66, 0x67, 0x62, 0x03, 0x66, 0x67, 0x62, 0x00 };
  ofile.write((char *)magicbytes,sizeof(magicbytes));
  std::vector<double> envelope = {-180,-90,180,90};

  std::vector<flatbuffers::Offset<FlatGeobuf::Column>> headerColumns;
  headerColumns.push_back(CreateColumnDirect(fbBuilder, "key", FlatGeobuf::ColumnType::String));
  auto phColumns = headerColumns.size() == 0 ? nullptr : &headerColumns;

  auto header = FlatGeobuf::CreateHeaderDirect(fbBuilder, "test dataset", &envelope, FlatGeobuf::GeometryType::Unknown, false, false, false, false, phColumns, 0, 0, 0);
  fbBuilder.FinishSizePrefixed(header);
  ofile.write((char *)fbBuilder.GetBufferPointer(),fbBuilder.GetSize());
  fbBuilder.Clear();

  osmium::area::AssemblerConfig config;
  config.ignore_invalid_locations = true;
  osmx::db::Locations locations(txn);
  osmx::db::Elements nodes(txn,"nodes");
  {
    for (auto node_id : node_ids) {
      if (!nodes.exists(node_id)) {
        continue; // it is an untagged node, ignore
      }
      auto message = nodes.getReader(node_id);
      auto node = message.getRoot<Node>();
      auto location = locations.get(node_id);
      auto tags = node.getTags();

      // fgb 
      std::vector<double> coords_vector = { location.coords.lon(), location.coords.lat() };
      auto geometry = FlatGeobuf::CreateGeometryDirect(fbBuilder, nullptr, &coords_vector, nullptr, nullptr, nullptr, nullptr, FlatGeobuf::GeometryType::Point);


      std::vector<uint8_t> properties;
      std::vector<flatbuffers::Offset<FlatGeobuf::Column>> columns;
      for (int i = 0; i < tags.size() / 2; i++) {
        // props[tags[i*2]] = tags[i*2+1].cStr();
      }
      const uint16_t column_index = 0;
      std::copy(reinterpret_cast<const uint8_t *>(&column_index), reinterpret_cast<const uint8_t *>(&column_index + 1), std::back_inserter(properties));
      const std::string str = "value2";
      uint32_t len = static_cast<uint32_t>(str.length());
      std::copy(reinterpret_cast<const uint8_t *>(&len), reinterpret_cast<const uint8_t *>(&len + 1), std::back_inserter(properties));
      std::copy(str.begin(), str.end(), std::back_inserter(properties));

      // columns.push_back(CreateColumnDirect(fbBuilder, "key", FlatGeobuf::ColumnType::String, nullptr, nullptr, 6));


      auto pProperties = properties.size() == 0 ? nullptr : &properties;
      auto pColumns = columns.size() == 0 ? nullptr : &columns;

      auto feature = FlatGeobuf::CreateFeatureDirect(fbBuilder, geometry, pProperties, pColumns);
      fbBuilder.FinishSizePrefixed(feature);
      ofile.write((char *)fbBuilder.GetBufferPointer(),fbBuilder.GetSize());
      fbBuilder.Clear();

      // geojson
      // nlohmann::json j;
      // j["type"] = "Feature";
      // nlohmann::json props;
      // for (int i = 0; i < tags.size() / 2; i++) {
      //   props[tags[i*2]] = tags[i*2+1].cStr();
      // }
      // j["properties"] = props;

      // nlohmann::json geom_obj;
      // geom_obj["type"] = "Point";
      // geom_obj["coordinates"] = { location.coords.lon(), location.coords.lat() };
      // j["geometry"] = geom_obj;
      // cout << rs << j.dump() << endl;

    }
  }

  osmx::db::Elements ways(txn,"ways");
  {
    for (auto way_id : way_ids) {
      auto message = ways.getReader(way_id);
      auto way = message.getRoot<Way>();

      // nlohmann::json props;
      // auto tags = way.getTags();
      // for (int i = 0; i < tags.size() / 2; i++) {
      //   props[tags[i*2]] = tags[i*2+1].cStr();
      // }
      // nlohmann::json j;
      // j["type"] = "Feature";
      // j["properties"] = props;

      // nlohmann::json geometry;
      // geometry["type"] = "LineString";
      
      // nlohmann::json coordinates = nlohmann::json::array();

      // auto nodes = way.getNodes();
      // for (int i = 0; i < nodes.size(); i++) {
      //   auto location = locations.get(nodes[i]);
      //   coordinates.push_back({location.coords.lon(),location.coords.lat()});
      // }
      // if (nodes[0] != nodes[nodes.size()-1]) continue;

      // osmium::memory::Buffer buffer1{0,osmium::memory::Buffer::auto_grow::yes};
      // osmium::memory::Buffer buffer2{0,osmium::memory::Buffer::auto_grow::yes};

      // {
      //   osmium::builder::WayBuilder way_builder{buffer1};
      //   way_builder.set_id(way_id);

      //   {
      //     osmium::builder::WayNodeListBuilder way_node_list_builder{way_builder};
      //     for (auto node_id : way.getNodes()) {
      //       way_node_list_builder.add_node_ref(node_id);
      //     }
      //   }

      //   auto tags = way.getTags();
      //   osmium::builder::TagListBuilder tag_builder{way_builder};
      //   for (int i = 0; i < tags.size() / 2; i++) {
      //     tag_builder.add_tag(tags[i*2],tags[i*2+1]);
      //   }
      // }

      // auto &way_obj = buffer1.get<osmium::Way>(0);

      // for (auto& node_ref : way_obj.nodes()) {
      //     auto location = locations.get(node_ref.ref());
      //     node_ref.set_location(location.coords);
      // }

      // osmium::area::Assembler a{config};
      // a(way_obj,buffer2);
      // buffer1.clear();
      // buffer2.clear();

      // geometry["coordinates"] = coordinates;
      // j["geometry"] = geometry;
      // cout << rs << j.dump() << endl;


    }
  }

  osmx::db::Elements relations(txn,"relations");
  for (auto relation_id : relation_ids) {
    auto reader = relations.getReader(relation_id);
    Relation::Reader relation = reader.getRoot<Relation>();


    bool is_multipolygon = false;
    auto tags = relation.getTags();
    for (int i = 0; i < tags.size() / 2; i++) {
      if (tags[i*2] == "type" && tags[i*2+1] == "multipolygon") {
        is_multipolygon = true;
      }
    }

    if (!is_multipolygon) continue;
    // cout << "Rel " << relation_id << endl;

    osmium::memory::Buffer buffer1{0,osmium::memory::Buffer::auto_grow::yes};

    {
      osmium::builder::RelationBuilder relation_builder{buffer1};
      relation_builder.set_id(relation_id);
      osmium::builder::RelationMemberListBuilder relation_member_list_builder{relation_builder};
      for (auto const &member : relation.getMembers()) {
        if (member.getType() == RelationMember::Type::WAY) {
          auto way_id = member.getRef();
          relation_member_list_builder.add_member(osmium::item_type::way,way_id,member.getRole());
        }
      }
    }
    buffer1.commit();
    for (auto const &member : relation.getMembers()) {
      if (member.getType() == RelationMember::Type::WAY) {
        auto way_id = member.getRef();
        {
          osmium::builder::WayBuilder way_builder{buffer1};
          way_builder.set_id(way_id);
          auto message = ways.getReader(way_id);
          auto way = message.getRoot<Way>();
          {
            osmium::builder::WayNodeListBuilder way_node_list_builder{way_builder};
            for (auto node_id : way.getNodes()) {
              way_node_list_builder.add_node_ref(node_id);
            }
          }
        }
        buffer1.commit();
      }
    }

    vector<const osmium::Way*> members;
    members.reserve(relation.getMembers().size());

    // find all pointers into Ways, populate the locations
    for (auto& way_obj : buffer1.select<osmium::Way>()) {
      for (auto& node_ref : way_obj.nodes()) {
          auto location = locations.get(node_ref.ref());
          node_ref.set_location(location.coords);
      }
      members.push_back(&way_obj);
    }

    auto &relation_obj = buffer1.get<osmium::Relation>(0);

    osmium::area::Assembler a{config};

    osmium::memory::Buffer buffer2{0,osmium::memory::Buffer::auto_grow::yes};
    if (a(relation_obj,members,buffer2)) {
      auto const &result = buffer2.get<osmium::Area>(0);
      // cout << result.id() << endl;
      // cout << "outer: " << get<0>(result.num_rings()) << " inner: " << get<1>(result.num_rings()) << endl;
    }
  }

  ofile.flush();
  mdb_env_close(env); // close the database.
}
