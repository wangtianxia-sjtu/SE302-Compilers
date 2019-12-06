#include "tiger/codegen/codegen.h"

namespace {
  AS::InstrList* iList = nullptr;
  AS::InstrList* last = nullptr;

  void emit(AS::Instr* inst) {
    if (last) {
      last->tail = new AS::InstrList(inst, nullptr);
      last = last->tail;
    }
    else {
      iList = new AS::InstrList(inst, nullptr);
      last = iList;
    }
  }
}

namespace CG {

AS::InstrList* Codegen(F::Frame* f, T::StmList* stmList) {
  // TODO: Put your codes here (lab6).
  return nullptr;
}

}  // namespace CG