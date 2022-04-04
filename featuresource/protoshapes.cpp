#include <vector>
#include <iomanip>
#include <fstream>
#include "osmx/storage.h"
#include "osmx/extract.h"
#include "osmium/area/assembler.hpp"
#include "osmium/osm/way.hpp"

using namespace std;

int main(int argc, char* argv[]) {

  vector<string> args(argv, argv+argc);

  MDB_env* env = osmx::db::createEnv(args[1]);
  MDB_txn* txn;
  CHECK(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn));

  auto startTime = std::chrono::high_resolution_clock::now();

  osmium::area::AssemblerConfig config;
  config.ignore_invalid_locations = true;
  osmx::db::Locations locations(txn);
  osmx::db::Elements nodes(txn,"nodes");
  osmx::db::Elements ways(txn,"ways");
  osmx::db::Elements relations(txn,"relations");

  MDB_dbi dbi;
  MDB_cursor *cursor;

  CHECK(mdb_dbi_open(txn, "relations", MDB_INTEGERKEY, &dbi));
  CHECK(mdb_cursor_open(txn,dbi,&cursor));


  MDB_val key, data;
  auto retval = mdb_cursor_get(cursor,&key,&data,MDB_FIRST);

  int count = 0;
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
        cout << "Rel " << relation_id << endl;
      }

      count++;
    }

    retval = mdb_cursor_get(cursor,&key,&data,MDB_NEXT);
  }

  cout << count << endl;

  mdb_cursor_close(cursor);
  mdb_env_close(env); // close the database.

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::high_resolution_clock::now() - startTime ).count();
  cerr << "Finished protoshapes in " << duration/1000.0 << " seconds." << endl;
}
