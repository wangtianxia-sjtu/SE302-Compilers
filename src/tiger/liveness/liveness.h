#ifndef TIGER_LIVENESS_LIVENESS_H_
#define TIGER_LIVENESS_LIVENESS_H_

#include "tiger/codegen/assem.h"
#include "tiger/frame/frame.h"
#include "tiger/frame/temp.h"
#include "tiger/liveness/flowgraph.h"
#include "tiger/util/graph.h"

namespace LIVE {

class MoveList {
 public:
  G::Node<TEMP::Temp>*src, *dst;
  MoveList* tail;

  MoveList(G::Node<TEMP::Temp>* src, G::Node<TEMP::Temp>* dst, MoveList* tail)
      : src(src), dst(dst), tail(tail) {}

  bool InMoveList(G::Node<TEMP::Temp>* src, G::Node<TEMP::Temp>* dst) const {
    const MoveList* head = this;
    for (; head; head = head->tail) {
      if (head->src == src && head->dst == dst)
        return true;
    }
    return false;
  }
};

class LiveGraph {
 public:
  G::Graph<TEMP::Temp>* graph;
  MoveList* moves;
};

LiveGraph Liveness(G::Graph<AS::Instr>* flowgraph);

bool inMoveList(G::Node<TEMP::Temp>* src, G::Node<TEMP::Temp>* dst, MoveList* list) {
  assert(src && dst);
  for (; list; list = list->tail) {
    if (src == list->src && dst == list->dst) {
      return true;
    }
  }
  return false;
}

MoveList* intersectMoveList(MoveList* left, MoveList* right) {
  if (!left || !right)
    return nullptr;
  MoveList* result = nullptr;
  for (; left; left = left->tail) {
    if (right->InMoveList(left->src, left->dst) && !inMoveList(left->src, left->dst, result)) {
      result = new MoveList(left->src, left->dst, result);
    }
  }
  return result;
}

MoveList* unionMoveList(MoveList* left, MoveList* right) {
  MoveList* result = nullptr;
  for (; left; left = left->tail) {
    if (!inMoveList(left->src, left->dst, result)) {
      result = new MoveList(left->src, left->dst, result);
    }
  }
  for (; right; right = right->tail) {
    if (!inMoveList(right->src, right->dst, result)) {
      result = new MoveList(right->src, right->dst, result);
    }
  }
  return result;
}

MoveList* minusMoveList(MoveList* left, MoveList* right) {
  MoveList* result = nullptr;
  for (; left; left = left->tail) {
    if (!inMoveList(left->src, left->dst, right) && !inMoveList(left->src, left->dst, result)) {
      result = new MoveList(left->src, left->dst, result);
    }
  }
  return result;
}

}  // namespace LIVE

#endif