**OSM Express** is a database file format for OpenStreetMap data (.osmx), as well as a command line tool and C++ library for reading and writing .osmx files. Find it on GitHub at [github.com/protomaps/OSMExpress](https://github.com/protomaps/OSMExpress)

![screenshot](https://github.com/protomaps/OSMExpress/blob/main/examples/screenshot.png?raw=true)

*Illustration of the cell covering for a rectangular input region and its overlap with indexed OpenStreetMap geometries.*

## Motivation

Here are some use cases that OSM Express fits well.

* You want an offline copy of OpenStreetMap, which can be updated every day, hour or minute from the main openstreetmap.org database, instead of redownloading the entire planet.
* You want to quickly access all OSM objects in a geographical region, such as as neighborhood, city or small country.
* You want to quickly look up OSM objects by ID, such as getting the `height` and `name` tags for a given way that represents a building, and construct geometries for ways and relations.
* You want to embed a database that does any of the above, such as in a web application that returns OSM objects as GeoJSON.

## Quick Start

### Command Line

Binaries are available for MacOS (Darwin) and GNU/Linux at [GitHub Releases](https://github.com/protomaps/OSMExpress/releases).

For information on how to compile the `osmx` program from source, see the [Programming Guide.](/docs/PROGRAMMING_GUIDE.md)

Once you have the `osmx` command line program, you'll need to start with an .osm.pbf or OSM XML file. The Planet file is available at [planet.openstreetmap.org](https://planet.openstreetmap.org), but it's preferable to begin with something smaller to learn with.

There are numerous sites for downloading .osm.pbf extracts, including [Protomaps Minutely Extracts](https://protomaps.com/downloads/osm), a service itself powered by OSM Express. For testing purposes let's start with this small PBF I generated of New York County:

[new\_york\_county.pbf (5.86 MB, generated 2019/09/02 6:42PM UTC)](https://protomaps-static.s3.us-east-2.amazonaws.com/new_york_county.osm.pbf)

Create an .osmx file by using the `expand` command on the .osm.pbf file:

    osmx expand new_york_county.osm.pbf new_york_county.osmx

This will result in a 91 MB .osmx file.

We can access objects inside this .osmx file by ID, displaying the node IDs of its member nodes and all tags:

    osmx query new_york_county.osmx way 34633854

    > 402743563 402743567 402743571 402743573 2709307502 2709307499 2709307464 402743563
    addr:city=New York City addr:housenumber=350 addr:postcode=10018 ...

We can also extract regions of the .osmx file into a new .osm.pbf file, which is useful for interoperability with other OSM tools.

    osmx extract new_york_county.osmx downtown.osm.pbf --bbox 40.7411\,-73.9937\,40.7486\,-73.9821

### Updating

`utils/osmx-update` is provided to update `.osmx` to the most recent file on a replication server using `osmx update`. For example to update a planet.osmx file with minutely updates:

    python utils/osmx-update planet.osmx https://planet.openstreetmap.org/replication/minute/

## Library

the OSM Express library is intentionally minimal and non-opinionated - for example, no attempt is made to transform OSM tags to a fixed schema, distinguish between polygon and linear ways, or assemble multipolygon relations into polygons. For these typical tasks it's recommended to use OSM Express as a library in your own program. Documentation and example code are available at the [Programming Guide.](/docs/PROGRAMMING_GUIDE.md)

## Other Languages

An .osmx file can be opened and queried direcly in a Python program using the `osmx` Python package. See [Python](/docs/PROGRAMMING_GUIDE.md#python) for details.

Languages other than Python may be supported in the future by either language-specific libraries or a new C API. See [Development](#Development) if you're interested or discuss on GitHub.

## Technical Details

### Storage Requirements

A full planet.osmx created from planet.osm.pbf (47 GB) is around 580 GB.

OSM Express is optimized for fast lookups, extracts and updates, goals opposed to making the database size as compact as possible. A typical .osmx file can be 10 times the size of the corresponding .osm.pbf, because:

* Relationships between parent elements and member elements are encoded in both directions, to enable lookups from node to way, way to relation, etc.
* The storage engine (LMDB) has no built-in compression, unlike some LSM-tree storage engines such as LevelDB.
* The `mmap`-based design of LMDB and Cap'n Proto requires that fields are word-aligned on disk, causing storage overhead.
* Keys and values are stored in full as strings. Keys could be hardcoded in a lookup table, saving about 10% space, but this would make the database less portable.

As of 2019, fast local storage is cheap; 1 terabyte solid state drives are less than 150 USD. On managed hosting providers like AWS and Google Cloud, extra storage is affordable compared to more memory or CPU cores. 

If it's necessary to optimize for storage space, an .osmx file can be stored on a filesystem with transparent compression such as ZFS or Btrfs, at the cost of CPU overhead. This can reduce planet.osmx to around 200GB.

### Privacy

OSM Express stores all metadata - version, timestamp, changeset, username and user ID - for all OSM objects, except for untagged nodes. The `osmx extract` `--noUserData` flag ignores changeset, username and user ID information for extracts, to comply with [GDPR guidelines](https://wiki.openstreetmap.org/wiki/GDPR).

### Performance

OSM Express should work with reasonable amounts of memory, less than 8 gigabytes, even for `expand` and `extract` on planet.osmx. The strongest predictor of performance is I/O latency. If benchmarking different storage environments, I/O latency can be best measured via IOPS at queue depth 1.

*WIP: benchmarks*

## Alternatives

* [osmium-tool](https://osmcode.org/osmium-tool/index.html) for creating extracts from osm.pbf files. This is more efficient for large country or continent sized extracts, or any task where the entire dataset needs to be read.
* [Overpass API](http://overpass-api.de) is a powerful server application for interactive querying and tag-based lookup of OSM data.
* [conveyal/osm-lib](https://github.com/conveyal/osm-lib) is a similar design, written in Java.
* [imposm3](https://github.com/omniscale/imposm3), [osm2pgsql](https://github.com/openstreetmap/osm2pgsql) if you want OSM data in PostgreSQL and/or want to render maps. 

## Concepts

### File Layout

The `osmx query` command with no arguments reveals the layout of an .osmx database:

    osmx query planet.osmx
    locations: 5313351219
    nodes: 144307630
    ways: 590470034
    relations: 6895065
    cell_node: 5313351219
    node_way: 5906888644
    node_relation: 10242142
    way_relation: 63350432
    relation_relation: 497137

an .osmx file is a LMDB database with 10 sub-databases. All keys are 64 bit integers.

* `locations`: maps a node ID to a 64-bit location, with 32 bits for each of lat, lon.
* `nodes`, `ways`, `relations` map object IDs to a Cap'n Proto message as described in `include/osmx/messages.capnp`
* `cell_node` maps a level 16 S2 cell to a node ID, using LMDB DUPSORT (sorted duplicate keys).
* `node_way`, `node_relation`, `way_relation` and `relation_relation` map object IDs to its parent objects, also using DUPSORT.

Finally, the `metadata` sub-database holds arbitrary string:string values. This is used to store the replication sequence number and timestamp. 

It is important to note that LMDB transactions span all sub-databases. This means that a read operation will retrieve the correct `timestamp` for the data it fetches, even if the database is written to while the read is happening.

### Spatial Indexing

OSM Express avoids expensive point-in-polygon computations for spatial operations. Instead, a query region is approximated by S2 cells with maximum level 16. The level 16 is chosen as a reasonable tradeoff between covering precision and storage space.

*Author's note: the S2 Covering of a region may differ depending on choice of architecture and compiler, while still being valid. Let me know if you know how to make this consistent.*

## Further Development

If you'd like to sponsor development of OSM Express features, or integrate it into your product, get in contact at [brandon@protomaps.com](mailto:brandon@protomaps.com).

## Presentations

[State of the Map US 2019, Minneapolis - Video](https://2019.stateofthemap.us/program/sun/osm-express-a-spatial-file-format-for-the-planet.html)