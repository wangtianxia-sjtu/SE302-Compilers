#include "tiger/liveness/liveness.h"
#include <map>

namespace {

  bool inTempList(TEMP::TempList* l, TEMP::Temp* t);
  bool equalTempList(TEMP::TempList* l1, TEMP::TempList* l2);
  TEMP::TempList* unionTempList(TEMP::TempList* l1, TEMP::TempList* l2);
  TEMP::TempList* minusTempList(TEMP::TempList* l1, TEMP::TempList* l2);

}

namespace LIVE {

LiveGraph Liveness(G::Graph<AS::Instr>* flowgraph) {

  LiveGraph result;

  std::map<G::Node<AS::Instr>*, TEMP::TempList*> node2in;
  std::map<G::Node<AS::Instr>*, TEMP::TempList*> node2out;

  // Record all the nodes in map
  for (G::NodeList<AS::Instr>* head = flowgraph->Nodes(); head; head = head->tail) {
    G::Node<AS::Instr>* node = head->head;
    assert(node);
    node2in[node] = nullptr;
    node2out[node] = nullptr;
  }

  // Compute liveness by iteration P221 10.4
  bool ok = false;
  while (!ok) {
    ok = true;
    for (G::NodeList<AS::Instr>* head = flowgraph->Nodes(); head; head = head->tail) {
      G::Node<AS::Instr>* node = head->head;
      assert(node);
      TEMP::TempList* in_old = node2in[node];
      TEMP::TempList* out_old = node2out[node];
      node2in[node] = unionTempList(node->NodeInfo()->GetUse(), 
                                    minusTempList(node2out[node], node->NodeInfo()->GetDef()));
      TEMP::TempList* union_succ_in = nullptr;
      for (G::NodeList<AS::Instr>* succ = node->Succ(); succ; succ = succ->tail) {
        assert(succ->head);
        TEMP::TempList* in_succ = node2in[succ->head];
        union_succ_in = unionTempList(union_succ_in, in_succ);
      }
      node2out[node] = union_succ_in;
      if (!equalTempList(node2in[node], in_old) || !equalTempList(node2out[node], out_old))
        ok = false;
    }
  }

  // Construct interference graph
  result.graph = new G::Graph<TEMP::Temp>();
  result.moves = nullptr;
  std::map<TEMP::Temp*, G::Node<TEMP::Temp>*> temp2node;

  // Add machine registers into the interference graph
  for (TEMP::TempList* head = F::allocatableRegisters(); head; head = head->tail) {
    assert(head->head);
    G::Node<TEMP::Temp>* node = result.graph->NewNode(head->head);
    assert(node);
    temp2node[head->head] = node;
  }

  // All the machine registers interfere with each other
  for (TEMP::TempList* r1 = F::allocatableRegisters(); r1; r1 = r1->tail) {
    for (TEMP::TempList* r2 = r1->tail; r2; r2 = r2->tail) {
      G::Node<TEMP::Temp>* node1 = temp2node[r1->head];
      G::Node<TEMP::Temp>* node2 = temp2node[r2->head];
      result.graph->AddEdge(node1, node2);
      result.graph->AddEdge(node2, node1);
    }
  }

  // Add temporary registers into the graph
  for (G::NodeList<AS::Instr>* head = flowgraph->Nodes(); head; head = head->tail) {
    G::Node<AS::Instr>* node = head->head;
    TEMP::TempList* allTemps = unionTempList(node2in[node], 
                               unionTempList(node2out[node],
                               unionTempList(node->NodeInfo()->GetDef(),
                               node->NodeInfo()->GetUse())));
    for (TEMP::TempList* head = allTemps; head; head = head->tail) {
      assert(head->head);
      if (head->head == F::SP()) // %rsp is not allocatable
        continue;
      if (temp2node.find(head->head) == temp2node.end()) {
        G::Node<TEMP::Temp>* node = result.graph->NewNode(head->head);
        temp2node[head->head] = node;
      }
    }
  }

  // Add edges between temporary registers P229
  for (G::NodeList<AS::Instr>* head = flowgraph->Nodes(); head; head = head->tail) {
    G::Node<AS::Instr>* node = head->head;
    assert(node);
    TEMP::TempList* def = node->NodeInfo()->GetDef();
    if (!def)
      continue;

    if (!FG::IsMove(node)) {

      /*
       * P229 Rule 1
       * Non-move instructions => Add edges between def and out
       */

      for (TEMP::TempList* defHead = def; defHead; defHead = defHead->tail) {
        assert(defHead->head);
        TEMP::Temp* defTemp = defHead->head;
        if (defTemp == F::SP())
          continue;
        G::Node<TEMP::Temp>* defNode = temp2node[defTemp];
        assert(defNode);
        TEMP::TempList* outTemps = node2out[node];
        for (TEMP::TempList* outTempsHead = outTemps; outTempsHead; outTempsHead = outTempsHead->tail) {
          assert(outTempsHead->head);
          TEMP::Temp* outTemp = outTempsHead->head;
          if (outTemp == F::SP())
            continue;
          G::Node<TEMP::Temp>* outNode = temp2node[outTemp];
          if (!outNode->Adj() || !outNode->Adj()->InNodeList(defNode)) {
            result.graph->AddEdge(defNode, outNode);
            result.graph->AddEdge(outNode, defNode);
          }
        }
      }
    }
    else {

      /*
       * P229 Rule 2
       * Move instructions => Add edges between def and (out - use)
       */

      for (TEMP::TempList* defHead = def; defHead; defHead = defHead->tail) {
        assert(defHead->head);
        TEMP::Temp* defTemp = defHead->head;
        if (defTemp == F::SP())
          continue;
        G::Node<TEMP::Temp>* defNode = temp2node[defTemp];
        assert(defNode);
        TEMP::TempList* outTemps = node2out[node];
        for (TEMP::TempList* outTempsHead = outTemps; outTempsHead; outTempsHead = outTempsHead->tail) {
          assert(outTempsHead->head);
          TEMP::Temp* outTemp = outTempsHead->head;
          if (outTemp == F::SP())
            continue;
          G::Node<TEMP::Temp>* outNode = temp2node[outTemp];
          assert(outNode);
          if (!(outNode->Adj() && outNode->Adj()->InNodeList(defNode)) && !inTempList(node->NodeInfo()->GetUse(), outTemp)) {
            result.graph->AddEdge(defNode, outNode);
            result.graph->AddEdge(outNode, defNode);
          }
        }

        // Add to movelist
        for (TEMP::TempList* uses = node->NodeInfo()->GetUse(); uses; uses = uses->tail) {
          assert(uses->head);
          if (uses->head == F::SP())
            continue;
          G::Node<TEMP::Temp>* useNode = temp2node[uses->head];
          assert(useNode);
          if (!result.moves || !result.moves->InMoveList(useNode, defNode)) {
            result.moves = new MoveList(useNode, defNode, result.moves);
          }
        }
      }
    }
  }
  return result;
}

}  // namespace LIVE

