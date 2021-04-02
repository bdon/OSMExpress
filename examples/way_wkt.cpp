#include <vector>
#include <iomanip>
#include "osmx/storage.h"

using namespace std;

// Example of a very simple C++ program that uses osmx headers
// to open a database, look up a way by ID, and assemble a WKT geometry from its nodes.
// Usage: ./print_wkt OSMX_FILE WAY_ID

int main(int argc, char* argv[]) {
  vector<string> args(argv, argv+argc);

  // Opening a database: create an Environment, and then a Transaction within the environment. 
  MDB_env* env = osmx::db::createEnv(args[1]);
  MDB_txn* txn;
  CHECK(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn));

  // Create a Database handle for each element type within the Transaction.
  osmx::db::Locations locations(txn);
  osmx::db::Elements ways(txn,"ways");

  // Fetch a Way element by ID.
  auto message = ways.getReader(stol(args[2]));
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

  mdb_env_close(env); // close the database.
}
