## Building from source

OSM Express uses CMake for its build scripts. It's only been tested with the Clang C++ compiler so far.

Most dependencies are included as Git submodules in the `vendor/` directory, but a few stable, common libraries are expected to exist on your system, including bzip2, zlib, Expat and OpenSSL. 

### FreeBSD 12

`sudo pkg install cmake expat`

### macOS 

via Homebrew: `brew install cmake bzip2 zlib openssl expat`

*Additional macOS notes: the Clang compiler should be available via XCode Command Line Tools.*
### Ubuntu 18.04

via Apt package manager: `sudo apt install cmake clang libbz2-dev libz-dev libexpat-dev libssl-dev`

### Build Instructions

    git clone --recursive https://github.com/protomaps/OSMExpress.git
    cd OSMExpress
    cmake -DCMAKE_BUILD_TYPE=Release .
    make

*macOS note: If OpenSSL is installed through Homebrew, you may need to add an option to your cmake command: `-DOPENSSL_ROOT_DIR=/usr/local/opt/openssl\@3`
For macOS systems with Apple Silicon, this path is `-DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl\@3`

## Using the C++ Headers

### Example: Way ID to WKT

See [examples/way_wkt.cpp](https://github.com/protomaps/OSMExpress/blob/master/examples/way_wkt.cpp) for a commented program.

### Example: Bbox to Way WKTs

See [examples/way_wkt.cpp](https://github.com/protomaps/OSMExpress/blob/master/examples/way_wkt.cpp) for a commented program.

## Python

Install the library with `pip install osmx` . This will also download and install the `pycapnp` and `lmdb` Python libraries.

The Python API supports only location, node, way and relation lookups at the moment. Example:

    import osmx

    env  = osmx.Environment('planet.osmx')
    txn = osmx.Transaction(env)
    locations = osmx.Locations(txn)
    nodes = osmx.Nodes(txn)
    ways = osmx.Ways(txn)

    way = ways.get(123456)

    for node_id in way.nodes:
        print(locations.get(node_id))

    print(osmx.tag_dict(way.tags))