namespace {

  bool inTempList(TEMP::TempList* l, TEMP::Temp* t) {
    assert(t);
    for (TEMP::TempList* head = l; head; head = head->tail) {
      if (head->head == t)
        return true;
    }
    return false;
  }

  bool equalTempList(TEMP::TempList* l1, TEMP::TempList* l2) {
    for (TEMP::TempList* head1 = l1; head1; head1 = head1->tail) {
      assert(head1->head);
      if (!inTempList(l2, head1->head))
        return false;
    }
    for (TEMP::TempList* head2 = l2; head2; head2 = head2->tail) {
      assert(head2->head);
      if (!inTempList(l1, head2->head))
        return false;
    }
    return true;
  }

  TEMP::TempList* unionTempList(TEMP::TempList* l1, TEMP::TempList* l2) {
    TEMP::TempList* result = l1;
    for (TEMP::TempList* head2 = l2; head2; head2 = head2->tail) {
      assert(head2->head);
      if (!inTempList(result, head2->head)) {
        result = new TEMP::TempList(head2->head, result);
      }
    }
    return result;
  }

  TEMP::TempList* minusTempList(TEMP::TempList* l1, TEMP::TempList* l2) {
    TEMP::TempList* result = nullptr;
    for (TEMP::TempList* head1 = l1; head1; head1 = head1->tail) {
      assert(head1->head);
      if (!inTempList(l2, head1->head))
        result = new TEMP::TempList(head1->head, result);
    }
    return result;
  }

}

