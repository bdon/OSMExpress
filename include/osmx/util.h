#pragma once
#include <chrono>
#include <iostream>
#include "osmium/tags/taglist.hpp"

#define CHECK(x) if (0 != x) { printf("%s, file %s, line %d.\n", mdb_strerror(x), __FILE__, __LINE__); abort(); }

// a higher cell level results in more precise extracts, as the size of 1 cell is the minimum index resolution.
#define CELL_INDEX_LEVEL 16

class Timer {
  public:
  Timer(std::string name) : mName(name) {
    mStartTime = std::chrono::high_resolution_clock::now();
    std::cout << "Start " << mName << std::endl;
  }

  ~Timer() {
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::high_resolution_clock::now() - mStartTime ).count();
    std::cout << "Finished " << mName << " in " << duration/1000.0 << " seconds." << std::endl;
  }

  private:
  std::chrono::high_resolution_clock::time_point mStartTime;
  std::string mName;
};

template <typename T>
void setTags(const osmium::TagList &tags, T &builder) {
  builder.initTags(tags.size() * 2);
  auto tagBuilder = builder.getTags();

  int i = 0;
  for (auto const &tag : tags) {
    tagBuilder.set(i,tag.key());
    i++;
    tagBuilder.set(i,tag.value());
    i++;
  }
}
