A Python package to read OSM Express (.osmx) database files. 

## Installation

```bash
pip install osmx
```

## Usage

[examples/read_way.py](examples/read_way.py) : Simple program: given a way ID, print the coordinates of its member nodes, its metadata and all the relations it directly belongs to.

[examples/web_server.py](examples/web_server.py) Uses only the Python standard library; starts an HTTP server that takes a url like /way/WAY_ID and returns a GeoJSON feature for that OSM object. Shows example of how to descend into relation members. 

[examples/augmented_diff.py](examples/augmented_diff.py) Creates an [augmented diff](https://wiki.openstreetmap.org/wiki/Overpass_API/Augmented_Diffs) similar to those implemented by Overpass API, but limited to a single OsmChange (.osc) replication sequence file. Requires that the OSMX database represents the replication sequence state directly before that of the .OSC file.
