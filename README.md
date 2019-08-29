# OSM Express

OSM Express is an ultra-fast storage format for OpenStreetMap data that powers [Protomaps](https://protomaps.com) tools.

* **Random access:** Look up nodes, ways and relations by ID; fetch member nodes to construct geometries.
* **Spatial indexing:** Nodes are bucketed into [S2 Geometry](http://s2geometry.io) cells. Access a region by providing a cell covering; works for non-rectangles.
* **Scalable:** OSM Express works the same way for OSM data of any size, from a city-sized extract to the entire planet. Planet-scale operations work efficiently on consumer grade hardware.
* **In-place updates:** Included are scripts to download minutely replication files and apply them to an .osmx database.
* **Concurrent access:** No running server process is required. Writing updates doesn't block reader access. Multiple processes can read concurrently, operating on isolated views of the database.

## Details

OSM Express is a compact 1,500 LOC, and really a cobbling together of a few low-level libraries:

* [Libosmium](https://osmcode.org/libosmium/index.html) for the reading and writing of OSM data.
* [LMDB](https://symas.com/lmdb) for a `mmap`-based ACID key-value store with fast cursor-based iteration.
* [Cap'n Proto](https://capnproto.org) for in-memory and on-disk storage of OSM elements.
* [CRoaring](https://roaringbitmap.org) for in-memory storage of ID sets as compressed bitmaps.
* [S2 Geometry](http://s2geometry.io) for indexing of geographic coordinates.

## Usage

OSM Express is being used in production, but should still be considered experimental with an unstable API. The simplest way to use it is via the `osmx` command line program. It can also be embedded into a C++ project by including headers.

### Command line

```
osmx expand planet.osm.pbf planet.osmx # converts a pbf or xml to osmx. Takes 5-10 hours for the planet, resulting in a ~600GB file.
osmx extract planet.osmx extract.osm.pbf --bbox 40.7411\,-73.9937\,40.7486\,-73.9821 # extract a new pbf for the given bounding box.
osmx update planet.osmx 3648548.osc 3648548 2019-08-29T17:50:02Z --commit # applies an OsmChange diff.
osmx query planet.osmx way 34633854 # look up an element by ID.
```

Detailed command line usage can be found in the [Manual]().

### Headers

Here is an example program that uses the C++ headers to read a way from a osmx file and output its [Well-Known Text](https://en.wikipedia.org/wiki/Well-known_text_representation_of_geometry) LineString geometry.

```
WIP
```

Detailed C++ usage can be found in the [Development Docs]().

### Other languages

TBD

## License and Development

2-Clause BSD, see [LICENSE.md](LICENSE.md). Bug reports, pull requests welcome! For support, new features, and integration, contact [hi@protomaps.com](mailto:hi@protomaps.com).

