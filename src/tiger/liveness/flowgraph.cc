#include "tiger/liveness/flowgraph.h"
#include <vector>
#include <map>
#include <iostream>

namespace {
  std::vector<AS::Instr *> toVector(AS::InstrList* iList);
}

namespace FG {

TEMP::TempList* Def(G::Node<AS::Instr>* n) {
  return n->NodeInfo()->GetDef();
}

TEMP::TempList* Use(G::Node<AS::Instr>* n) {
  return n->NodeInfo()->GetUse();
}

bool IsMove(G::Node<AS::Instr>* n) {
  return n->NodeInfo()->kind == AS::Instr::Kind::MOVE;
}

G::Graph<AS::Instr>* AssemFlowGraph(AS::InstrList* il, F::Frame* f) {
  G::Graph<AS::Instr>* graph = new G::Graph<AS::Instr>();
  std::vector<AS::Instr *> instrList = toVector(il);
  std::vector<G::Node<AS::Instr>*> nodes;
  std::map<TEMP::Label*, G::Node<AS::Instr>*> label2node;

  std::size_t s = instrList.size();
  if (s == 0)
    return graph;
  
  // Create nodes and handle labels
  for (std::size_t i = 0; i < s; ++i) {
    G::Node<AS::Instr>* curNode = graph->NewNode(instrList[i]);
    nodes.push_back(curNode);
    if (instrList[i]->kind == AS::Instr::Kind::LABEL) {
      TEMP::Label* l = static_cast<AS::LabelInstr*>(instrList[i])->label;
      label2node[l] = curNode;
    }
  }

  // Add edges between adjacent instructions
  for (std::size_t i = 0; i < s-1; ++i) {
    std::string assem = instrList[i]->GetAssem();
    if (assem[0] == 'j' && assem[1] == 'm' && assem[2] == 'p')
      continue;
    graph->AddEdge(nodes[i], nodes[i+1]);
  }

  // Add edges for jump instructions
  for (std::size_t i = 0; i < s; ++i) {
    if (instrList[i]->kind == AS::Instr::Kind::OPER) {
      AS::OperInstr* operInstr = static_cast<AS::OperInstr*>(instrList[i]);
      if (operInstr->jumps) {
        for (TEMP::LabelList* head = operInstr->jumps->labels; head; head = head->tail) {
          if (label2node.find(head->head) == label2node.end()) {
            std::cerr << "Unknown label in AssemFlowGraph: " << head->head->Name() << std::endl;
            assert(0);
          }
          else {
            graph->AddEdge(nodes[i], label2node[head->head]);
          }
        }
      }
    }
  }
  return graph;
}

}  // namespace FG

namespace {
  std::vector<AS::Instr *> toVector(AS::InstrList* iList) {
    std::vector<AS::Instr *> result;
    for (AS::InstrList* head = iList; head; head = head->tail) {
      assert(head->head);
      result.push_back(head->head);
    }
    return result;
  }
}
