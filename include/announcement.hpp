#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <utility>

enum class Relationship : uint8_t 
{
  ORIGIN,
  FROM_CUSTOMER,
  FROM_PEER,
  FROM_PROVIDER
};

struct Announcement 
{
  std::string prefix;
  std::vector<uint32_t> as_path;
  uint32_t next_hop_asn;
  Relationship received_from;
  bool rov_invalid = false;

  Announcement() = default;
  
  Announcement(std::string p,
               std::vector<uint32_t> path,
               uint32_t next_hop,
               Relationship rel,
               bool invalid = false)
    : prefix(std::move(p)),
      as_path(std::move(path)),
      next_hop_asn(next_hop),
      received_from(rel),
      rov_invalid(invalid) {}

};

inline Announcement make_origin_announcement(const std::string& prefix,
                                             uint32_t asn)
{
  Announcement a;
  a.prefix        = prefix;
  a.as_path       = {asn};
  a.next_hop_asn  = asn;
  a.received_from = Relationship::ORIGIN;
  a.rov_invalid   = false;
  return a;
}

inline int relationship_rank(Relationship r) noexcept
{
  switch (r)
  {
    case Relationship::ORIGIN:        return 3;
    case Relationship::FROM_CUSTOMER: return 2;
    case Relationship::FROM_PEER:     return 1;
    case Relationship::FROM_PROVIDER: return 0;
  }
  return -1;
}

inline bool better_announcement(const Announcement& a,
                                const Announcement& b)
{
  const int ra = relationship_rank(a.received_from);
  const int rb = relationship_rank(b.received_from);
  if (ra != rb) return ra > rb;

  const std::size_t la = a.as_path.size();
  const std::size_t lb = b.as_path.size();
  if (la != lb) return la < lb;

  return a.next_hop_asn < b.next_hop_asn;
}
