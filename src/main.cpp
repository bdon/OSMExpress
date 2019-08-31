#include <vector>
#include "lmdb.h"

#include "util.h"
#include "osmx.h"

#include "storage.h"

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
  } else if (find(db_cmds.begin(),db_cmds.end(),args[1])) {
    MDB_env* env = db::createEnv(args[2]);
    MDB_txn* txn;
    CHECK(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn));

    if (args[1] == "node") {
      auto id = stol(args[3]);
      auto location = db::Locations(txn).get(id);
      cout << location << endl;
      auto tags = db::Elements(txn,"nodes").getReader(id).getRoot<Node>().getTags();
      for (int i = 0; i < tags.size() / 2; i++) {
        cout << tags[i*2].cStr() << "=" << tags[i*2+1].cStr() << "\n";
      }
    } else if (args[1] == "way") {
      db::Elements ways(txn,"ways");
      auto message = ways.getReader(stol(args[3]));
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
    } else if (args[1] == "relation") {
      db::Elements relations(txn,"relations");
      uint64_t relation_id = stol(args[3]);
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
    } else if (args[1] == "timestamp") {
      db::Metadata metadata(txn);
      cout << metadata.get("osmosis_replication_timestamp") << endl;
    } else if (args[1] == "seqnum") {
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
  }
}
