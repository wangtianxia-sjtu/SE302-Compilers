#include "tiger/regalloc/regalloc.h"
#include "tiger/liveness/flowgraph.h"
#include "tiger/liveness/liveness.h"
#include <vector>
#include <set>
#include <map>

namespace RA {
  enum RegisterColor { RAX, RBP, RBX, RDI, RSI, RDX, RCX, R8, R9, R10, R11, R12, R13, R14, R15 };
}  //namespace RA

namespace {
  const int K = 15;
  LIVE::LiveGraph liveGraph;
  std::vector<AS::Instr *> instrVector;

  std::map<G::Graph<TEMP::Temp>*, int> node2degree;
  std::map<G::Graph<TEMP::Temp>*, int> node2color;
  std::map<G::Graph<TEMP::Temp>*, G::Graph<TEMP::Temp>*> node2alias;
  std::map<G::Graph<TEMP::Temp>*, LIVE::MoveList*> node2moveList;

  std::vector<AS::Instr *> toVector(AS::InstrList* iList);
  AS::InstrList* toList(const std::vector<AS::Instr *>& iVector);

  void Build();
}

namespace RA {

Result RegAlloc(F::Frame* f, AS::InstrList* il) {
  bool done = false;
  instrVector = toVector(il);
  while (!done) {
    G::Graph<AS::Instr>* flowGraph = FG::AssemFlowGraph(toList(instrVector), f);
    liveGraph = LIVE::Liveness(flowGraph);
    // TODO
  }
}

}  // namespace RA

namespace {

  std::vector<AS::Instr *> toVector(AS::InstrList* iList) {
    std::vector<AS::Instr *> result;
    while (iList) {
      result.push_back(iList->head);
      iList = iList->tail;
    }
    return result;
  }

  AS::InstrList* toList(const std::vector<AS::Instr *>& iVector) {
    AS::InstrList* result = nullptr;
    for (std::vector<AS::Instr *>::const_reverse_iterator cri = iVector.crbegin(); cri != iVector.crend(); ++cri) {
      result = new AS::InstrList(*cri, result);
    }
    return result;
  }

  void Build() {
    // TODO
  }
}
