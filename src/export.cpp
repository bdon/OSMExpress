#include <string>

#include "roaring.hh"
#include "lmdb.h"
#include "s2/s2latlng.h"
#include "s2/s2region_coverer.h"
#include "s2/s2latlng_rect.h"

#include "kj/io.h"
#include "capnp/message.h"
#include "messages.capnp.h"
#include "capnp/serialize.h"

#include "util.h"

#include "osmium/io/any_output.hpp"
#include "osmium/util/progress_bar.hpp"
#include "osmium/memory/callback_buffer.hpp"
#include "osmium/builder/attr.hpp"
#include "osmium/builder/osm_object_builder.hpp"

#include "nlohmann/json.hpp"
#include "storage.h"
#include <cxxopts.hpp>

// small:  {\"bbox\":[40.7411\,-73.9937\,40.7486\,-73.9821]}
// big:    {\"bbox\":[40.6762\,-74.0543\,40.8093\,-73.8603]}
// radius: {"center":[40.7411,-73.9937],"radius":25.5}
// indo:  {\"bbox\":[-12.039321\,94.394531\,8.407168\,142.418292]}

using namespace std;
using namespace osmx;

struct ExportProgress {
  string timestamp = "";
  uint64_t cells_total = 0;
  uint64_t cells_prog = 0;
  uint64_t nodes_total = 0;
  uint64_t nodes_prog = 0;
  uint64_t elems_total = 0;
  uint64_t elems_prog = 0;

  void print() {
    cout << "{\"timestamp\":\"" << timestamp << "\",\"cells_total\":" << cells_total << ",\"cells_prog\":" << cells_prog << ",\"nodes_total\":" << nodes_total << ",\"nodes_prog\":" << nodes_prog << ",\"elems_total\":" << elems_total << ",\"elems_prog\":" << elems_prog << "}" << endl;
  }
};

class ProgressSection {

public:
  ProgressSection(ExportProgress &expprog, uint64_t &total, uint64_t &prog, uint64_t total_to_set, bool jsonOutput) : expprog(expprog), total(total), prog(prog), progressbar(total_to_set, osmium::isatty(2) && !jsonOutput), jsonOutput(jsonOutput) {
    total = total_to_set;
  }

  ~ProgressSection() {
    progressbar.done();
  }

  void tick() {
    prog++;
    if (prog - last_prog > (total / 100)) {
      if (jsonOutput) expprog.print();
      else progressbar.update(prog);
      last_prog = prog;
    }
  }

private:
    osmium::ProgressBar progressbar;
    uint64_t& prog;
    uint64_t& total;
    bool jsonOutput;
    ExportProgress &expprog;
    uint64_t last_prog = 0;
};



void traverseCell(MDB_cursor *cursor,S2CellId cell_id,Roaring64Map &set) {
  S2CellId start = cell_id.child_begin(CELL_INDEX_LEVEL);
  S2CellId end = cell_id.child_end(CELL_INDEX_LEVEL);
  MDB_val key, data;
  key.mv_size = sizeof(S2CellId);
  key.mv_data = (void *)&start;
  CHECK(mdb_cursor_get(cursor,&key,&data,MDB_SET_RANGE));
  while (*((S2CellId *)key.mv_data) < end) {
    int retval_values = mdb_cursor_get(cursor,&key,&data,MDB_GET_MULTIPLE);
    while (0 == retval_values) {
      for (int i = 0; i < data.mv_size/sizeof(uint64_t); i++) {
        uint64_t *d = (uint64_t*)data.mv_data;
        set.add(*(d+i));
      }
      retval_values = mdb_cursor_get(cursor,&key,&data,MDB_NEXT_MULTIPLE);
    }
    mdb_cursor_get(cursor,&key,&data,MDB_NEXT_NODUP);
  }
}

void traverseReverse(MDB_cursor *cursor,uint64_t from, Roaring64Map &set) {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&from;

  if (mdb_cursor_get(cursor,&key,&data,MDB_SET) != 0) return;
  int retval_values = mdb_cursor_get(cursor,&key,&data,MDB_GET_MULTIPLE);
  while (0 == retval_values) {
    for (int i = 0; i < data.mv_size/sizeof(uint64_t); i++) {
      uint64_t *d = (uint64_t*)data.mv_data;
      uint64_t to_id = *(d+i);
      set.add(to_id);
    }
    retval_values = mdb_cursor_get(cursor,&key,&data,MDB_NEXT_MULTIPLE);
  }
}

