#include <string>
#include "s2/s2region.h"
#include "nlohmann/json.hpp"

class Shape {
public:
	Shape(const std::string &text, const std::string &ext);
	bool Contains(S2Point p);

private:
	void AddS2RegionFromGeometry(nlohmann::json &geometry);
	std::vector<std::unique_ptr<S2Region>> mRegions;
};