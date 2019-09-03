import sys
import osmx

env  = osmx.Environment(sys.argv[1])
txn = osmx.Transaction(env)
locations = osmx.Locations(txn)
nodes = osmx.Nodes(txn)
ways = osmx.Ways(txn)

way = ways.get(sys.argv[2])

for node_id in way.nodes:
	print(locations.get(node_id))

print(osmx.tag_dict(way.tags))