void cmdExport(int argc, char * argv[]) {
  cxxopts::Options cmd_options("Import", "Convert a a .pbf into an .osmx.");
  cmd_options.add_options()
    ("v,verbose", "Verbose output")
    ("json", "JSON progress output")
    ("cmd", "Command to run", cxxopts::value<string>())
    ("osmx", "Input .osmx", cxxopts::value<string>())
    ("output", "Output file, pbf or xml", cxxopts::value<string>())
    ("boundary", "Boundary JSON", cxxopts::value<string>())
  ;
  cmd_options.parse_positional({"cmd","osmx","output","boundary"});
  auto result = cmd_options.parse(argc, argv);

  auto startTime = std::chrono::high_resolution_clock::now();
  ExportProgress prog;
  string err;

  bool jsonOutput = result.count("json") > 0;
  if (jsonOutput) prog.print();

  auto json = nlohmann::json::parse(result["boundary"].as<string>());
  if (!json.is_object()) {
    if (!jsonOutput) cout << "JSON must be Object." << endl;
    exit(1);
  }

  unique_ptr<S2Region> region;
  if (json.count("bbox") > 0) {
    auto coords = json["bbox"];
    auto lo = S2LatLng::FromDegrees(coords[0].get<double>(),coords[1].get<double>());
    auto hi = S2LatLng::FromDegrees(coords[2].get<double>(),coords[3].get<double>());
    region = make_unique<S2LatLngRect>(lo,hi);
  } else if (json.count("radius") > 0) {

  } else if (json.count("coordinates") > 0) {
    // bbox: {"type":"Polygon","coordinates":[]}
  }

  S2RegionCoverer::Options options;
  options.set_max_cells(1024);
  options.set_max_level(CELL_INDEX_LEVEL);
  S2RegionCoverer coverer(options);
  S2CellUnion covering = coverer.GetCovering(*region);
  if (!jsonOutput) {
    cout << "Query cells: " << covering.cell_ids().size() << endl;
  }

  Roaring64Map node_ids;
  Roaring64Map way_ids;
  Roaring64Map relation_ids;

  MDB_env* env = db::createEnv(result["osmx"].as<string>(),false);
  MDB_txn* txn;
  CHECK(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn));

  db::Metadata metadata(txn);
  prog.timestamp = metadata.get("osmosis_replication_timestamp");
  if (!jsonOutput) {
    cout << "Snapshot timestamp is " << prog.timestamp  << endl;
  }

  {
    ProgressSection section(prog,prog.cells_total,prog.cells_prog,covering.size(),jsonOutput);
    MDB_dbi dbi;
    MDB_cursor *cursor;
    CHECK(mdb_dbi_open(txn, "cell_node", MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP, &dbi));
    CHECK(mdb_cursor_open(txn,dbi,&cursor));
    for (auto cell_id : covering.cell_ids()) {
      traverseCell(cursor,cell_id,node_ids);
      section.tick();
    }
    mdb_cursor_close(cursor);
  }

  {
    ProgressSection section(prog,prog.nodes_total,prog.nodes_prog,node_ids.cardinality(),jsonOutput);
    MDB_dbi dbi;
    MDB_cursor *cursor;
    CHECK(mdb_dbi_open(txn, "node_way", MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP, &dbi));
    CHECK(mdb_cursor_open(txn,dbi,&cursor));
    for (auto const &node_id : node_ids) {
      traverseReverse(cursor,node_id,way_ids);
      section.tick();
    }
  }

  if (!jsonOutput) cout << "Ways: " << way_ids.cardinality() << endl;

  // find all Relations that these nodes or Ways are a member of.
  {
    MDB_dbi dbi;
    MDB_cursor *cursor;
    CHECK(mdb_dbi_open(txn, "node_relation", MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP, &dbi));
    CHECK(mdb_cursor_open(txn,dbi,&cursor));
    for (auto const &node_id : node_ids) {
      traverseReverse(cursor,node_id,relation_ids);
    }
  }

  {
    MDB_dbi dbi;
    MDB_cursor *cursor;
    CHECK(mdb_dbi_open(txn, "way_relation", MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP, &dbi));
    CHECK(mdb_cursor_open(txn,dbi,&cursor));
    for (auto const &way_id : way_ids) {
      traverseReverse(cursor,way_id,relation_ids);
    }
  }

  {
    MDB_dbi dbi;
    MDB_cursor *cursor;
    CHECK(mdb_dbi_open(txn, "relation_relation", MDB_INTEGERKEY | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP, &dbi));
    CHECK(mdb_cursor_open(txn,dbi,&cursor));
    Roaring64Map discovered_relations;
    Roaring64Map discovered_relations_2;

    for (auto const &relation_id : relation_ids) {
      traverseReverse(cursor,relation_id,discovered_relations);
    }

    relation_ids |= discovered_relations;

    while(true) {
      for (auto const &relation_id : discovered_relations) {
        traverseReverse(cursor,relation_id,discovered_relations_2);
      }
      int num_discovered = 0;
      for (auto discovered_relation_id : discovered_relations_2) {
        if (relation_ids.addChecked(discovered_relation_id)) num_discovered++;
      }
      if (num_discovered == 0) break;
      discovered_relations = discovered_relations_2;
      discovered_relations_2.clear();
    }
  }

  if (!jsonOutput) cout << "Relations: " << relation_ids.cardinality() << endl;
  db::Elements ways(txn,"ways");

  // make it Way-complete: go through all Ways and add in any missing Nodes.

  {
    for (auto way_id : way_ids) {
      auto reader = ways.getReader(way_id);
      Way::Reader way = reader.getRoot<Way>();
      for (auto node_id : way.getNodes()) {
        node_ids.add(node_id);
      }
    }
  }

  if (!jsonOutput) cout << "Nodes: " << node_ids.cardinality() << endl;

  // start Write

  osmium::io::Header header;
  header.set("generator", "osmx 0.0.1");
  osmium::io::Writer writer{result["output"].as<string>(), header, osmium::io::overwrite::allow};
  osmium::memory::CallbackBuffer cb;
  cb.set_callback([&](osmium::memory::Buffer&& buffer) {
    writer(move(buffer));
  });

  {
    ProgressSection section(prog,prog.elems_total,prog.elems_prog,node_ids.cardinality() + way_ids.cardinality() + relation_ids.cardinality(),jsonOutput);

    {
      db::Locations location_index(txn);
      db::Elements nodes_table(txn,"nodes");
      for (auto node_id : node_ids) {
        section.tick();
        auto loc = location_index.get(node_id);
        if (loc.is_undefined()) continue;

        {
          using namespace osmium::builder::attr; 
          osmium::builder::NodeBuilder node_builder{cb.buffer()};
          node_builder.set_id(node_id);
          node_builder.set_location(loc);

          if (!nodes_table.exists(node_id)) continue;
          auto reader = nodes_table.getReader(node_id);
          Node::Reader node = reader.getRoot<Node>();
          auto tags = node.getTags();
          osmium::builder::TagListBuilder tag_builder{node_builder};
          for (int i = 0; i < tags.size() / 2; i++) {
            tag_builder.add_tag(tags[i*2],tags[i*2+1]);
          }
        }
        cb.buffer().commit();
        cb.possibly_flush();
      }
    }
    
    // Writing ways pass
    {
      for (auto way_id : way_ids) {
        section.tick();
        auto reader = ways.getReader(way_id);
        Way::Reader way = reader.getRoot<Way>();

        {
          using namespace osmium::builder::attr; 
          osmium::builder::WayBuilder way_builder{cb.buffer()};
          way_builder.set_id(way_id);

          {
            osmium::builder::WayNodeListBuilder way_node_list_builder{way_builder};
            for (auto node_id : way.getNodes()) {
              way_node_list_builder.add_node_ref(node_id);
            }
          }

          auto tags = way.getTags();
          osmium::builder::TagListBuilder tag_builder{way_builder};
          for (int i = 0; i < tags.size() / 2; i++) {
            tag_builder.add_tag(tags[i*2],tags[i*2+1]);
          }
        }
        cb.buffer().commit();
        cb.possibly_flush();
      }
    }

    {
      db::Elements relations(txn,"relations");
      for (auto relation_id : relation_ids) {
        section.tick();
        auto reader = relations.getReader(relation_id);
        Relation::Reader relation = reader.getRoot<Relation>();

        {
          using namespace osmium::builder::attr; 
          osmium::builder::RelationBuilder relation_builder{cb.buffer()};
          relation_builder.set_id(relation_id);

          {
            osmium::builder::RelationMemberListBuilder relation_member_list_builder{relation_builder};
            for (auto const &member : relation.getMembers()) {
              if (member.getType() == RelationMember::Type::NODE) {
                relation_member_list_builder.add_member(osmium::item_type::node,member.getRef(),member.getRole());
              } else if (member.getType() == RelationMember::Type::WAY) {
                relation_member_list_builder.add_member(osmium::item_type::way,member.getRef(),member.getRole());
              } else {
                relation_member_list_builder.add_member(osmium::item_type::relation,member.getRef(),member.getRole());
              }
            }
          }

          auto tags = relation.getTags();
          osmium::builder::TagListBuilder tag_builder{relation_builder};
          for (int i = 0; i < tags.size() / 2; i++) {
            tag_builder.add_tag(tags[i*2],tags[i*2+1]);
          }
        }
        cb.buffer().commit();
        cb.possibly_flush();
      }
    }
  }


  cb.flush();
  writer.close();
  mdb_env_close(env);
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::high_resolution_clock::now() - startTime ).count();
  if (!jsonOutput) cout << "Finished export in " << duration/1000.0 << " seconds." << endl;
}
