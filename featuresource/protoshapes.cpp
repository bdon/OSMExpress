#include <vector>
#include <iomanip>
#include <fstream>
#include "osmx/storage.h"
#include "osmx/extract.h"
#include "osmium/area/assembler.hpp"
#include "osmium/osm/way.hpp"
#include "flatgeobuf/feature_generated.h"
#include "flatgeobuf/header_generated.h"

using namespace std;

std::vector<uint8_t> tagsToProperties(const std::unordered_map<string,uint16_t> &keyToCol, const capnp::List<capnp::Text,capnp::Kind::BLOB>::Reader &tags) {
  std::vector<uint8_t> properties;
  for (int i = 0; i < tags.size() / 2; i++) {
    if (keyToCol.count(tags[i*2].cStr()) > 0) {
      const uint16_t column_index = keyToCol.at(tags[i*2].cStr());
      std::copy(reinterpret_cast<const uint8_t *>(&column_index), reinterpret_cast<const uint8_t *>(&column_index + 1), std::back_inserter(properties));
      const std::string str = tags[i*2+1].cStr();
      uint32_t len = static_cast<uint32_t>(str.length());
      std::copy(reinterpret_cast<const uint8_t *>(&len), reinterpret_cast<const uint8_t *>(&len + 1), std::back_inserter(properties));
      std::copy(str.begin(), str.end(), std::back_inserter(properties));
    }
  }
  return properties;
}

