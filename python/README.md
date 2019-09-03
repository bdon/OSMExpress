A Python package to read OSM Express (.osmx) database files. 

Right now, only access to nodes, ways and relations by ID is supported.

## Installation

```bash
pip install osmx
```

## Usage

```python
import sys
import osmx

env  = osmx.Environment('your_database.osmx')
txn = osmx.Transaction(env)
locations = osmx.Locations(txn)
nodes = osmx.Nodes(txn)
ways = osmx.Ways(txn)

way = ways.get(123456)

for node_id in way.nodes:
	print(locations.get(node_id))

print(osmx.tag_dict(way.tags))
```
