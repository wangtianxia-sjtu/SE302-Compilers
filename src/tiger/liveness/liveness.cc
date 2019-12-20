#include "tiger/liveness/liveness.h"
#include <set>
#include <algorithm>

namespace {

  template <class T>
  std::set<T> union_set(const std::set<T> &s1, const std::set<T> &s2);

  template <class T>
  bool equal_set(const std::set<T> &s1, const std::set<T> &s2);

  template <class T>
  std::set<T> minus_set(const std::set<T> &s1, const std::set<T> &s2);

  template <class T>
  bool in_set(T element, const std::set<T> &s);

}

namespace LIVE {

LiveGraph Liveness(G::Graph<AS::Instr>* flowgraph) {
  return LiveGraph();
}

}  // namespace LIVE

namespace {

  template <class T>
  std::set<T> union_set(const std::set<T> &s1, const std::set<T> &s2) {
    std::set<T> result;
    std::set_union(s1.cbegin(), s1.cend(), s2.cbegin(), s2.cend(), 
                        std::inserter(result, result.begin()));
    return result;
  }

  template <class T>
  bool equal_set(const std::set<T> &s1, const std::set<T> &s2) {
    return s1 == s2;
  }

  template <class T>
  std::set<T> minus_set(const std::set<T> &s1, const std::set<T> &s2) {
    std::set<T> result;
    std::set_difference(s1.cbegin(), s1.cend(), s2.cbegin(), s2.cend(), 
                        std::inserter(result, result.begin()));
    return result;
  }

  template <class T>
  bool in_set(T element, const std::set<T> &s) {
    return (s.find(element) != s.end());
  }
}

