#include <string>
#include "s2/s2region.h"

class Shape {
public:
	Shape(const std::string &text, const std::string &ext);
	bool Contains(S2Point p);

private:
	std::vector<std::unique_ptr<S2Region>> mRegions;
};