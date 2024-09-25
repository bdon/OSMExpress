#include <iostream>
#include <cassert>
#include <set>
#include "cxxopts.hpp"
#include "roaring.hh"
#include "osmium/handler.hpp"
#include "osmium/io/any_input.hpp"
#include "osmium/visitor.hpp"
#include "osmium/util/progress_bar.hpp"
#include "s2/s2latlng.h"
#include "s2/s2cell_union.h"
#include "osmx/storage.h"

using namespace std;
using namespace osmx;

class DataUpdate : public osmium::handler::Handler {
  public:
  DataUpdate(MDB_txn *txn) : 
  mTxn(txn), 
  mLocations(txn), 
  mNodes(txn,"nodes"), 
  mWays(txn,"ways"), 
  mRelations(txn,"relations"),
  mCellNode(txn,"cell_node"),
  mNodeWay(txn,"node_way"),
  mNodeRelation(txn,"node_relation"),
  mWayRelation(txn,"way_relation"),
  mRelationRelation(txn, "relation_relation")  {
  }

  // update location, node, cell_location tables
  void node(const osmium::Node& node) {
    uint64_t id = node.id();
    db::Location prev_location = mLocations.get(id);
    db::Location new_location = db::Location{node.location(),(int32_t)node.version()};
    uint64_t prev_cell;
    if (prev_location.is_defined()) prev_cell = S2CellId(S2LatLng::FromDegrees(prev_location.coords.lat(),prev_location.coords.lon())).parent(CELL_INDEX_LEVEL).id();

    if (!node.visible()) {
      mLocations.del(id);
      mNodes.del(id);
      mCellNode.del(prev_cell,id);
      return;
    } else {
      mLocations.put(id,new_location);
      if (node.tags().size() > 0) {
        ::capnp::MallocMessageBuilder message;
        Node::Builder nodeMsg = message.initRoot<Node>();
        setTags<Node::Builder>(node.tags(),nodeMsg);
        auto metadata = nodeMsg.initMetadata();
        metadata.setVersion(node.version());
        metadata.setTimestamp(node.timestamp().seconds_since_epoch());
        metadata.setChangeset(node.changeset());
        metadata.setUid(node.uid());
        metadata.setUser(node.user());
        kj::VectorOutputStream output;
        capnp::writeMessage(output,message);
        mNodes.put(id,output);
      } else {
        mNodes.del(id); 
      }
    }

    uint64_t new_cell = S2CellId(S2LatLng::FromDegrees(new_location.coords.lat(),new_location.coords.lon())).parent(CELL_INDEX_LEVEL).id();
    if (!prev_location.is_defined()) {
      mCellNode.put(new_cell,id);
      return;
    }

    if (prev_cell != new_cell) {
      mCellNode.del(prev_cell,id);
      mCellNode.put(new_cell,id);
    }
  }

  // update way, node_way tables
  void way(const osmium::Way &way) {
    uint64_t id = way.id();

    set<uint64_t> prev_nodes;
    set<uint64_t> new_nodes;

    if (mWays.exists(id)) {
      auto reader = mWays.getReader(id);
      Way::Reader way = reader.getRoot<Way>();
      for (auto const &node_id : way.getNodes()) {
        prev_nodes.insert(node_id);
      }
    }

    if (!way.visible()) {
      mWays.del(id);
    } else {
      auto const &nodes = way.nodes();
      ::capnp::MallocMessageBuilder message;
      Way::Builder wayMsg = message.initRoot<Way>();
      wayMsg.initNodes(nodes.size());
      int i = 0;
      for (int i = 0; i < nodes.size(); i++) {
        wayMsg.getNodes().set(i,nodes[i].ref());
        new_nodes.insert(nodes[i].ref());
      }
      setTags<Way::Builder>(way.tags(),wayMsg);
      auto metadata = wayMsg.initMetadata();
      metadata.setVersion(way.version());
      metadata.setTimestamp(way.timestamp().seconds_since_epoch());
      metadata.setChangeset(way.changeset());
      metadata.setUid(way.uid());
      metadata.setUser(way.user());
      kj::VectorOutputStream output;
      capnp::writeMessage(output,message);
      mWays.put(id,output);
    }

    if (!way.visible()) {
      for (uint64_t node_id : prev_nodes) mNodeWay.del(node_id,id);
    } else {
      for (uint64_t node_id : prev_nodes) {
        if (new_nodes.count(node_id) == 0) mNodeWay.del(node_id,id);
      }
      for (uint64_t node_id : new_nodes) {
        if (prev_nodes.count(node_id) == 0) mNodeWay.put(node_id,id);
      }
    }
  }

