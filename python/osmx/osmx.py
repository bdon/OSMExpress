import sys
import os
import lmdb
import capnp

capnp.remove_import_hook()
messages_capnp = capnp.load(os.path.join(os.path.dirname(__file__), 'messages.capnp'))

def tag_dict(tag_list):
    it = enumerate(tag_list)
    d = {}
    for x in it:
        d[x[1]] = next(it)[1]
    return d

class Environment:
    def __init__(self,fname):
        self._handle = lmdb.Environment(fname,max_dbs=10,readonly=True,readahead=False,subdir=False)

class Transaction:
    def __init__(self,env):
        self.env = env
        self._handle = lmdb.Transaction(self.env._handle, buffers=True)

    def __enter__(self,*args,**kwargs):
        self._handle.__enter__(*args,**kwargs)
        return self

    def __exit__(self,*args,**kwargs):
        self._handle.__exit__(*args,**kwargs)

class Index:
    def __init__(self):
        pass

class Index:
    def __init__(self,txn,name):
        self.txn = txn
        self._handle = txn.env._handle.open_db(name,txn=txn._handle,integerkey=True,create=False,dupsort=True,integerdup=True,dupfixed=True)

    def __getitem__(self,obj_id):
        cursor = self.txn._handle.cursor(self._handle)
        cursor.set_key(int(obj_id).to_bytes(8,byteorder=sys.byteorder))
        retval = [int.from_bytes(data,byteorder=sys.byteorder,signed=False) for data in cursor.iternext_dup()]
        cursor.close()
        return retval

    # TODO deprecate get
    def get(self,obj_id):
        return self[obj_id]

class Table:
    def __init__(self,txn,name):
        self.txn = txn
        self._handle = txn.env._handle.open_db(name,txn=txn._handle,integerkey=True,create=False)

    def _get_bytes(self,elem_id):
        return self.txn._handle.get(int(elem_id).to_bytes(8,byteorder=sys.byteorder),db=self._handle)

    def __contains__(self,key):
        if self._get_bytes(key):
            return True
        return False

    def __getitem__(self,oid):
        msg = self._get_bytes(oid)
        if not msg:
            return None
        return self.msgclass.from_bytes(msg)

    # TODO deprecate get
    def get(self,oid):
        return self[oid]

    def __iter__(self):
        cursor = self.txn._handle.cursor(self._handle)
        cursor.first()
        for o in iter(cursor):
            obj_id = int.from_bytes(o[0],byteorder=sys.byteorder,signed=False)
            yield (obj_id,self.msgclass.from_bytes(o[1]))

class Locations(Table):
    def __init__(self,txn):
        super().__init__(txn,b'locations')

    def __getitem__(self,node_id):
        msg = self._get_bytes(node_id)
        if not msg:
            return None
        return (
            int.from_bytes(msg[4:8],byteorder=sys.byteorder,signed=True) / 10000000,
            int.from_bytes(msg[0:4],byteorder=sys.byteorder,signed=True) / 10000000,
            int.from_bytes(msg[8:12],byteorder=sys.byteorder,signed=False)
            )

    def __iter__(self):
        cursor = self.txn._handle.cursor(self._handle)
        cursor.first()
        for o in iter(cursor):
            obj_id = int.from_bytes(o[0],byteorder=sys.byteorder,signed=False)
            msg = o[1]
            yield (obj_id, (
                int.from_bytes(msg[4:8],byteorder=sys.byteorder,signed=True) / 10000000,
                int.from_bytes(msg[0:4],byteorder=sys.byteorder,signed=True) / 10000000,
                int.from_bytes(msg[8:12],byteorder=sys.byteorder,signed=False)
            ))

class Nodes(Table):
    def __init__(self,txn):
        super().__init__(txn,b'nodes')
        self.msgclass = messages_capnp.Node

class Ways(Table):
    def __init__(self,txn):
        super().__init__(txn,b'ways')
        self.msgclass = messages_capnp.Way

class Relations(Table):
    def __init__(self,txn):
        super().__init__(txn,b'relations')
        self.msgclass = messages_capnp.Relation

class NodeWay(Index):
    def __init__(self,txn):
        super().__init__(txn,b'node_way')

class NodeRelation(Index):
    def __init__(self,txn):
        super().__init__(txn,b'node_relation')

class WayRelation(Index):
    def __init__(self,txn):
        super().__init__(txn,b'way_relation')

class RelationRelation(Index):
    def __init__(self,txn):
        super().__init__(txn,b'relation_relation')
