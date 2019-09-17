#include <iomanip>
#include <fstream>
#include "osmium/handler.hpp"
#include "osmium/visitor.hpp"
#include "osmium/io/any_input.hpp"
#include "osmium/util/progress_bar.hpp"
#include "osmium/io/reader_with_progress_bar.hpp"
#include "cxxopts.hpp"
#include "kj/io.h"
#include "capnp/message.h"
#include "capnp/serialize.h"
#include "s2/s2latlng.h"
#include "s2/s2cell_id.h"
#include "osmx/storage.h"
#include "osmx/messages.capnp.h"

using namespace std;
using namespace osmx;


typedef std::pair<uint64_t, uint64_t> Pair; 
typedef std::pair<Pair, uint64_t> pqelem; 

class SortReader {
  public:
  SortReader(std::string filename) : mStream(filename, std::ios::in | std::ios::binary) { }

  bool getNext() {
    mStream.read((char *)&entry,sizeof(uint64_t) *2);
    if (mStream.eof()) return false;
    return true;
  }

  Pair entry;

  private:
  std::ifstream mStream;
};

class Sorter {
int MAX_RUN_SIZE = 64000000; // about 1 GB
public:
  Sorter(std::string tempDir,std::string name) : mTempDir(tempDir), mName(name) { 
    mStorage.reserve(MAX_RUN_SIZE);
  }

  void put(uint64_t from, uint64_t to) {
    mStorage.push_back(std::make_pair(from,to));
    if (mStorage.size() > MAX_RUN_SIZE) persist();
  }

  void put(S2CellId from, uint64_t to) {
    put(from.id(),to);
  }

  void persist() {
    if (mStorage.size() == 0) return;
    sort(mStorage.begin(),mStorage.end());
    int runNumber = mSavedRuns.size();
    std::ofstream stream;
    std::stringstream fname;
    fname << mTempDir << "/" << std::setw(2) << std::setfill('0') << mName << "_" << std::setw(3) << std::setfill('0') << runNumber << ".run";
    stream.open(fname.str(),std::ios::binary);
    for (auto const &entry: mStorage) {
      stream.write((char *)&entry.first,sizeof(uint64_t));
      stream.write((char *)&entry.second,sizeof(uint64_t));
    }
    stream.close();
    mStorage.clear();
    mStorage.reserve(MAX_RUN_SIZE);
    mSavedRuns.push_back(fname.str());
  }

  void writeDb(MDB_env *env) {
    persist();

    Timer timer("External sort " + mName);
    osmium::ProgressBar progress{MAX_RUN_SIZE * mSavedRuns.size(), osmium::isatty(2)};
    int read = 0;
    std::priority_queue<pqelem, std::vector<pqelem>, std::greater<pqelem>> q;
    std::vector<SortReader> readers;
    db::IndexWriter index(env,mName);

    for (int i = 0; i < mSavedRuns.size(); i++) {
      readers.emplace_back(mSavedRuns[i]);  
      if (readers[i].getNext()) q.push(make_pair(readers[i].entry, i));
    }

    Pair last;

    while (q.size() > 0) {
      pqelem pair = q.top();
      auto idx = pair.second;
      if (pair.first != last) {
        if (pair.first.first != last.first) index.put(pair.first.first,pair.first.second,MDB_APPEND);
        else index.put(pair.first.first,pair.first.second,MDB_APPENDDUP);
      }
      q.pop();
      if (readers[idx].getNext()) q.push(make_pair(readers[idx].entry, idx));
      progress.update(read++);
      last = pair.first;
    }

    index.commit();

    progress.done();

    for (auto const &run : mSavedRuns) {
      remove(run.c_str());
    }
  }

private:
  Sorter( const Sorter& ) = delete;
  Sorter& operator=( const Sorter& ) = delete;
  std::vector<std::pair<uint64_t,uint64_t>> mStorage;
  int mRunNumber = 0;
  std::vector<std::string> mSavedRuns;
  std::string mTempDir;
  std::string mName;
};

class Handler: public osmium::handler::Handler {
  public:
  Handler(MDB_env *env, MDB_txn *txn,string tempDir) : 
    mEnv(env),
    mTxn(txn),
    mCellNode(tempDir,"cell_node"), 
    mLocations(txn), 
    mNodes(txn,"nodes"),
    mWays(txn,"ways"),
    mRelations(txn,"relations"),
    mNodeWay(tempDir,"node_way"),
    mNodeRelation(tempDir,"node_relation"),
    mWayRelation(tempDir,"way_relation"),
    mRelationRelation(tempDir,"relation_relation")
  {
  }

  ~Handler() {
    CHECK(mdb_txn_commit(mTxn));
    mCellNode.writeDb(mEnv);
    mNodeWay.writeDb(mEnv);
    mNodeRelation.writeDb(mEnv);
    mWayRelation.writeDb(mEnv);
    mRelationRelation.writeDb(mEnv);
  }