  // update relation, node_relation, way_relation and relation_relation tables
  void relation(const osmium::Relation &relation) {
    uint64_t id = relation.id();

    set<uint64_t> prev_nodes;
    set<uint64_t> prev_ways;
    set<uint64_t> prev_relations;
    set<uint64_t> new_nodes;
    set<uint64_t> new_ways;
    set<uint64_t> new_relations;

    if (mRelations.exists(id)) {
      auto reader = mRelations.getReader(id);
      Relation::Reader relation = reader.getRoot<Relation>();
      for (auto const &member : relation.getMembers()) {
        if (member.getType() == RelationMember::Type::NODE) {
          prev_nodes.insert(member.getRef());
        } else if (member.getType() == RelationMember::Type::WAY) {
          prev_ways.insert(member.getRef());
        } else {
          prev_relations.insert(member.getRef());
        }
      }
    }

    if (!relation.visible()) {
      mRelations.del(relation.id());
    } else {
      ::capnp::MallocMessageBuilder message;
      Relation::Builder relationMsg = message.initRoot<Relation>();
      setTags<Relation::Builder>(relation.tags(),relationMsg);
      auto members = relationMsg.initMembers(relation.members().size());
      int i = 0;
      for (auto const &member : relation.members()) {
        members[i].setRef(member.ref());
        members[i].setRole(member.role());
        if (member.type() == osmium::item_type::node) {
          new_nodes.insert(member.ref());
          members[i].setType(RelationMember::Type::NODE);
        }
        else if (member.type() == osmium::item_type::way) {
          new_ways.insert(member.ref());
          members[i].setType(RelationMember::Type::WAY);
        }
        else if (member.type() == osmium::item_type::relation) {
          new_relations.insert(member.ref());
          members[i].setType(RelationMember::Type::RELATION);
        }
        i++;
      }
      auto metadata = relationMsg.initMetadata();
      metadata.setVersion(relation.version());
      metadata.setTimestamp(relation.timestamp().seconds_since_epoch());
      metadata.setChangeset(relation.changeset());
      metadata.setUid(relation.uid());
      metadata.setUser(relation.user());
      kj::VectorOutputStream output;
      capnp::writeMessage(output,message);
      mRelations.put(relation.id(),output);
    }

    if (!relation.visible()) {
      for (uint64_t node_id : prev_nodes) mNodeRelation.del(node_id,id);
      for (uint64_t way_id : prev_ways) mWayRelation.del(way_id,id);
      for (uint64_t relation_id : prev_relations) mRelationRelation.del(relation_id,id);
    } else {
      for (uint64_t node_id : prev_nodes) {
        if (new_nodes.count(node_id) == 0) mNodeRelation.del(node_id,id);
      }
      for (uint64_t node_id : new_nodes) {
        if (prev_nodes.count(node_id) == 0) mNodeRelation.put(node_id,id);
      }
      for (uint64_t way_id : prev_ways) {
        if (new_ways.count(way_id) == 0) mWayRelation.del(way_id,id);
      }
      for (uint64_t way_id : new_ways) {
        if (prev_ways.count(way_id) == 0) mWayRelation.put(way_id,id);
      }
      for (uint64_t relation_id : prev_relations) {
        if (new_relations.count(relation_id) == 0) mRelationRelation.del(relation_id,id);
      }
      for (uint64_t relation_id : new_relations) {
        if (prev_relations.count(relation_id) == 0) mRelationRelation.put(relation_id,id);
      }
    }
  }

