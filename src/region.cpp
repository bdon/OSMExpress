#include <sstream>
#include <iostream>
#include "s2/s2latlng.h"
#include "s2/s2latlng_rect.h"
#include "s2/s2cap.h"
#include "s2/s2polygon.h"
#include "s2/s2loop.h"
#include "region.h"

static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}


std::unique_ptr<S2Polygon> S2PolyFromCoordinates(nlohmann::json &coordinates) {
    std::vector<std::unique_ptr<S2Loop>> loopRegions;
    for (auto loop : coordinates) {
        std::vector<S2Point> points;
        for (auto coord : loop) {
            double lon = coord[0].get<double>();
            double lat = coord[1].get<double>();
            points.push_back(S2LatLng::FromDegrees(lat,lon).ToPoint());
        }
        auto loopRegion = std::make_unique<S2Loop>(points);
        loopRegion->Normalize();
        loopRegions.push_back(std::move(loopRegion));
    };
    return std::make_unique<S2Polygon>(std::move(loopRegions));
}

void Region::AddS2RegionFromGeometry(nlohmann::json &geometry) {
    if (geometry["type"] == "Polygon") {
        auto p = S2PolyFromCoordinates(geometry["coordinates"]);
        mRegions.push_back(move(p));
    } else if (geometry["type"] == "MultiPolygon") {
        for (auto polygon : geometry["coordinates"]) {
            auto p = S2PolyFromCoordinates(polygon);
            mRegions.push_back(move(p));
        }
    }
}

Region::Region(const std::string &text, const std::string &ext) {
    if (ext == "bbox") {
        double minLat,minLon,maxLat,maxLon;
        std::sscanf(text.c_str(), "%lf,%lf,%lf,%lf",&minLat,&minLon,&maxLat,&maxLon);
        auto lo = S2LatLng::FromDegrees(minLat,minLon);
        auto hi = S2LatLng::FromDegrees(maxLat,maxLon);
        mRegions.push_back(std::make_unique<S2LatLngRect>(lo,hi));
    } else if (ext == "disc") {
        double lat,lon,radius;
        std::sscanf(text.c_str(), "%lf,%lf,%lf",&lat,&lon,&radius);
        auto center = S2LatLng::FromDegrees(lat,lon);
        auto angle = S1Angle::Degrees(radius);
        mRegions.push_back(std::make_unique<S2Cap>(center.ToPoint(),angle));
    } else if (ext == "poly") {
        std::istringstream f(text);
        std::string line;

        // discard the first line
        std::getline(f,line);
        // start the first polygon
        std::getline(f,line);
        std::vector<S2Point> points;

        while (std::getline(f, line)) {
            rtrim(line);
            double lat, lon;
            if (line == "END") {

            } else {
                std::istringstream iss(line);
                iss >> lat;
                iss >> lon;
                points.push_back(S2LatLng::FromDegrees(lat,lon).ToPoint());
            }
        }

        auto loop = std::make_unique<S2Loop>(points);
        loop->Normalize();
        mRegions.push_back(std::move(loop));
    } else if (ext == "geojson") {
        auto json = nlohmann::json::parse(text);
        if (json["type"] == "Polygon" || json["type"] == "MultiPolygon") {
            AddS2RegionFromGeometry(json);
        } else if (json["type"] == "GeometryCollection") {
            for (auto geometry : json) {
                AddS2RegionFromGeometry(json);
            }
        } else if (json["type"] == "Feature") {
            AddS2RegionFromGeometry(json["geometry"]);
        } else if (json["type"] == "FeatureCollection") {
            for (auto feature : json["features"]) {
                AddS2RegionFromGeometry(feature["geometry"]);
            }
        }
    }
}

bool Region::Contains(S2Point p) {
    for (auto const &region : mRegions) {
        if (region->Contains(p)) return true;
    }
    return false;
}

S2CellUnion Region::GetCovering(S2RegionCoverer &coverer) {
    S2CellUnion retval;
    for (auto const &region : mRegions) {
        retval = retval.Union(coverer.GetCovering(*region));
    }
    return retval;
}