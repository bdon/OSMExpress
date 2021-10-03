#include <string>
#include "s2/s2region.h"
#include "s2/s2cell_union.h"
#include "s2/s2region_coverer.h"
#include "s2/s2latlng_rect.h"
#include "nlohmann/json.hpp"

class Region {
public:
	Region(const std::string &text, const std::string &ext);
	bool Contains(S2Point p);
	S2CellUnion GetCovering(S2RegionCoverer &coverer);
	S2LatLngRect GetBounds();

private:
	void AddS2RegionFromGeometry(nlohmann::json &geometry);
	void AddS2RegionFromPolyFile(std::istringstream &file);
	std::vector<std::unique_ptr<S2Region>> mRegions;
};