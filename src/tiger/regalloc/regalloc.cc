#include "tiger/regalloc/regalloc.h"
#include "tiger/liveness/flowgraph.h"
#include "tiger/liveness/liveness.h"
#include <vector>
#include <set>
#include <map>

namespace {
  LIVE::LiveGraph liveGraph;
  std::vector<AS::Instr *> instrVector;

  std::map<G::Node<TEMP::Temp>*, int> node2degree;
  std::map<G::Node<TEMP::Temp>*, int> node2color;
  std::map<G::Node<TEMP::Temp>*, G::Node<TEMP::Temp>*> node2alias;
  std::map<G::Node<TEMP::Temp>*, LIVE::MoveList*> node2moveList;

  LIVE::MoveList* coalescedMoves = nullptr;
  LIVE::MoveList* constrainedMoves = nullptr;
  LIVE::MoveList* frozenMoves = nullptr;
  LIVE::MoveList* worklistMoves = nullptr;
  LIVE::MoveList* activeMoves = nullptr;

  G::NodeList<TEMP::Temp>* precolored = nullptr;
  G::NodeList<TEMP::Temp>* simplifyWorklist = nullptr;
  G::NodeList<TEMP::Temp>* freezeWorklist = nullptr;
  G::NodeList<TEMP::Temp>* spillWorklist = nullptr;
  G::NodeList<TEMP::Temp>* spilledNodes = nullptr;
  G::NodeList<TEMP::Temp>* coalescedNodes = nullptr;
  G::NodeList<TEMP::Temp>* coloredNodes = nullptr;
  G::NodeList<TEMP::Temp>* selectStack = nullptr;

  std::vector<AS::Instr *> toVector(AS::InstrList* iList);
  AS::InstrList* toList(const std::vector<AS::Instr *>& iVector);

  void Build();
  void MakeWorklist();
  void Simplify();
  void Coalesce();
  void Freeze();
  void SelectSpill();
  void AssignColors();
  bool MoveRelated(G::Node<TEMP::Temp>* n);
  void RewriteProgram(F::Frame* f);
  G::NodeList<TEMP::Temp>* Adjacent(G::Node<TEMP::Temp>* n);
  void DecrementDegree(G::Node<TEMP::Temp>* n);
  void FreezeMoves(G::Node<TEMP::Temp>* u);
  void AddWorkList(G::Node<TEMP::Temp>* n);
  bool OK(G::Node<TEMP::Temp>* t, G::Node<TEMP::Temp>* r);
  bool Conservative(G::NodeList<TEMP::Temp>* nodes);
  G::Node<TEMP::Temp>* GetAlias(G::Node<TEMP::Temp>* n);
  void Combine(G::Node<TEMP::Temp>* u, G::Node<TEMP::Temp>* v);
  void EnableMoves(G::NodeList<TEMP::Temp>* nodes);
  TEMP::Map* AssignRegisters();
  void AddEdge(G::Node<TEMP::Temp>* u, G::Node<TEMP::Temp>* v);
  LIVE::MoveList* NodeMoves(G::Node<TEMP::Temp>* n);
}