  void node(const osmium::Node& node) {
    mLocations.put(node.id(), node.location(),MDB_APPEND);
    auto loc = node.location();
    auto ll = S2LatLng::FromDegrees(loc.lat(),loc.lon());
    auto cell = S2CellId(ll).parent(CELL_INDEX_LEVEL);
    mCellNode.put(cell,node.id());

    if (node.tags().size() > 0) {
      ::capnp::MallocMessageBuilder message;
      Node::Builder nodeMsg = message.initRoot<Node>();
      setTags<Node::Builder>(node.tags(),nodeMsg);
      kj::VectorOutputStream output;
      capnp::writeMessage(output,message);
      mNodes.put(node.id(),output,MDB_APPEND);
    }
  }

  void way(const osmium::Way& way) {
  	auto const &nodes = way.nodes();
    ::capnp::MallocMessageBuilder message;
    Way::Builder wayMsg = message.initRoot<Way>();
    wayMsg.initNodes(nodes.size());
    int i = 0;
    for (int i = 0; i < nodes.size(); i++) {
       wayMsg.getNodes().set(i,nodes[i].ref());
       mNodeWay.put(nodes[i].ref(),way.id());
    }
    setTags<Way::Builder>(way.tags(),wayMsg);
    kj::VectorOutputStream output;
    capnp::writeMessage(output,message);
    mWays.put(way.id(),output,MDB_APPEND);
  }

  void relation(const osmium::Relation& relation) {
    ::capnp::MallocMessageBuilder message;
    Relation::Builder relationMsg = message.initRoot<Relation>();
    setTags<Relation::Builder>(relation.tags(),relationMsg);
    auto members = relationMsg.initMembers(relation.members().size());
    int i = 0;
    for (auto const &member : relation.members()) {
      members[i].setRef(member.ref());
      members[i].setRole(member.role());
      if (member.type() == osmium::item_type::node) {
        members[i].setType(RelationMember::Type::NODE);
        mNodeRelation.put(member.ref(),relation.id());
      }
      else if (member.type() == osmium::item_type::way) {
        members[i].setType(RelationMember::Type::WAY);
        mWayRelation.put(member.ref(),relation.id());
      }
      else if (member.type() == osmium::item_type::relation) {
        members[i].setType(RelationMember::Type::RELATION);
        mRelationRelation.put(member.ref(),relation.id());
      }
      i++;
    }
    kj::VectorOutputStream output;
    capnp::writeMessage(output,message);
    mRelations.put(relation.id(),output,MDB_APPEND);
  }

  private:
  MDB_env* mEnv;
  MDB_txn* mTxn;
  Sorter mCellNode;
  db::Locations mLocations;

  db::Elements mNodes;
  db::Elements mWays;
  db::Elements mRelations;

  Sorter mNodeWay;
  Sorter mNodeRelation;
  Sorter mWayRelation;
  Sorter mRelationRelation;
};

void cmdExpand(int argc, char* argv[]) {
  cxxopts::Options options("Expand", "Expand a a .osm.pbf into an .osmx file");
  options.add_options()
    ("v,verbose", "Verbose output")
    ("cmd", "Command to run", cxxopts::value<string>())
    ("input", "Input .pbf", cxxopts::value<string>())
    ("output", "Output .osmx", cxxopts::value<string>())
  ;
  options.parse_positional({"cmd","input", "output"});
  auto result = options.parse(argc, argv);

  if (result.count("input") == 0 || result.count("output") == 0) {
    cout << "Usage: osmx expand OSM_FILE OSMX_FILE [OPTIONS]" << endl << endl;
    cout << "OSM_FILE must be an OSM XML or PBF." << endl << endl;
    cout << "EXAMPLE:" << endl;
    cout << " osmx expand planet_latest.osm.pbf planet.osmx" << endl << endl;
    cout << "OPTIONS:" << endl;
    cout << " --v,--verbose: verbose output." << endl;
    exit(1);
  }

  string input =result["input"].as<string>();
  string output = result["output"].as<string>();

  Timer timer("convert");
  MDB_env* env = db::createEnv(output,true);
  MDB_txn* txn;
  CHECK(mdb_txn_begin(env, NULL, 0, &txn));

  const osmium::io::File input_file{input};
  osmium::io::ReaderWithProgressBar reader{true, input_file, osmium::osm_entity_bits::object, osmium::io::read_meta::no};

  db::Metadata metadata(txn);
  auto header = reader.header();

  for (auto option : header) {
    cout << option.first << " " << option.second << endl;
  }
  cout << "Box: " << header.box() << endl;
  cout << "Timestamp: " << header.get("osmosis_replication_timestamp") << endl;
  cout << "Sequence#: " << header.get("osmosis_replication_sequence_number") << endl;
  metadata.put("osmosis_replication_timestamp",header.get("osmosis_replication_timestamp"));
  metadata.put("osmosis_replication_sequence_number",header.get("osmosis_replication_sequence_number"));
  metadata.put("import_filename",input);
  string tempDir = output + "-temp";
  assert(mkdir(tempDir.c_str(),S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0);

  {
    Timer insert("insert");
    Handler handler(env,txn,tempDir);
    osmium::apply(reader, handler);
  }

  assert(rmdir(tempDir.c_str()) == 0);
}
