#include <vector>
#include "osmx/storage.h"
#include "osmx/cmd.h"

using namespace std;
using namespace osmx;

int main(int argc, char* argv[]) {
  vector<string> args(argv, argv+argc);
  auto db_cmds = {"stat","node","way","relation"};
  if (args[1] == "expand") {
    cmdExpand(argc,argv);
  } else if (args[1] == "extract") {
    cmdExtract(argc,argv);
  } else if (args[1] == "update") {
    cmdUpdate(argc,argv);
  } else if (args[1] == "query") {
    if (argc == 2) {
      cout << "USAGE:" << endl;
      cout << " osmx query OSMX_FILE [OPTIONS]" << endl << endl;
      cout << "EXAMPLES:" << endl;
      cout << " osmx query planet.osmx" << endl;
      cout << " osmx query planet.osmx way 123456" << endl << endl;
      cout << "OPTIONS:" << endl;
      cout << " none specified: print table statistics." << endl;
      cout << " [node,way,relation] ID: print OSM object" << endl;
      cout << " timestamp: print data timestamp" << endl;
      cout << " seqnum: print replication seqence number" << endl;
      exit(1);
    }
    MDB_env* env = db::createEnv(args[2]);
    MDB_txn* txn;
    CHECK(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn));

    if (args[3] == "node") {
      auto id = stol(args[4]);
      auto location = db::Locations(txn).get(id);
      cout << location << endl;
      auto tags = db::Elements(txn,"nodes").getReader(id).getRoot<Node>().getTags();
      for (int i = 0; i < tags.size() / 2; i++) {
        cout << tags[i*2].cStr() << "=" << tags[i*2+1].cStr() << "\n";
      }
    } else if (args[3] == "way") {
      db::Elements ways(txn,"ways");
      auto message = ways.getReader(stol(args[4]));
      auto way = message.getRoot<Way>();
      for (auto node_id : way.getNodes()) {
        cout << node_id << " ";
      }
      cout << endl;
      auto tags = way.getTags();
      for (int i = 0; i < tags.size() / 2; i++) {
        cout << tags[i*2].cStr() << "=" << tags[i*2+1].cStr() << " ";
      }
      cout << endl;
    } else if (args[3] == "relation") {
      db::Elements relations(txn,"relations");
      uint64_t relation_id = stol(args[4]);
      auto message = relations.getReader(relation_id);
      auto relation = message.getRoot<Relation>();
      auto tags = relation.getTags();
      for (int i = 0; i < tags.size() / 2; i++) {
        cout << tags[i*2].cStr() << "=" << tags[i*2+1].cStr() << " ";
      }
      auto members = relation.getMembers();
      for (auto const &member : members) {
        cout << member.getRef() << endl;
      }
    } else if (args[3] == "timestamp") {
      db::Metadata metadata(txn);
      cout << metadata.get("osmosis_replication_timestamp") << endl;
    } else if (args[3] == "seqnum") {
      db::Metadata metadata(txn);
      cout << metadata.get("osmosis_replication_sequence_number") << endl;
    } else {
      auto tables = {"locations","nodes","ways","relations","cell_node","node_way","node_relation","way_relation","relation_relation"};
      for (auto const &table : tables) {
        MDB_dbi dbi;
        CHECK(mdb_dbi_open(txn, table, MDB_INTEGERKEY, &dbi));
        MDB_stat stat;
        CHECK(mdb_stat(txn,dbi,&stat));
        cout << table << ": " << stat.ms_entries << endl;
      }

      db::Metadata metadata(txn);
      cout << "Timestamp: " << metadata.get("osmosis_replication_timestamp") << endl;
      cout << "Sequence #: " << metadata.get("osmosis_replication_sequence_number") << endl;
    }

    mdb_env_sync(env,true);
    mdb_env_close(env);
  } else {
    cout << "Usage: osmx COMMAND [ARG...]" << endl << endl;
    cout << "COMMANDS:" << endl;
    cout << " expand   Convert an OSM PBF or XML to an osmx database." << endl;
    cout << " extract  Create a regional extract PBF from an osmx database." << endl;
    cout << " update   Apply an OSM changeset to an osmx database." << endl;
    cout << " query    Look up objects by ID in an osmx database." << endl;
  }
}
