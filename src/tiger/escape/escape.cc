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
    ESC::EscapeEntry* e = env->Look(sym);
    if (!e)
      return;
    if (depth > e->depth) {
      *(e->escape) = true;
    }
  }

  void FieldVar::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    var->Traverse(env, depth);
  }

  void SubscriptVar::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) const {
    var->Traverse(env, depth);
    subscript->Traverse(env, depth);
  }

  void VarExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    var->Traverse(env, depth);
  }

  void NilExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    return;
  }

  void IntExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    return;
  }

  void StringExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    return;
  }

  void CallExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    ExpList* head = args;
    for (; head; head = head->tail) {
      head->head->Traverse(env, depth);
    }
  }

  void OpExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    left->Traverse(env, depth);
    right->Traverse(env, depth);
  }

  void RecordExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    for (EFieldList* head = fields; head; head = head->tail) {
      head->head->exp->Traverse(env, depth);
    }
  }

  void SeqExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    for (ExpList* head = seq; head; head = head->tail) {
      head->head->Traverse(env, depth);
    }
  }

  void AssignExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    var->Traverse(env, depth);
    exp->Traverse(env, depth);
  }

  void IfExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    test->Traverse(env, depth);
    then->Traverse(env, depth);
    if (elsee)
      elsee->Traverse(env, depth);
  }

  void WhileExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    test->Traverse(env, depth);
    body->Traverse(env, depth);
  }

  void ForExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    escape = false;
    ESC::EscapeEntry* e = new ESC::EscapeEntry(depth, &escape);
    env->BeginScope();
    env->Enter(var, e);
    lo->Traverse(env, depth);
    hi->Traverse(env, depth);
    body->Traverse(env, depth);
    env->EndScope();
  }

  void BreakExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    return;
  }

  void LetExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    env->BeginScope();
    for (DecList* head = decs; head; head = head->tail) {
      head->head->Traverse(env, depth);
    }
    body->Traverse(env, depth);
    env->EndScope();
  }

  void ArrayExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    size->Traverse(env, depth);
    init->Traverse(env, depth);
  }

  void VoidExp::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    return;
  }

  void FunctionDec::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    for (FunDecList* funDecHead = functions; funDecHead; funDecHead = funDecHead->tail) {
      FunDec* funDec = funDecHead->head;
      env->BeginScope();
      for (FieldList* field = funDec->params; field; field = field->tail) {
        Field* f = field->head;
        f->escape = false;
        ESC::EscapeEntry* e = new ESC::EscapeEntry(depth+1, &f->escape);
        env->Enter(f->name, e);
      }
      funDec->body->Traverse(env, depth+1);
      env->EndScope();
    }
  }

  void VarDec::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    escape = false;
    env->Enter(var, new ESC::EscapeEntry(depth, &escape));
    init->Traverse(env, depth);
  }

  void TypeDec::Traverse(S::Table<ESC::EscapeEntry> *env, int depth) {
    return;
  }
}  // namespace A