namespace RA {

Result RegAlloc(F::Frame* f, AS::InstrList* il) {
  Result r;
  bool done = false;
  instrVector = toVector(il);
  while (!done) {
    G::Graph<AS::Instr>* flowGraph = FG::AssemFlowGraph(toList(instrVector), f);
    liveGraph = LIVE::Liveness(flowGraph);
    
    Build();

    MakeWorklist();

    while (simplifyWorklist || worklistMoves || freezeWorklist || spillWorklist) {
      if (simplifyWorklist) {
        Simplify();
      }
      else if (worklistMoves) {
        Coalesce();
      }
      else if (freezeWorklist) {
        Freeze();
      }
      else if (spillWorklist) {
        SelectSpill();
      }
    }

    AssignColors();

    if (spilledNodes) {
      RewriteProgram(f);
      done = false;
    }
  }

  r.coloring = AssignRegisters();
  r.il = toList(instrVector);
  return r;
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
    node2degree.clear();
    node2color.clear();
    node2alias.clear();
    node2moveList.clear();

    coalescedMoves = nullptr;
    constrainedMoves = nullptr;
    frozenMoves = nullptr;
    worklistMoves = liveGraph.moves;
    activeMoves = nullptr;

    precolored = nullptr;
    simplifyWorklist = nullptr;
    freezeWorklist = nullptr;
    spillWorklist = nullptr;
    spilledNodes = nullptr;
    coalescedNodes = nullptr;
    coloredNodes = nullptr;
    selectStack = nullptr;

    for (G::NodeList<TEMP::Temp>* head = liveGraph.graph->Nodes(); head; head = head->tail) {
      G::Node<TEMP::Temp>* curNode = head->head;
      assert(curNode);
      TEMP::Temp* curTemp = curNode->NodeInfo();
      assert(curTemp);

      /* node2degree */
      node2degree[curNode] = 0;
      for (G::NodeList<TEMP::Temp>* successors = curNode->Succ(); successors; successors = successors->tail) {
        assert(successors->head);
        assert(successors->head != curNode);
        node2degree[curNode]++;
      }

      /* node2color */
      node2color[curNode] = F::defaultRegisterColor(curTemp);
      if (node2color[curNode] != -1) {
        precolored = new G::NodeList<TEMP::Temp>(curNode, precolored);
      }

      /* node2alias */
      node2alias[curNode] = curNode;

      /* node2moveList */
      node2moveList[curNode] = nullptr;
      for (LIVE::MoveList* head = liveGraph.moves; head; head = head->tail) {
        if (head->src == curNode || head->dst == curNode) {
          node2moveList[curNode] = new LIVE::MoveList(head->src, head->dst, node2moveList[curNode]);
        }
      }
    }
  }

  void MakeWorklist() {
    G::NodeList<TEMP::Temp>* nodes = liveGraph.graph->Nodes();
    for (; nodes; nodes = nodes->tail) {
      G::Node<TEMP::Temp>* node = nodes->head;
      if (G::inNodeList(node, precolored))
        continue; // Skip precolored nodes here
      if (node2degree[node] >= F::K) {
        spillWorklist = new G::NodeList<TEMP::Temp>(node, spillWorklist);
      }
      else {
        if (MoveRelated(node)) {
          freezeWorklist = new G::NodeList<TEMP::Temp>(node, freezeWorklist);
        }
        else {
          simplifyWorklist = new G::NodeList<TEMP::Temp>(node, simplifyWorklist);
        }
      }
    }
  }

  bool MoveRelated(G::Node<TEMP::Temp>* n) {
    assert(n);
    return NodeMoves(n) != nullptr;
  }

  LIVE::MoveList* NodeMoves(G::Node<TEMP::Temp>* n) {
    assert(n);
    return LIVE::intersectMoveList(node2moveList[n], 
                                  LIVE::unionMoveList(activeMoves, worklistMoves));
  }

  void Simplify() {
    G::Node<TEMP::Temp>* node = simplifyWorklist->head;
    simplifyWorklist = simplifyWorklist->tail;
    selectStack = new G::NodeList<TEMP::Temp>(node, selectStack);
    G::NodeList<TEMP::Temp>* adj = Adjacent(node);
    for (; adj; adj = adj->tail) {
      DecrementDegree(adj->head);
    }
  }

  void DecrementDegree(G::Node<TEMP::Temp>* n) {
    assert(n);
    assert(node2degree.find(n) != node2degree.end());
    int oldDegree = node2degree[n];
    node2degree[n]--;
    if (oldDegree == F::K && node2color[n] == -1) { // TODO: node2color[n] == -1? Why?
      EnableMoves(new G::NodeList<TEMP::Temp>(n, Adjacent(n)));
      spillWorklist = G::minusNodeList(spillWorklist, new G::NodeList<TEMP::Temp>(n, nullptr));
      if (MoveRelated(n)) {
        freezeWorklist = new G::NodeList<TEMP::Temp>(n, freezeWorklist);
      }
      else {
        simplifyWorklist = new G::NodeList<TEMP::Temp>(n, simplifyWorklist);
      }
    }
  }

