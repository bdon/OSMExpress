#include <string>
#include "s2/s2region.h"
#include "s2/s2cell_union.h"
#include "s2/s2region_coverer.h"
#include "nlohmann/json.hpp"

class Region {
public:
	Region(const std::string &text, const std::string &ext);
	bool Contains(S2Point p);
	S2CellUnion GetCovering(S2RegionCoverer &coverer);

private:
	void AddS2RegionFromGeometry(nlohmann::json &geometry);
	std::vector<std::unique_ptr<S2Region>> mRegions;
};