#include "s2/s2cell_union.h"

struct ExportProgress {
  string timestamp = "";
  uint64_t cells_total = 0;
  uint64_t cells_prog = 0;
  uint64_t nodes_total = 0;
  uint64_t nodes_prog = 0;
  uint64_t elems_total = 0;
  uint64_t elems_prog = 0;

  void print() {
    std::cout << "{\"Timestamp\":\"" << timestamp << "\",\"CellsTotal\":" << cells_total << ",\"CellsProg\":" << cells_prog << ",\"NodesTotal\":" << nodes_total << ",\"NodesProg\":" << nodes_prog << ",\"ElemsTotal\":" << elems_total << ",\"ElemsProg\":" << elems_prog << "}" << std::endl;
  }
};

void entitiesForRegion(MDB_txn *txn, S2CellUnion &covering, Roaring64Map &node_ids, Roaring64Map &way_ids, Roaring64Map &relation_ids, bool jsonOutput, ExportProgress &prog);