  private:
  MDB_txn *mTxn;
  db::Locations mLocations;
  db::Elements mNodes;
  db::Elements mWays;
  db::Elements mRelations;
  db::Index mNodeWay;
  db::Index mNodeRelation;
  db::Index mWayRelation;
  db::Index mRelationRelation;
  db::Index mCellNode;
};

void cmdUpdate(int argc, char* argv[]) {
  cxxopts::Options cmdoptions("Update", "Update an .osmx file with a .osc diff.");
  cmdoptions.add_options()
    ("v,verbose", "Verbose output")
    ("commit", "Commit the update")
    ("cmd", "Command to run", cxxopts::value<string>())
    ("osmx", ".osmx to update", cxxopts::value<string>())
    ("osc", ".osc to apply", cxxopts::value<string>())
    ("seqnum", "The sequence number of the .osc", cxxopts::value<string>())
    ("timestamp", "The timestamp of the .osc", cxxopts::value<string>())
  ;

  cmdoptions.parse_positional({"cmd","osmx","osc","seqnum","timestamp"});
  auto result = cmdoptions.parse(argc, argv);

  if (result.count("osmx") == 0 || result.count("osc") == 0 || \
    result.count("seqnum") == 0 || result.count("timestamp") == 0) {
    cout << "Usage: osmx update OSMX_FILE OSC_FILE SEQNUM TIMESTAMP [OPTIONS]" << endl;
    cout << "Applies OSC_FILE and saves SEQNUM and TIMESTAMP into the metadata table." << endl << endl;
    cout << "EXAMPLE:" << endl;
    cout << " osmx update planet.osmx 123456.osc 123456 2019-09-05T00:00:00Z --commit" << endl << endl;
    cout << "OPTIONS:" << endl;
    cout << " --v,--verbose: verbose output." << endl;
    cout << " --commit: Actually commit the transaction; otherwise runs the update and rolls back." << endl;
    exit(1);
  }

  string osmx = result["osmx"].as<string>();
  string osc = result["osc"].as<string>();
  bool verbose = result.count("verbose") > 0;
  auto startTime = std::chrono::high_resolution_clock::now();

  MDB_env* env = db::createEnv(osmx,true);
  MDB_txn* txn;
  CHECK(mdb_txn_begin(env, NULL, 0, &txn));

  string old_seqnum = "UNKNOWN";
  auto new_seqnum = result["seqnum"].as<string>();
  auto new_timestamp = result["timestamp"].as<string>();
  db::Metadata metadata(txn);
  if (verbose) cout << "Timestamp: " << metadata.get("osmosis_replication_timestamp") << endl;
  old_seqnum = metadata.get("osmosis_replication_sequence_number");

  if (verbose) cout << "Starting update from " << old_seqnum << " to " << new_seqnum << endl;
  const osmium::io::File input_file{osc};

  osmium::io::Reader reader{input_file, osmium::osm_entity_bits::object};
  DataUpdate data_update(txn);
  osmium::apply(reader, data_update);
  
  auto duration = (std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::high_resolution_clock::now() - startTime ).count()) / 1000.0;

  if (result.count("commit") > 0) {
    {
      metadata.put("osmosis_replication_sequence_number",new_seqnum);
      metadata.put("osmosis_replication_timestamp",new_timestamp);
    }
    CHECK(mdb_txn_commit(txn));
    cout << "Committed: ";
  } else {
    mdb_txn_abort(txn);
    cout << "Aborted: ";
  }
  cout << old_seqnum << " -> " << new_seqnum << " in " << duration << " seconds." << endl;
  mdb_env_sync(env,true);
  mdb_env_close(env);
}

