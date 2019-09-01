#pragma once
#include "lmdb.h"
#include "osmium/osm/location.hpp"
#include "kj/io.h"
#include "capnp/message.h"
#include "capnp/serialize.h"
#include "osmx/messages.capnp.h"
#include "osmx/util.h"

namespace osmx { namespace db {


uint64_t to64(osmium::Location loc);
osmium::Location toLoc(uint64_t val);
MDB_env *createEnv(std::string path, bool writable = false);

class Noncopyable {
  public:
  Noncopyable() { }
  Noncopyable( const Noncopyable& ) = delete;
  Noncopyable& operator=( const Noncopyable& ) = delete;
};

class Metadata : public Noncopyable {
  public:
  Metadata(MDB_txn *txn);
  void put(const std::string &key_str, const std::string &value_str);
  std::string get(const std::string &key_str);

  private:
  MDB_txn* mTxn;
  MDB_dbi mDbi;
};

class Elements : public Noncopyable {
  public:
  Elements(MDB_txn *txn, const std::string &name);
  void put(uint64_t id, kj::VectorOutputStream &vos, int flags = 0);
  void del(uint64_t id);
  bool exists(uint64_t id);
  capnp::FlatArrayMessageReader getReader(uint64_t id);

  private:
  MDB_txn *mTxn;
  MDB_dbi mDbi;
};

class Locations : public Noncopyable {
  public:
  Locations(MDB_txn *txn);
  void put(uint64_t id, const osmium::Location value, int flags = 0);
  void del(uint64_t id);
  bool exists(uint64_t id);
  osmium::Location get(uint64_t id) const;

  private:
  MDB_txn* mTxn;
  MDB_dbi mDbi;
};

class Index : public Noncopyable {
  public:
  Index(MDB_txn *txn, const std::string &name);
  void put(uint64_t from, uint64_t osm_id, int flags = 0);
  void del(uint64_t from, uint64_t osm_id );

  private:
  MDB_dbi mDbi;
  MDB_txn *mTxn;
};

class IndexWriter : public Noncopyable {
  public:
  IndexWriter(MDB_env *env, const std::string &name);
  void put(uint64_t from, uint64_t osm_id, int flags = 0);
  void commit();

  private:
  MDB_env *mEnv;
  MDB_dbi mDbi;
  MDB_txn *mTxn;
  std::string mName;
  int mWrites = 0;
};

} }