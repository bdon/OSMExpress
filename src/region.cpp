#include <sstream>
#include <iostream>
#include "s2/s2latlng.h"
#include "s2/s2latlng_rect.h"
#include "s2/s2cap.h"
#include "s2/s2polygon.h"
#include "s2/s2loop.h"
#include "osmx/region.h"

static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

std::unique_ptr<S2Polygon> S2PolyFromCoordinates(nlohmann::json &coordinates) {
    std::vector<std::unique_ptr<S2Loop>> loopRegions;
    for (auto loop : coordinates) {
        std::vector<S2Point> points;

        // ignore the last repeated point
        for (int i = 0; i < loop.size() - 1; i++) {
            double lon = loop[i][0].get<double>();
            double lat = loop[i][1].get<double>();
            points.push_back(S2LatLng::FromDegrees(lat,lon).Normalized().ToPoint());
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
        auto lo = S2LatLng::FromDegrees(minLat,minLon).Normalized();
        auto hi = S2LatLng::FromDegrees(maxLat,maxLon).Normalized();
        mRegions.push_back(std::make_unique<S2LatLngRect>(lo,hi));
    } else if (ext == "disc") {
        double lat,lon,radius;
        std::sscanf(text.c_str(), "%lf,%lf,%lf",&lat,&lon,&radius);
        auto center = S2LatLng::FromDegrees(lat,lon).Normalized();
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
                points.push_back(S2LatLng::FromDegrees(lat,lon).Normalized().ToPoint());
            }

        }

        if (points[0] == points[points.size() - 1]) points.pop_back();

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
    } else {
        std::cerr << "Unknown ext" << std::endl;
        assert(false);
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

S2LatLngRect Region::GetBounds() {
    auto const &firstRegion = mRegions[0];
    auto lat_min = firstRegion->GetRectBound().lat_lo();
    auto lat_max = firstRegion->GetRectBound().lat_hi();
    auto lng_min = firstRegion->GetRectBound().lng_lo();
    auto lng_max = firstRegion->GetRectBound().lng_hi();

    for (size_t i = 1; i < mRegions.size(); i++) {
        auto const &r = mRegions[i];
        auto lat_lo = r->GetRectBound().lat_lo();
        auto lat_hi = r->GetRectBound().lat_hi();
        auto lng_lo = r->GetRectBound().lng_lo();
        auto lng_hi = r->GetRectBound().lng_hi();
        if (lat_lo < lat_min) lat_min = lat_lo;
        if (lat_hi > lat_max) lat_max = lat_hi;
        if (lng_lo < lng_min) lng_min = lng_lo;
        if (lng_hi > lng_max) lng_max = lng_hi;
    }

    return S2LatLngRect(S2LatLng(lat_min,lng_min),S2LatLng(lat_max,lng_max));
}