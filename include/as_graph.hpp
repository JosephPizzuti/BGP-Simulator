#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <algorithm>
#include "data_record.hpp"
#include "read_caida.hpp"

struct ASNode {
  std::vector<uint32_t> providers;
  std::vector<uint32_t> customers;
  std::vector<uint32_t> peers;
};

class ASGraph {
  std::vector<ASNode> nodes;

public:
  explicit ASGraph(size_t max_asn = 100000)
    : nodes(max_asn + 1) {}

  inline void add_provider_customer(uint32_t provider, const uint32_t customer) noexcept {
    if (provider >= nodes.size() || customer >= nodes.size()) return;
    nodes[provider].customers.push_back(customer);
    nodes[customer].providers.push_back(provider);
  }

  inline void add_peer(uint32_t a, uint32_t b) noexcept {
    if (a >= nodes.size() || b >= nodes.size()) return;
    nodes[a].peers.push_back(b);
    nodes[b].peers.push_back(a);
  }

  inline const ASNode& get(uint32_t asn) const noexcept {
    return nodes[asn];
  }

  inline size_t size() const noexcept {
    return nodes.size();
  }
};

inline void build_graph(const std::string& filename, ASGraph& graph) {
  read_caida_data(filename, [&](const DataRecord& rec) {
    if (rec.indicator == -1) {
      graph.add_provider_customer(rec.provider_peer, rec.customer_peer);
    }
    else if (rec.indicator == 0) {
      graph.add_peer(rec.provider_peer, rec.customer_peer);
    }
    else {
      throw std::runtime_error("Unexpected indicator value in CAIDA file");
    }
  });
}

enum VisitState : uint8_t {
  UNVISITED = 0,
  ACTIVE    = 1,
  FINISHED  = 2
};

namespace detail {

inline bool dfs_has_cycle(uint32_t u,
                          const ASGraph& graph,
                          std::vector<VisitState>& state)
{
  state[u] = ACTIVE;

  for (uint32_t customer : graph.get(u).customers) {
    if (state[customer] == ACTIVE) {
      return true;
    }
    if (state[customer] == UNVISITED) {
      if (dfs_has_cycle(customer, graph, state)) {
        return true;
      }
    }
  }

  state[u] = FINISHED;
  return false;
}

} // end of namespace detail

inline bool has_provider_cycle(const ASGraph& graph)
{
  std::vector<VisitState> state(graph.size(), UNVISITED);

  for (uint32_t asn = 1; asn < graph.size(); ++asn) {
    if (state[asn] == UNVISITED) {
      if (detail::dfs_has_cycle(asn, graph, state)) {
        return true;
      }
    }
  }
  return false;
}

inline void assert_provider_acyclic(const ASGraph& graph)
{
  if (has_provider_cycle(graph)) {
    throw std::runtime_error("Provider/customer cycle detected in AS graph");
  }
}

inline std::vector<int> compute_propagation_ranks(const ASGraph& graph)
{
  const std::size_t n = graph.size();
  std::vector<int> rank(n, -1);

  std::vector<uint32_t> remaining_children(n, 0);
  for (uint32_t asn = 1; asn < n; ++asn)
  {
    remaining_children[asn] =
      static_cast<uint32_t>(graph.get(asn).customers.size());
  }

  std::queue<uint32_t> q;
  for (uint32_t asn = 1; asn < n; ++asn)
  {
    if (remaining_children[asn] == 0)
    {
      rank[asn] = 0;
      q.push(asn);
    }
  }

  while (!q.empty())
  {
    uint32_t u = q.front();
    q.pop();
    const int r_u = rank[u];

    for (uint32_t provider : graph.get(u).providers)
    {
      if (rank[provider] < r_u + 1)
        rank[provider] = r_u + 1;
      if (remaining_children[provider] > 0)
      {
        --remaining_children[provider];
        if (remaining_children[provider] == 0)
          q.push(provider);
      }
    }
  }

  for (uint32_t asn = 1; asn < n; ++asn)
  {
    if (remaining_children[asn] != 0)
      throw std::runtime_error("compute_propogation_ranks: provider/customer cycle detected");
  }

  return rank;
}

inline std::vector<std::vector<uint32_t>> flatten_graph(const ASGraph& graph)
{
  std::vector<int> rank = compute_propagation_ranks(graph);

  int max_rank = -1;
  const std::size_t n = rank.size();
  for (uint32_t asn = 1; asn < n; ++asn)
  {
    if (rank[asn] >= 0 && rank[asn] > max_rank)
      max_rank = rank[asn];
  }

  if (max_rank < 0) return {};

  std::vector<std::vector<uint32_t>> layers(static_cast<std::size_t>(max_rank) + 1);

  for (uint32_t asn = 1; asn < n; ++asn)
  {
    int r = rank[asn];
    if (r >= 0)
      layers[static_cast<std::size_t>(r)].push_back(asn);
  }

  return layers;
}
