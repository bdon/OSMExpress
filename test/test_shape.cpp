#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"
#include "shape.h"
#include "s2/s2latlng.h"

using namespace std;

// osmium header format is like this: Box: (-79.82402,40.439216,-71.660801,45.07133)

// small:  {\"bbox\":[40.7411\,-73.9937\,40.7486\,-73.9821]}
// big:    {\"bbox\":[40.6762\,-74.0543\,40.8093\,-73.8603]}
// radius: {"center":[40.7411,-73.9937],"radius":25.5}
// indo:  {\"bbox\":[-12.039321\,94.394531\,8.407168\,142.418292]}

// bbox should be minLat,minLon,maxLat,maxLon (opposite of GeoJSON)
TEST_CASE("rectangular bbox") {
  SECTION("basic bbox") {
    string bbox = "-1.0,-1.0,1.0,1.0";
    Shape s{bbox,"bbox"};
    REQUIRE(s.Contains(S2LatLng::FromDegrees(0,0).ToPoint()));
    REQUIRE(s.Contains(S2LatLng::FromDegrees(0.9,0.9).ToPoint()));
  }
}

TEST_CASE("disc") {
  SECTION("basic disc") {
    string disc = "0.0,0.0,1.0";
    Shape s{disc,"disc"};
    REQUIRE(s.Contains(S2LatLng::FromDegrees(0,0).ToPoint()));
    REQUIRE(!s.Contains(S2LatLng::FromDegrees(0.9,0.9).ToPoint()));
  }
}

TEST_CASE("geojson polygon") {
    SECTION("polygon geometry") {
        string json = R"json({
  "type": "Polygon",
  "coordinates": [
    [
      [-1.0,-1.0],
      [-1.0,1.0],
      [1.0,1.0],
      [1.0,-1.0],
      [-1.0,-1.0]
    ]
  ]
})json";
        Shape s{json,"json"};
        REQUIRE(s.Contains(S2LatLng::FromDegrees(0,0).ToPoint()));
        REQUIRE(!s.Contains(S2LatLng::FromDegrees(2.0,2.0).ToPoint()));
    }

    SECTION("polygon with a hole") {
        string json = R"json({
  "type": "Polygon",
  "coordinates": [
    [
      [-2.0,-2.0],
      [-2.0,2.0],
      [2.0,2.0],
      [2.0,-2.0],
      [-2.0,-2.0]
    ],
    [
      [-1.0,-1.0],
      [-1.0,1.0],
      [1.0,1.0],
      [1.0,-1.0],
      [-1.0,-1.0]
    ]
  ]
})json";
        Shape s{json,"json"};
        REQUIRE(s.Contains(S2LatLng::FromDegrees(1.5,1.5).ToPoint()));
        REQUIRE(!s.Contains(S2LatLng::FromDegrees(0.0,0.0).ToPoint()));
    }

    SECTION("multipolygon geometry") {
        string json = R"json({
  "type": "MultiPolygon",
  "coordinates": [
    [[
      [0.0,0.0],
      [1.0,0.0],
      [1.0,1.0],
      [0.0,1.0],
      [0.0,0.0]
    ]],
    [[
      [2.0,2.0],
      [3.0,2.0],
      [3.0,3.0],
      [2.0,3.0],
      [2.0,2.0]
    ]]
  ]
})json";
        Shape s{json,"json"};
        REQUIRE(s.Contains(S2LatLng::FromDegrees(0.5,0.5).ToPoint()));
        REQUIRE(s.Contains(S2LatLng::FromDegrees(2.5,2.5).ToPoint()));
    }
}

// test handle whitespace
TEST_CASE("osmosis .poly") {
    SECTION("simple polygon") {
        string poly = R"poly(basic
first_area
    0.1e+01 0.1e+01
    0.1e+01 -0.1e+01
    -0.1e+01    -0.1e+01
    -0.1e+01    0.1e+01
END
END
)poly";
        Shape s{poly,"poly"};
        REQUIRE(s.Contains(S2LatLng::FromDegrees(0,0).ToPoint()));
        REQUIRE(!s.Contains(S2LatLng::FromDegrees(2.0,2.0).ToPoint()));
    }

    SECTION("different whitespace, opposite orientation") {
        string poly = R"poly(basic
first_area
    0.1E+01 0.1E+01
    -0.1E+01    0.1E+01
    -0.1E+01    -0.1E+01
    0.1E+01 -0.1E+01
END
END
)poly";
        Shape s{poly,"poly"};
        REQUIRE(s.Contains(S2LatLng::FromDegrees(0,0).ToPoint()));
        REQUIRE(!s.Contains(S2LatLng::FromDegrees(2.0,2.0).ToPoint()));
    }

    SECTION("repeated last point") {
        string poly = R"poly(basic
first_area
    0.1e+01 0.1e+01
    0.1e+01 -0.1e+01
    -0.1e+01    -0.1e+01
    -0.1e+01    0.1e+01
    0.1e+01 0.1e+01
END
END
)poly";
        Shape s{poly,"poly"};
        REQUIRE(s.Contains(S2LatLng::FromDegrees(0,0).ToPoint()));
        REQUIRE(!s.Contains(S2LatLng::FromDegrees(2.0,2.0).ToPoint()));

    }

    SECTION("multiple outer loops") {

    }

    SECTION("loop with hole") {

    }
}