#include "tiger/escape/escape.h"

namespace ESC {

class EscapeEntry {
 public:
  int depth;
  bool* escape;

  EscapeEntry(int depth, bool* escape) : depth(depth), escape(escape) {}
};

void FindEscape(A::Exp* exp) {
  S::Table<ESC::EscapeEntry> *env = new S::Table<ESC::EscapeEntry>();
  exp->Traverse(env, 0);
}

}  // namespace ESC

namespace A {

  void SimpleVar::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void FieldVar::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void SubscriptVar::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void VarExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void NilExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void IntExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void StringExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void CallExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void OpExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void RecordExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void SeqExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void AssignExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void IfExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void WhileExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void ForExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void BreakExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void LetExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void ArrayExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void VoidExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void FunctionDec::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void VarDec::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }

  void TypeDec::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    return;
  }
}  // namespace A
