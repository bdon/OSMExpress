#include "osmx/storage.h"

namespace osmx { namespace db {
  
uint64_t to64(osmium::Location loc) {
  uint64_t x = loc.x();
  return x << 32 | loc.y();
}

osmium::Location toLoc(uint64_t val) {
  return osmium::Location((int64_t)(val >> 32), (int64_t)(val & 0x00000000ffffffff));
}


MDB_env *createEnv(std::string path, bool writable) {
  MDB_env* env;
  CHECK(mdb_env_create(&env));

  // the maximum size of any LMDB dataset. 
  // 1TB is a safe number.
  // only affects the size of virtual memory, not real memory.
  mdb_env_set_mapsize(env,1UL * 1024UL * 1024UL * 1024UL * 1024UL);
  mdb_env_set_maxdbs(env,10);
  int flags = 0;
  if (!writable) flags |= MDB_RDONLY;
  CHECK(mdb_env_open(env, path.c_str(),MDB_NOSUBDIR | MDB_NORDAHEAD | MDB_NOSYNC | flags, 0664));
  return env;
}

Metadata::Metadata(MDB_txn *txn) : mTxn(txn) {
  CHECK(mdb_dbi_open(mTxn, "metadata", MDB_CREATE, &mDbi));
}

void Metadata::put(const std::string &key_str, const std::string &value_str) {
    MDB_val key, data;
    key.mv_size = key_str.size();
    key.mv_data = (void *)key_str.data();
    data.mv_size = value_str.size();
    data.mv_data = (void *)value_str.data();
    CHECK(mdb_put(mTxn,mDbi, &key, &data, 0));
}

std::string Metadata::get(const std::string &key_str) {
    MDB_val key, data;
    key.mv_size = key_str.size();
    key.mv_data = (void *)key_str.data();
    auto retval = mdb_get(mTxn,mDbi, &key, &data);
    if (retval == 0) return std::string((const char *)data.mv_data,data.mv_size);
    else return "";
}

Elements::Elements(MDB_txn *txn, const std::string &name) : mTxn(txn) {
  CHECK(mdb_dbi_open(txn, name.c_str(), MDB_INTEGERKEY | MDB_CREATE, &mDbi));
}

void Elements::put(uint64_t id, kj::VectorOutputStream &vos, int flags) {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&id;
  data.mv_size = vos.getArray().size();
  data.mv_data = (void *)vos.getArray().begin();
  CHECK(mdb_put(mTxn, mDbi, &key, &data, flags));
}

void Elements::del(uint64_t id) {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&id;
  mdb_del(mTxn, mDbi, &key, &data);
}

bool Elements::exists(uint64_t id) {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&id;
  return mdb_get(mTxn,mDbi,&key,&data) == 0;
}

capnp::FlatArrayMessageReader Elements::getReader(uint64_t id) {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&id;
  CHECK(mdb_get(mTxn,mDbi,&key,&data));
  auto arr = kj::ArrayPtr<const capnp::word>((const capnp::word *)data.mv_data,data.mv_size);
  return capnp::FlatArrayMessageReader(arr);
}

Locations::Locations(MDB_txn *txn) : mTxn(txn) {
    CHECK(mdb_dbi_open(mTxn, "locations", MDB_INTEGERKEY | MDB_CREATE, &mDbi));
}

void Locations::put(uint64_t id, const osmium::Location value, int flags) {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&id;
  uint64_t as64 = to64(value);
  data.mv_size = sizeof(uint64_t);
  data.mv_data = (void *)&as64;
  CHECK(mdb_put(mTxn, mDbi, &key, &data, flags));
}

void Locations::del(uint64_t id) {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&id;
  mdb_del(mTxn,mDbi,&key,&data);
}

osmium::Location Locations::get(uint64_t id) const {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&id;
  int retval = mdb_get(mTxn, mDbi, &key, &data);
  if (retval == MDB_NOTFOUND) return osmium::Location{};
  CHECK(retval);
  osmium::Location v = toLoc(*((uint64_t *)data.mv_data));
  return v;
}

bool Locations::exists(uint64_t id) {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&id;
  int retval = mdb_get(mTxn, mDbi, &key, &data);
  return retval != MDB_NOTFOUND;
}

Index::Index(MDB_txn *txn, const std::string &name) : mTxn(txn) {
  CHECK(mdb_dbi_open(txn, name.c_str(), MDB_INTEGERKEY | MDB_CREATE | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP, &mDbi));
}

void Index::put(uint64_t from, uint64_t osm_id, int flags) {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&from;
  data.mv_size = sizeof(uint64_t);
  data.mv_data = (void *)&osm_id;
  CHECK(mdb_put(mTxn,mDbi,&key,&data,flags));
}

void Index::del(uint64_t from, uint64_t osm_id ) {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&from;
  data.mv_size = sizeof(uint64_t);
  data.mv_data = (void *)&osm_id;
  mdb_del(mTxn,mDbi,&key,&data);
}

IndexWriter::IndexWriter(MDB_env *env, const std::string &name) : mEnv(env), mName(name) {
  CHECK(mdb_txn_begin(env, NULL, 0, &mTxn));
  CHECK(mdb_dbi_open(mTxn, name.c_str(), MDB_INTEGERKEY | MDB_CREATE | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP, &mDbi));
}

void IndexWriter::put(uint64_t from, uint64_t osm_id, int flags) {
  MDB_val key, data;
  key.mv_size = sizeof(uint64_t);
  key.mv_data = (void *)&from;
  data.mv_size = sizeof(uint64_t);
  data.mv_data = (void *)&osm_id;
  CHECK(mdb_put(mTxn,mDbi,&key,&data,flags));
  if (mWrites++ == 8000000) {
    CHECK(mdb_txn_commit(mTxn));
    CHECK(mdb_txn_begin(mEnv, NULL, 0, &mTxn));
    CHECK(mdb_dbi_open(mTxn, mName.c_str(), MDB_INTEGERKEY | MDB_CREATE | MDB_DUPSORT | MDB_DUPFIXED | MDB_INTEGERDUP, &mDbi));
    mWrites = 0;
  }
}

void IndexWriter::commit() {
  CHECK(mdb_txn_commit(mTxn));
}

}}
