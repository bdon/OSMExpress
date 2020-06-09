import sys
import osmx

if len(sys.argv) <= 1:
    print("Usage: read_way.py OSMX_FILE WAY_ID")
    exit(1)

env  = osmx.Environment(sys.argv[1])
with osmx.Transaction(env) as txn:
    locations = osmx.Locations(txn)
    nodes = osmx.Nodes(txn)
    ways = osmx.Ways(txn)

    way = ways.get(sys.argv[2])

    for node_id in way.nodes:
        print(locations.get(node_id))

    print(osmx.tag_dict(way.tags))
    print(way.metadata)
