#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct NalByteRange
{
  size_t offset;
  size_t payloadOffset;
  size_t end;
  bool   complete;
};

// Bound every payload before constructing iterators, including damaged length-prefixed packets.
template <typename Bytes>
std::vector<NalByteRange> splitNalUnits(const Bytes &data, bool annexB, size_t lengthSize = 4)
{
  std::vector<NalByteRange> units;
  if (annexB)
  {
    for (size_t pos = 0; pos + 2 < data.size();)
    {
      size_t prefix = 0;
      if (data[pos] == 0 && data[pos + 1] == 0)
      {
        if (pos + 3 < data.size() && data[pos + 2] == 0 && data[pos + 3] == 1)
          prefix = 4;
        else if (data[pos + 2] == 1)
          prefix = 3;
      }
      if (!prefix)
      {
        ++pos;
        continue;
      }
      if (!units.empty())
      {
        units.back().end      = pos;
        units.back().complete = units.back().payloadOffset < pos;
      }
      units.push_back({pos, pos + prefix, data.size(), pos + prefix < data.size()});
      pos += prefix;
    }
  }
  else if (lengthSize >= 1 && lengthSize <= 4)
  {
    size_t pos = 0;
    while (pos < data.size())
    {
      const auto start = pos;
      if (data.size() - pos < lengthSize)
      {
        units.push_back({start, data.size(), data.size(), false});
        break;
      }
      uint64_t length = 0;
      for (size_t byte = 0; byte < lengthSize; ++byte)
        length = (length << 8) | uint8_t(data[pos++]);
      if (length > data.size() - pos)
      {
        units.push_back({start, pos, data.size(), false});
        break;
      }
      units.push_back({start, pos, pos + size_t(length), length > 0});
      pos += size_t(length);
    }
  }
  return units;
}