int main(int argc, char* argv[]) {

  vector<string> args(argv, argv+argc);

  MDB_env* env = osmx::db::createEnv(args[1]);
  MDB_txn* txn;
  CHECK(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn));

  auto startTime = std::chrono::high_resolution_clock::now();

  osmx::db::Locations locations(txn);
  osmx::db::Elements nodes(txn,"nodes");
  osmx::db::Elements ways(txn,"ways");
  osmx::db::Elements relations(txn,"relations");

  MDB_dbi dbi;
  MDB_cursor *cursor;
  MDB_val key, data;

  CHECK(mdb_dbi_open(txn, "relations", MDB_INTEGERKEY, &dbi));
  CHECK(mdb_cursor_open(txn,dbi,&cursor));

  std::map<string,size_t> extra_keys;

  auto retval = mdb_cursor_get(cursor,&key,&data,MDB_FIRST);

  while (retval == 0) {
    auto arr = kj::ArrayPtr<const capnp::word>((const capnp::word *)data.mv_data,data.mv_size);
    capnp::FlatArrayMessageReader reader = capnp::FlatArrayMessageReader(arr);
    Relation::Reader relation = reader.getRoot<Relation>();

    bool is_boundary = false;
    auto tags = relation.getTags();

    for (int i = 0; i < tags.size() / 2; i++) {
      if (tags[i*2] == "boundary" && tags[i*2+1] == "administrative") {
        is_boundary = true;
      }
    }

    if (is_boundary) {
      for (int i = 0; i < tags.size() / 2; i++) {
        if (strncmp("name:",tags[i*2].cStr(),5) == 0) {
          ++extra_keys[tags[i*2]];
        }
        // if (strncmp("official_name:",tags[i*2].cStr(),14) == 0) {
        //   ++extra_keys[tags[i*2]];
        // }
        // if (strncmp("alt_name:",tags[i*2].cStr(),9) == 0) {
        //   ++extra_keys[tags[i*2]];
        // }
      }
    }

    retval = mdb_cursor_get(cursor,&key,&data,MDB_NEXT);
  }

  vector<string> columns = {
    "admin_level",
    "name",
    "alt_name",
    "int_name",
    "ISO3166-1",
    "ISO3166-1:alpha2",
    "ISO3166-1:alpha3",
    "ISO3166-1:numeric",
    "wikidata",
    "wikipedia",
  };

  for (auto const &pair : extra_keys) {
    if (get<1>(pair) >= 500) {
      columns.push_back(get<0>(pair));
    }
  }

  cout << "Columns: " << columns.size() << endl;

  flatbuffers::FlatBufferBuilder fbBuilder;
  unique_ptr<ostream> out;
  // out = make_unique<std::ostream>(std::cout.rdbuf());
  out = make_unique<std::ofstream>(args[2], std::ios::binary);
  uint8_t magicbytes[] = { 0x66, 0x67, 0x62, 0x03, 0x66, 0x67, 0x62, 0x00 };
  out->write((char *)magicbytes,sizeof(magicbytes));
  std::vector<double> envelope = {-180,-90,180,90};

  std::vector<flatbuffers::Offset<FlatGeobuf::Column>> headerColumns;

  std::unordered_map<string,uint16_t> keyToCol;

  for (int i = 0; i < columns.size(); i++) {
    keyToCol.insert({columns[i],i});
    headerColumns.push_back(CreateColumnDirect(fbBuilder, columns[i].c_str(), FlatGeobuf::ColumnType::String));
  }
  auto phColumns = headerColumns.size() == 0 ? nullptr : &headerColumns;

  auto crs = FlatGeobuf::CreateCrsDirect(fbBuilder,nullptr,4326);

  auto header = FlatGeobuf::CreateHeaderDirect(fbBuilder, "test dataset", &envelope, FlatGeobuf::GeometryType::Unknown, false, false, false, false, phColumns, 0, 0, crs);
  fbBuilder.FinishSizePrefixed(header);
  out->write((char *)fbBuilder.GetBufferPointer(),fbBuilder.GetSize());
  fbBuilder.Clear();

  osmium::area::AssemblerConfig config;
  config.ignore_invalid_locations = true;

  retval = mdb_cursor_get(cursor,&key,&data,MDB_FIRST);

  while (retval == 0) {
    auto arr = kj::ArrayPtr<const capnp::word>((const capnp::word *)data.mv_data,data.mv_size);
    capnp::FlatArrayMessageReader reader = capnp::FlatArrayMessageReader(arr);
    Relation::Reader relation = reader.getRoot<Relation>();

    bool is_boundary = false;
    auto tags = relation.getTags();
    for (int i = 0; i < tags.size() / 2; i++) {
      if (tags[i*2] == "boundary" && tags[i*2+1] == "administrative") {
        is_boundary = true;
      }
    }

    if (is_boundary) {
      uint64_t relation_id = *((uint64_t *)key.mv_data);
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
      if (a(relation_obj,members,buffer2)) { // ASSEMBLE SUCCESS
        auto const &result = buffer2.get<osmium::Area>(0);

        flatbuffers::Offset<FlatGeobuf::Geometry> geometry;

        // if it has 1 outer ring, it is a Polygon
        if (get<0>(result.num_rings()) == 1) {

          std::vector<uint32_t> ends_vector;
          std::vector<double> coords_vector;
          for (auto const &outer_ring : result.outer_rings()) {
            for (auto const coord : outer_ring) {
              coords_vector.push_back(coord.lon());
              coords_vector.push_back(coord.lat());
            }
            ends_vector.push_back(coords_vector.size()/2);

            for (auto const &inner_ring : result.inner_rings(outer_ring)) {
              for (auto const coord : inner_ring) {
                coords_vector.push_back(coord.lon());
                coords_vector.push_back(coord.lat());
              }
              ends_vector.push_back(coords_vector.size()/2);
            }
          }
          geometry = FlatGeobuf::CreateGeometryDirect(fbBuilder, &ends_vector, &coords_vector, nullptr, nullptr, nullptr, nullptr, FlatGeobuf::GeometryType::Polygon);

        } else {
          // ------------ MORE THAN ONE OUTER RING ---------------
          // if it has more than 1 outer ring, it is a MultiPolygon

          std::vector<flatbuffers::Offset<FlatGeobuf::Geometry>> parts;
          for (auto const &outer_ring : result.outer_rings()) {
            std::vector<uint32_t> ends_vector;
            std::vector<double> coords_vector;
            for (auto const coord : outer_ring) {
              coords_vector.push_back(coord.lon());
              coords_vector.push_back(coord.lat());
            }
            ends_vector.push_back(coords_vector.size()/2);

            for (auto const &inner_ring : result.inner_rings(outer_ring)) {
              for (auto const coord : inner_ring) {
                coords_vector.push_back(coord.lon());
                coords_vector.push_back(coord.lat());
              }
              ends_vector.push_back(coords_vector.size()/2);
            }
            auto geom_part = FlatGeobuf::CreateGeometryDirect(fbBuilder, &ends_vector, &coords_vector, nullptr, nullptr, nullptr, nullptr, FlatGeobuf::GeometryType::Polygon);
            parts.push_back(geom_part);
          }
          geometry = FlatGeobuf::CreateGeometryDirect(fbBuilder, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, FlatGeobuf::GeometryType::MultiPolygon, &parts);
        }

        std::vector<uint8_t> properties = tagsToProperties(keyToCol,tags);
        auto pProperties = properties.size() == 0 ? nullptr : &properties;

        auto feature = FlatGeobuf::CreateFeatureDirect(fbBuilder, geometry, pProperties, nullptr);
        fbBuilder.FinishSizePrefixed(feature);
        out->write((char *)fbBuilder.GetBufferPointer(),fbBuilder.GetSize());
        fbBuilder.Clear();
      }
    }

    retval = mdb_cursor_get(cursor,&key,&data,MDB_NEXT);
  }

  mdb_cursor_close(cursor);
  mdb_env_close(env); // close the database.

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::high_resolution_clock::now() - startTime ).count();
  cerr << "Finished protoshapes in " << duration/1000.0 << " seconds." << endl;
}