  void EnableMoves(G::NodeList<TEMP::Temp>* nodes) {
    for (; nodes; nodes = nodes->tail) {
      for (LIVE::MoveList* m = NodeMoves(nodes->head); m; m = m->tail) {
        if (LIVE::inMoveList(m->src, m->dst, activeMoves)) {
          activeMoves = LIVE::minusMoveList(activeMoves, new LIVE::MoveList(m->src, m->dst, nullptr));
          worklistMoves = new LIVE::MoveList(m->src, m->dst, worklistMoves);
        }
      }
    }
  }

  G::NodeList<TEMP::Temp>* Adjacent(G::Node<TEMP::Temp>* n) {
    assert(n);
    G::NodeList<TEMP::Temp>* adjList = n->Succ();
    return G::minusNodeList(adjList, G::unionNodeList(selectStack, coalescedNodes));
  }

  void Coalesce() {
    G::Node<TEMP::Temp>* x, *y, *u, *v;
    x = worklistMoves->src;
    y = worklistMoves->dst;

    if (G::inNodeList(GetAlias(y), precolored)) {
      u = GetAlias(y);
      v = GetAlias(x);
    }
    else {
      u = GetAlias(x);
      v = GetAlias(y);
    }
    worklistMoves = worklistMoves->tail;

    if (u == v) {
      coalescedMoves = new LIVE::MoveList(x, y, coalescedMoves);
      AddWorkList(u);
    }
    else if (G::inNodeList(v, precolored) || G::inNodeList(u, v->Adj())) {
      constrainedMoves = new LIVE::MoveList(x, y, constrainedMoves);
      AddWorkList(u);
      AddWorkList(v);
    }
    else if ((G::inNodeList(u, precolored) && OK(v, u)) // Note: OK is implemented differently from the text book
    || (!G::inNodeList(u, precolored) && Conservative(G::unionNodeList(Adjacent(u), Adjacent(v))))) {
      coalescedMoves = new LIVE::MoveList(x, y, coalescedMoves);
      Combine(u, v);
      AddWorkList(u);
    }
    else {
      activeMoves = new LIVE::MoveList(x, y, activeMoves);
    }
  }

  G::Node<TEMP::Temp>* GetAlias(G::Node<TEMP::Temp>* n) {
    if (G::inNodeList(n, coalescedNodes)) {
      return GetAlias(node2alias[n]);
    }
    else
      return n;
  }

  void AddWorkList(G::Node<TEMP::Temp>* n) {
    assert(n);
    if (!G::inNodeList(n, precolored) && !MoveRelated(n) && node2degree[n] < F::K) {
      freezeWorklist = G::minusNodeList(freezeWorklist, new G::NodeList<TEMP::Temp>(n, nullptr));
      simplifyWorklist = new G::NodeList<TEMP::Temp>(n, simplifyWorklist);
    }
  }

  bool OK(G::Node<TEMP::Temp>* t, G::Node<TEMP::Temp>* r) {
    // Note: OK is implemented differently from the text book
    assert(t && r);
    for (G::NodeList<TEMP::Temp>* adj = Adjacent(t); adj; adj = adj->tail) {
      G::Node<TEMP::Temp>* h = adj->head;
      if (!(node2degree[h] < F::K) || G::inNodeList(h, precolored) || G::inNodeList(h, r->Adj())) {
        return false; // This is the OK condition in the text book.
      }
    }
    return true;
  }

  bool Conservative(G::NodeList<TEMP::Temp>* nodes) {
    int count = 0;
    for (; nodes; nodes = nodes->tail) {
      assert(node2degree.find(nodes->head) != node2degree.end());
      if (node2degree[nodes->head] >= F::K) {
        count++;
      }
    }
    return count < F::K;
  }

