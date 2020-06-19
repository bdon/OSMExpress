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

    def get(self,obj_id):
        cursor = self.txn._handle.cursor(self._handle)
        cursor.set_key(int(obj_id).to_bytes(8,byteorder=sys.byteorder))
        retval = [int.from_bytes(data,byteorder=sys.byteorder,signed=False) for data in cursor.iternext_dup()]
        cursor.close()
        return retval

class Table:
    def __init__(self,txn,name):
        self.txn = txn
        self._handle = txn.env._handle.open_db(name,txn=txn._handle,integerkey=True,create=False)

    def _get_bytes(self,elem_id):
        return self.txn._handle.get(int(elem_id).to_bytes(8,byteorder=sys.byteorder),db=self._handle)

class Locations(Table):
    def __init__(self,txn):
        super().__init__(txn,b'locations')

    def get(self,node_id):
        msg = self._get_bytes(node_id)
        if not msg:
            return None
        return (
            int.from_bytes(msg[4:8],byteorder=sys.byteorder,signed=True) / 10000000,
            int.from_bytes(msg[0:4],byteorder=sys.byteorder,signed=True) / 10000000,
            int.from_bytes(msg[8:12],byteorder=sys.byteorder,signed=False)
            )

class Nodes(Table):
    def __init__(self,txn):
        super().__init__(txn,b'nodes')

    def get(self,node_id):
        msg = self._get_bytes(node_id)
        if not msg:
            return None
        return messages_capnp.Node.from_bytes(msg)

class Ways(Table):
    def __init__(self,txn):
        super().__init__(txn,b'ways')

    def get(self,way_id):
        msg = self._get_bytes(way_id)
        if not msg:
            return None
        return messages_capnp.Way.from_bytes(msg)

class Relations(Table):
    def __init__(self,txn):
        super().__init__(txn,b'relations')

    def get(self,relation_id):
        msg = self._get_bytes(relation_id)
        if not msg:
            return None
        return messages_capnp.Relation.from_bytes(msg)

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
