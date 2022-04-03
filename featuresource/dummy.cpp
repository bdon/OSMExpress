#include <vector>
#include <iostream>
#include "flatbuffers/flatbuffers.h"
#include "flatgeobuf/feature_generated.h"
#include "flatgeobuf/header_generated.h"

using namespace std;

uint8_t magicbytes[] = { 0x66, 0x67, 0x62, 0x03, 0x66, 0x67, 0x62, 0x00 };

int main(int argc, char* argv[]) {
  istream &in = std::cin;
  uint8_t magic_place[8];
  fread(magic_place,sizeof(magicbytes),1,stdin);

  uint32_t tmp_len;
  fread(reinterpret_cast<char *>(&tmp_len),sizeof(tmp_len),1,stdin);

  vector<char> buf;
  buf.reserve(tmp_len);

  fread(buf.data(),tmp_len,1,stdin);

  flatbuffers::Verifier v((const uint8_t *)buf.data(),tmp_len);
  const auto ok = FlatGeobuf::VerifyHeaderBuffer(v);
  if (!ok) {
    fprintf(stderr, "flatgeobuf header verification failed\n");
    exit(EXIT_FAILURE);
  }
  auto header = FlatGeobuf::GetHeader(buf.data());
  auto node_size = header->index_node_size();

  while (fread(reinterpret_cast<char *>(&tmp_len),sizeof(tmp_len),1,stdin)) {
    auto sFeature = make_shared<vector<uint8_t>>();
    sFeature->reserve(tmp_len);
    fread(sFeature->data(),tmp_len,1,stdin);

    flatbuffers::Verifier v2(sFeature->data(),tmp_len);
    const auto ok2 = FlatGeobuf::VerifyFeatureBuffer(v2);
    if (!ok2) {
      fprintf(stderr, "flatgeobuf feature buffer verification failed\n");
      exit(EXIT_FAILURE);
    }
    auto feature = FlatGeobuf::GetFeature(sFeature->data());
  }
}