  void Combine(G::Node<TEMP::Temp>* u, G::Node<TEMP::Temp>* v) {
    assert(u && v);
    if (G::inNodeList(v, freezeWorklist)) {
      freezeWorklist = G::minusNodeList(freezeWorklist, new G::NodeList<TEMP::Temp>(v, nullptr));
    }
    else {
      spillWorklist = G::minusNodeList(spillWorklist, new G::NodeList<TEMP::Temp>(v, nullptr));
    }
    coalescedNodes = new G::NodeList<TEMP::Temp>(v, coalescedNodes);
    node2alias[v] = u;
    node2moveList[u] = LIVE::unionMoveList(node2moveList[u], node2moveList[v]); // TODO: nodeMove?
    for (G::NodeList<TEMP::Temp>* adj = Adjacent(v); adj; adj = adj->tail) {
      G::Node<TEMP::Temp>* t = adj->head;
      AddEdge(t, u);
      DecrementDegree(t);
    }
    if (node2degree[u] >= F::K && G::inNodeList(u, freezeWorklist)) {
      freezeWorklist = G::minusNodeList(freezeWorklist, new G::NodeList<TEMP::Temp>(u, nullptr));
      spillWorklist = new G::NodeList<TEMP::Temp>(u, spillWorklist);
    }
  }

  void AddEdge(G::Node<TEMP::Temp>* u, G::Node<TEMP::Temp>* v) {
    assert(u && v);
    if (!G::inNodeList(u, v->Adj()) && u != v) {
      if (!G::inNodeList(u, precolored)) {
        G::Graph<TEMP::Temp>::AddEdge(u, v);
        node2degree[u]++;
      }
      if (!G::inNodeList(v, precolored)) {
        G::Graph<TEMP::Temp>::AddEdge(v, u);
        node2degree[v]++;
      }
    }
  }

  void Freeze() {
    G::Node<TEMP::Temp>* u = freezeWorklist->head;
    freezeWorklist = freezeWorklist->tail;
    simplifyWorklist = new G::NodeList<TEMP::Temp>(u, simplifyWorklist);
    FreezeMoves(u);
  }

  void FreezeMoves(G::Node<TEMP::Temp>* u) {
    assert(u);
    for (LIVE::MoveList* m = NodeMoves(u); m; m = m->tail) {
      G::Node<TEMP::Temp>* x = m->src;
      G::Node<TEMP::Temp>* y = m->dst;

      G::Node<TEMP::Temp>* v;
      if (GetAlias(y) == GetAlias(u)) {
        v = GetAlias(x);
      }
      else {
        v = GetAlias(y);
      }
      activeMoves = LIVE::minusMoveList(activeMoves, new LIVE::MoveList(m->src, m->dst, nullptr));
      frozenMoves = new LIVE::MoveList(m->src, m->dst, frozenMoves);
      if (!NodeMoves(v) && node2degree[v] < F::K) {
        freezeWorklist = G::minusNodeList(freezeWorklist, new G::NodeList<TEMP::Temp>(v, nullptr));
        simplifyWorklist = new G::NodeList<TEMP::Temp>(v, simplifyWorklist);
      }
    }
  }

  void SelectSpill() {
    G::Node<TEMP::Temp>* m = spillWorklist->head;
    spillWorklist = spillWorklist->tail;
    simplifyWorklist = new G::NodeList<TEMP::Temp>(m, simplifyWorklist);
    FreezeMoves(m);
  }

  void AssignColors() {
    assert(spilledNodes == nullptr);
    assert(coloredNodes == nullptr);
    while (selectStack) {
      G::Node<TEMP::Temp>* n = selectStack->head;
      selectStack = selectStack->tail;
      std::set<int> okColors;
      for (int i = 0; i < F::K; ++i)
        okColors.insert(i);
      for (G::NodeList<TEMP::Temp>* adj = n->Succ(); adj; adj = adj->tail) {
        // TODO
        G::Node<TEMP::Temp>* w = adj->head;
        if (G::inNodeList(GetAlias(w), G::unionNodeList(coloredNodes, precolored))) {
          int color = node2color[GetAlias(w)];
          assert(color != -1);
          okColors.erase(color);
        }
      }
      if (okColors.empty()) {
        spilledNodes = new G::NodeList<TEMP::Temp>(n, spilledNodes);
      }
      else {
        coloredNodes = new G::NodeList<TEMP::Temp>(n, coloredNodes);
        int c = *(okColors.begin());
        node2color[n] = c;
      }
    }
    for (G::NodeList<TEMP::Temp>* head = coalescedNodes; head; head = head->tail) {
      G::Node<TEMP::Temp>* n = head->head;
      node2color[n] = node2color[GetAlias(n)];
    }
  }
}
