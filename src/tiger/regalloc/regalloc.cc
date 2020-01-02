#include "tiger/regalloc/regalloc.h"
#include "tiger/liveness/flowgraph.h"
#include "tiger/liveness/liveness.h"
#include <vector>
#include <set>
#include <map>
#include <iostream>

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

  std::vector<G::Node<TEMP::Temp> *> toTempVector(G::NodeList<TEMP::Temp>* templist);
  G::NodeList<TEMP::Temp>* toTempList(const std::vector<G::Node<TEMP::Temp> *>& tempVector);
  G::Node<TEMP::Temp>* selectNodeFromSpillWorklist();


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

  void addBefore(std::vector<AS::Instr *>& iVector, AS::Instr* pos, AS::Instr* newInstr);
  void addAfter(std::vector<AS::Instr *>& iVector, AS::Instr* pos, AS::Instr* newInstr);
  AS::Instr* findSpilledInstr(std::vector<AS::Instr *>& iVector, TEMP::Temp* spilledTemp);

  void AssertNode(G::Node<TEMP::Temp>* n);
  void AssertWorkList(LIVE::MoveList* l);

  int length(G::NodeList<TEMP::Temp>* l);
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
    else {
      done = true;
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

  std::vector<G::Node<TEMP::Temp> *> toTempVector(G::NodeList<TEMP::Temp>* templist) {
    std::vector<G::Node<TEMP::Temp> *> result;
    while (templist) {
      result.push_back(templist->head);
      templist = templist->tail;
    }
    return result;
  }

  G::NodeList<TEMP::Temp>* toTempList(const std::vector<G::Node<TEMP::Temp> *>& tempVector) {
    G::NodeList<TEMP::Temp>* result = nullptr;
    for (std::vector<G::Node<TEMP::Temp> *>::const_reverse_iterator cri = tempVector.crbegin(); cri != tempVector.crend(); ++cri) {
      result = new G::NodeList<TEMP::Temp>(*cri, result);
    }
    return result;
  }

  G::Node<TEMP::Temp>* selectNodeFromSpillWorklist() {
    std::vector<G::Node<TEMP::Temp> *> tempVector = toTempVector(spillWorklist);
    std::vector<G::Node<TEMP::Temp> *>::iterator target = tempVector.begin();
    int maxDegree = 0;
    for (std::vector<G::Node<TEMP::Temp> *>::iterator it = tempVector.begin(); it != tempVector.end(); ++it) {
      G::Node<TEMP::Temp>* node = *it;
      if (node2degree[node] > maxDegree) {
        maxDegree = node2degree[node];
        target = it;
      }
    }
    G::Node<TEMP::Temp>* result = *target;
    tempVector.erase(target);
    spillWorklist = toTempList(tempVector);
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
      AssertNode(node);
    }
    AssertWorkList(worklistMoves);
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
    AssertWorkList(worklistMoves);
    G::Node<TEMP::Temp>* node = simplifyWorklist->head;
    simplifyWorklist = simplifyWorklist->tail;
    selectStack = new G::NodeList<TEMP::Temp>(node, selectStack);
    G::NodeList<TEMP::Temp>* adj = Adjacent(node);
    for (; adj; adj = adj->tail) {
      DecrementDegree(adj->head);
    }
    AssertNode(node);
  }

  void DecrementDegree(G::Node<TEMP::Temp>* n) {
    assert(n);
    assert(node2degree.find(n) != node2degree.end());
    int oldDegree = node2degree[n];
    node2degree[n]--;
    if (oldDegree == F::K) {
      EnableMoves(new G::NodeList<TEMP::Temp>(n, Adjacent(n)));
      if (node2color[n] == -1) {
        // Only non-precolored nodes can be removed from spillWorklist
        spillWorklist = G::minusNodeList(spillWorklist, new G::NodeList<TEMP::Temp>(n, nullptr));
        if (MoveRelated(n)) {
          freezeWorklist = new G::NodeList<TEMP::Temp>(n, freezeWorklist);
        }
        else {
          simplifyWorklist = new G::NodeList<TEMP::Temp>(n, simplifyWorklist);
        }
      }
    }
    AssertNode(n);
  }

  void EnableMoves(G::NodeList<TEMP::Temp>* nodes) {
    for (; nodes; nodes = nodes->tail) {
      for (LIVE::MoveList* m = NodeMoves(nodes->head); m; m = m->tail) {
        if (LIVE::inMoveList(m->src, m->dst, activeMoves)) {
          activeMoves = LIVE::minusMoveList(activeMoves, new LIVE::MoveList(m->src, m->dst, nullptr));
          worklistMoves = new LIVE::MoveList(m->src, m->dst, worklistMoves);
          AssertNode(m->src);
          AssertNode(m->dst);
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
    AssertWorkList(worklistMoves);
    G::Node<TEMP::Temp>* x, *y, *u, *v;
    x = worklistMoves->src;
    y = worklistMoves->dst;

    AssertNode(x);
    AssertNode(y);

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
      AssertNode(u);
      Combine(u, v);
      AddWorkList(u);
    }
    else {
      activeMoves = new LIVE::MoveList(x, y, activeMoves);
    }
  }

  G::Node<TEMP::Temp>* GetAlias(G::Node<TEMP::Temp>* n) {
    assert(n);
    if (G::inNodeList(n, coalescedNodes)) {
      return GetAlias(node2alias[n]);
    }
    else
      return n;
  }

  void AddWorkList(G::Node<TEMP::Temp>* n) {
    AssertNode(n);
    assert(n);
    if (!G::inNodeList(n, precolored) && !MoveRelated(n) && node2degree[n] < F::K) {
      freezeWorklist = G::minusNodeList(freezeWorklist, new G::NodeList<TEMP::Temp>(n, nullptr));
      simplifyWorklist = new G::NodeList<TEMP::Temp>(n, simplifyWorklist);
    }
    AssertNode(n);
  }

  bool OK(G::Node<TEMP::Temp>* t, G::Node<TEMP::Temp>* r) {
    // Note: OK is implemented differently from the text book
    assert(t && r);
    AssertNode(t);
    AssertNode(r);
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
    AssertWorkList(worklistMoves);
    assert(u && v);
    if (G::inNodeList(v, freezeWorklist)) {
      freezeWorklist = G::minusNodeList(freezeWorklist, new G::NodeList<TEMP::Temp>(v, nullptr));
    }
    else {
      spillWorklist = G::minusNodeList(spillWorklist, new G::NodeList<TEMP::Temp>(v, nullptr));
    }
    coalescedNodes = new G::NodeList<TEMP::Temp>(v, coalescedNodes);
    node2alias[v] = u;
    AssertNode(u);
    node2moveList[u] = LIVE::unionMoveList(node2moveList[u], node2moveList[v]);
    AssertWorkList(worklistMoves);
    for (G::NodeList<TEMP::Temp>* adj = Adjacent(v); adj; adj = adj->tail) {
      G::Node<TEMP::Temp>* t = adj->head;
      AddEdge(t, u);
      DecrementDegree(t);
    }
    if (node2degree[u] >= F::K && G::inNodeList(u, freezeWorklist)) {
      freezeWorklist = G::minusNodeList(freezeWorklist, new G::NodeList<TEMP::Temp>(u, nullptr));
      spillWorklist = new G::NodeList<TEMP::Temp>(u, spillWorklist);
    }
    AssertNode(u);
    AssertWorkList(worklistMoves);
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
    AssertNode(u);
    AssertNode(v);
  }

  void Freeze() {
    G::Node<TEMP::Temp>* u = freezeWorklist->head;
    freezeWorklist = freezeWorklist->tail;
    simplifyWorklist = new G::NodeList<TEMP::Temp>(u, simplifyWorklist);
    FreezeMoves(u);
    AssertNode(u);
  }

  void FreezeMoves(G::Node<TEMP::Temp>* u) {
    assert(u);
    for (LIVE::MoveList* m = NodeMoves(u); m; m = m->tail) {
      G::Node<TEMP::Temp>* x = m->src;
      G::Node<TEMP::Temp>* y = m->dst;

      AssertNode(x);
      AssertNode(y);

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
    G::Node<TEMP::Temp>* m = selectNodeFromSpillWorklist();
    // G::Node<TEMP::Temp>* m = spillWorklist->head;
    // spillWorklist = spillWorklist->tail;
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
        assert(c != -1);
        node2color[n] = c;
      }
    }
    for (G::NodeList<TEMP::Temp>* head = coalescedNodes; head; head = head->tail) {
      G::Node<TEMP::Temp>* n = head->head;
      int color = node2color[GetAlias(n)];
      AssertNode(n);
      AssertNode(GetAlias(n));
      assert(color != -1);
      node2color[n] = color;
    }
  }

  void RewriteProgram(F::Frame* f) {
    std::string fs = f->GetName()->Name() + "_framesize";
    for (; spilledNodes; spilledNodes = spilledNodes->tail) {
      G::Node<TEMP::Temp>* nodeToSpill = spilledNodes->head;
      TEMP::Temp* tempToSpill = nodeToSpill->NodeInfo();
      assert(!TEMP::inTempList(tempToSpill, F::allocatableRegisters())); // A machine register should never be spilled
      AS::Instr* spilledInstr = nullptr;
      F::Access* access = f->AllocLocal(true);
      int offset = f->GetSize(); // Now the size is the offset of certain variable in frame
      while ((spilledInstr = findSpilledInstr(instrVector, tempToSpill)) != nullptr) {
        // We have found an instruction which includes a use of the "tempToSpill"
        TEMP::Temp* newTemp = TEMP::Temp::NewTemp();
        TEMP::TempList* def = spilledInstr->GetDef();
        TEMP::TempList* use = spilledInstr->GetUse();

        if (TEMP::inTempList(tempToSpill, use)) {
          // This instruction will use the "tempToSpill"
          std::string assem = "movq (" + fs + "-" + std::to_string(offset) + ")(%rsp), `d0";
          AS::OperInstr* newInstr = new AS::OperInstr(assem, new TEMP::TempList(newTemp, nullptr), nullptr, new AS::Targets(nullptr));
          addBefore(instrVector, spilledInstr, newInstr);
          TEMP::replaceTemps(use, tempToSpill, newTemp); // Replace the spilled temp with the new one
        }

        if (TEMP::inTempList(tempToSpill, def)) {
          // This instruction will def the "tempToSpill"
          std::string assem = "movq `s0, (" + fs + "-" + std::to_string(offset) + ")(%rsp)";
          AS::OperInstr* newInstr = new AS::OperInstr(assem, nullptr, new TEMP::TempList(newTemp, nullptr), new AS::Targets(nullptr));
          addAfter(instrVector, spilledInstr, newInstr);
          TEMP::replaceTemps(def, tempToSpill, newTemp);
        }

        assert(!TEMP::inTempList(tempToSpill, use) && !TEMP::inTempList(tempToSpill, def));
      }
    }
    assert(!spilledNodes);
  }

  void addBefore(std::vector<AS::Instr *>& iVector, AS::Instr* pos, AS::Instr* newInstr) {
    std::vector<AS::Instr *>::iterator it;
    for (it = iVector.begin(); it != iVector.end(); ++it) {
      if ((*it) == pos)
        break;
    }
    if (it == iVector.end())
      assert(0);
    iVector.insert(it, newInstr);
  }

  void addAfter(std::vector<AS::Instr *>& iVector, AS::Instr* pos, AS::Instr* newInstr) {
    std::vector<AS::Instr *>::iterator it;
    bool found = false;
    for (it = iVector.begin(); it != iVector.end(); ++it) {
      if ((*it) == pos) {
        ++it;
        found = true;
        break;
      }
    }
    if (found == false)
      assert(0);
    iVector.insert(it, newInstr);
  }

  AS::Instr* findSpilledInstr(std::vector<AS::Instr *>& iVector, TEMP::Temp* spilledTemp) {
    for (std::vector<AS::Instr *>::iterator it = iVector.begin(); it != iVector.end(); ++it) {
      TEMP::TempList* def = (*it)->GetDef();
      TEMP::TempList* use = (*it)->GetUse();
      if (TEMP::inTempList(spilledTemp, def) || TEMP::inTempList(spilledTemp, use)) {
        return (*it);
      }
    }
    return nullptr;
  }

  TEMP::Map* AssignRegisters() {
    TEMP::Map* result = TEMP::Map::Empty();
    result->Enter(F::SP(), new std::string("%rsp"));
    G::NodeList<TEMP::Temp>* nodes = liveGraph.graph->Nodes();
    for (; nodes; nodes = nodes->tail) {
      int color = node2color[nodes->head];
      assert(color != -1);
      result->Enter(nodes->head->NodeInfo(), F::color2register(color));
    }
    return result;
  }

  void AssertNode(G::Node<TEMP::Temp>* n) {
    static std::map<G::Node<TEMP::Temp>*, int> m;
    if (!G::inNodeList((n), coalescedNodes) &&
        !G::inNodeList((n), precolored) &&
        !G::inNodeList((n), simplifyWorklist) &&
        !G::inNodeList((n), freezeWorklist) &&
        !G::inNodeList((n), spillWorklist) &&
        !G::inNodeList((n), spilledNodes) &&
        !G::inNodeList((n), coloredNodes) &&
        !G::inNodeList((n), selectStack)) {
          std::cerr << "Unknown node: " << n << std::endl;
          if (m.find(n) != m.end())
            std::cerr << "Used to belong to: " << m[n] << std::endl;
          else
            std::cerr << "Not seen before" << std::endl;
          assert(0);
    }
    else {
      if (G::inNodeList((n), coalescedNodes)) {
        m[n] = 0;
      }
      if (G::inNodeList((n), precolored)) {
        m[n] = 1;
      }
      if (G::inNodeList((n), simplifyWorklist)) {
        m[n] = 2;
      }
      if (G::inNodeList((n), freezeWorklist)) {
        m[n] = 3;
      }
      if (G::inNodeList((n), spillWorklist)) {
        m[n] = 4;
      }
      if (G::inNodeList((n), spilledNodes)) {
        m[n] = 5;
      }
      if (G::inNodeList((n), coloredNodes)) {
        m[n] = 6;
      }
      if (G::inNodeList((n), selectStack)) {
        m[n] = 7;
      }
    }
  }

  void AssertWorkList(LIVE::MoveList* l) {
    for (; l; l = l->tail) {
      AssertNode(l->src);
      AssertNode(l->dst);
    }
  }

  int length(G::NodeList<TEMP::Temp>* l) {
    int i = 0;
    for(; l; l = l->tail) {
      ++i;
    }
    return i;
  }
}
