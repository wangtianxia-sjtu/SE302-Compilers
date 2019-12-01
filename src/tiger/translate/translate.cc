#include "tiger/translate/translate.h"

#include <cstdio>
#include <set>
#include <string>
#include <iostream>
#include <vector>

#include "tiger/errormsg/errormsg.h"
#include "tiger/frame/temp.h"
#include "tiger/semant/semant.h"
#include "tiger/semant/types.h"
#include "tiger/util/util.h"

extern EM::ErrorMsg errormsg;

namespace {
  F::FragList* globalFrags = nullptr;
  const std::string stringEqual = "stringEqual";
  void AddToGlobalFrags(F::Frag* newFrag) {
    F::FragList* tail = globalFrags;
    if (tail) {
      while (tail->tail) {
        tail = tail->tail;
      }
      tail->tail = new F::FragList(newFrag, nullptr);
    }
    else {
      globalFrags = new F::FragList(newFrag, nullptr);
    }
  }
  T::ExpList* ToExpList(const std::vector<TR::Exp *> &formalsVector);
  TR::Exp* Calculate(A::Oper op, TR::Exp* left, TR::Exp* right);
  TR::Exp* Conditional(A::Oper op, TR::Exp* left, TR::Exp* right, bool isString);
}

namespace TR {

class AccessList {
 public:
  Access *head;
  AccessList *tail;

  AccessList(Access *head, AccessList *tail) : head(head), tail(tail) {}
};

class Level {
 public:
  F::Frame *frame;
  Level *parent;

  Level(F::Frame *frame, Level *parent) : frame(frame), parent(parent) {}

  AccessList *Formals(Level *level);

  static Level *NewLevel(Level *parent, TEMP::Label *name, U::BoolList *formals) {
    U::BoolList* formalsWithStaticLink = new U::BoolList(true, formals);
    F::Frame* frame = F::NewX64Frame(name, formalsWithStaticLink);
    Level* level = new Level(frame, parent);
    return level;
  }
};

class Access {
 public:
  Level *level;
  F::Access *access;

  Access(Level *level, F::Access *access) : level(level), access(access) {}

  static Access *AllocLocal(Level *level, bool escape) {
    F::Access* access = level->frame->AllocLocal(escape);
    return new Access(level, access);
  }
};

class PatchList {
 public:
  TEMP::Label **head;
  PatchList *tail;

  PatchList(TEMP::Label **head, PatchList *tail) : head(head), tail(tail) {}
};

class Cx {
 public:
  PatchList *trues;
  PatchList *falses;
  T::Stm *stm;

  Cx(PatchList *trues, PatchList *falses, T::Stm *stm)
      : trues(trues), falses(falses), stm(stm) {}
};

class Exp {
 public:
  enum Kind { EX, NX, CX };

  Kind kind;

  Exp(Kind kind) : kind(kind) {}

  virtual T::Exp *UnEx() const = 0;
  virtual T::Stm *UnNx() const = 0;
  virtual Cx UnCx() const = 0;
};

class ExpAndTy {
 public:
  TR::Exp *exp;
  TY::Ty *ty;

  ExpAndTy(TR::Exp *exp, TY::Ty *ty) : exp(exp), ty(ty) {}
};

void do_patch(PatchList *tList, TEMP::Label *label) {
  for (; tList; tList = tList->tail) *(tList->head) = label;
}

PatchList *join_patch(PatchList *first, PatchList *second) {
  if (!first) return second;
  for (; first->tail; first = first->tail)
    ;
  first->tail = second;
  return first;
}

class ExExp : public Exp {
 public:
  T::Exp *exp;

  ExExp(T::Exp *exp) : Exp(EX), exp(exp) {}

  T::Exp *UnEx() const override {
    return exp;
  }

  T::Stm *UnNx() const override {
    return new T::ExpStm(exp);
  }

  Cx UnCx() const override {
    T::CjumpStm* stm = new T::CjumpStm(T::NE_OP, exp, new T::ConstExp(0), nullptr, nullptr);
    PatchList* trues = new PatchList(&(stm->true_label), nullptr);
    PatchList* falses = new PatchList(&(stm->false_label), nullptr);
    return Cx(trues, falses, stm);
  }
};

class NxExp : public Exp {
 public:
  T::Stm *stm;

  NxExp(T::Stm *stm) : Exp(NX), stm(stm) {}

  T::Exp *UnEx() const override {
    return new T::EseqExp(stm, new T::ConstExp(0));
  }

  T::Stm *UnNx() const override {
    return stm;
  }

  Cx UnCx() const override {
    std::cerr << "Error: NxExp cannot be a Cx" << std::endl;
    assert(0);
  }

};

class CxExp : public Exp {
 public:
  Cx cx;

  CxExp(struct Cx cx) : Exp(CX), cx(cx) {}
  CxExp(PatchList *trues, PatchList *falses, T::Stm *stm)
      : Exp(CX), cx(trues, falses, stm) {}

  T::Exp *UnEx() const override {
    // TODO: Eliminate NewTemp here P157
    TEMP::Temp* r = TEMP::Temp::NewTemp();
    TEMP::Label* t = TEMP::NewLabel();
    TEMP::Label* f = TEMP::NewLabel();
    do_patch(cx.trues, t);
    do_patch(cx.falses, f);
    return new T::EseqExp(new T::MoveStm(new T::TempExp(r), new T::ConstExp(1)),
                new T::EseqExp(cx.stm, 
                  new T::EseqExp(new T::LabelStm(f),
                    new T::EseqExp(new T::MoveStm(new T::TempExp(r), new T::ConstExp(0)),
                      new T::EseqExp(new T::LabelStm(t), new T::TempExp(r))))));
  }

  T::Stm *UnNx() const override {
    TEMP::Label* done = TEMP::NewLabel();
    do_patch(cx.trues, done);
    do_patch(cx.falses, done);
    return new T::SeqStm(cx.stm, new T::LabelStm(done));
  }

  Cx UnCx() const override {
    return cx;
  }
};

Level *Outermost() {
  static Level *lv = nullptr;
  if (lv != nullptr) return lv;

  lv = new Level(nullptr, nullptr);
  return lv;
}

F::FragList *TranslateProgram(A::Exp *root) {
  // TODO: Needs Tr_procEntryExit and Tr_getResult here P173
  Level* mainFrame = Outermost();
  TEMP::Label* mainLabel = TEMP::NewLabel();
  ExpAndTy result = root->Translate(E::BaseVEnv(), E::BaseTEnv(), mainFrame, mainLabel);
  return nullptr;
}

 AccessList* Level::Formals(Level *level) {
    F::AccessList* f_accessList = level->frame->GetFormalList();
    AccessList* result = nullptr;
    // f_accessList = f_accessList->tail; // TODO: skip static link? TBD
    if (f_accessList) {
      AccessList* tail = nullptr;
      Access* tr_access = new Access(level, f_accessList->head);
      result = new AccessList(tr_access, nullptr);
      tail = result;
      f_accessList = f_accessList->tail;
      while (f_accessList) {
        tail->tail = new AccessList(new Access(level, f_accessList->head), nullptr);
        tail = tail->tail;
        f_accessList = f_accessList->tail;
      }
      return result;
    }
    return nullptr;
 }

}  // namespace TR

namespace A {

TR::ExpAndTy SimpleVar::Translate(S::Table<E::EnvEntry> *venv,
                                  S::Table<TY::Ty> *tenv, TR::Level *level,
                                  TEMP::Label *label) const {
  E::EnvEntry *envEntry = venv->Look(sym);
  if (!envEntry) {
    errormsg.Error(pos, "undefined variable %s", sym->Name().c_str());
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }
  if (envEntry->kind == E::EnvEntry::FUN) {
    errormsg.Error(pos, "function variable %s is not a simple value", sym->Name().c_str());
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }

  /* ----------------------------------------------------------------------- */

  TY::Ty* resultType = static_cast<E::VarEntry*>(envEntry)->ty;
  TR::Level* resultLevel = static_cast<E::VarEntry*>(envEntry)->access->level;
  T::Exp* framePtr = new T::TempExp(F::FP());
  while (level != resultLevel) {
    framePtr = new T::MemExp(new T::BinopExp(T::PLUS_OP, framePtr, new T::ConstExp(-F::wordSize))); // static link is the first in-frame parameter
    level = level->parent;
  }
  T::Exp* resultPtr = static_cast<E::VarEntry*>(envEntry)->access->access->ToExp(framePtr);
  return TR::ExpAndTy(new TR::ExExp(resultPtr), resultType);
}

TR::ExpAndTy FieldVar::Translate(S::Table<E::EnvEntry> *venv,
                                 S::Table<TY::Ty> *tenv, TR::Level *level,
                                 TEMP::Label *label) const {
  TR::ExpAndTy varResult = var->Translate(venv, tenv, level, label);
  TY::Ty* resultType = varResult.ty;
  if (!resultType || resultType->ActualTy()->kind != TY::Ty::Kind::RECORD) {
    errormsg.Error(pos, "not a record type");
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }
  resultType = resultType->ActualTy();
  TY::RecordTy* recordTy = static_cast<TY::RecordTy *>(resultType);
  TY::FieldList* fieldList = recordTy->fields;
  TY::Ty* fieldType = nullptr;
  int order = 0;
  while (fieldList) {
    if (!fieldList->head->name->Name().compare(sym->Name())) {
      fieldType = fieldList->head->ty;
      break;
    }
    fieldList = fieldList->tail;
    order++;
  }
  if (!fieldType) {
    errormsg.Error(pos, "field %s doesn't exist", sym->Name().c_str());
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }

  /* ----------------------------------------------------------------------- */

  TR::Exp* exp = varResult.exp;
  TR::Exp* fieldExp = new TR::ExExp(new T::MemExp(new T::BinopExp(T::PLUS_OP, exp->UnEx(), new T::ConstExp(order * F::wordSize))));
  return TR::ExpAndTy(fieldExp, fieldType);
}

TR::ExpAndTy SubscriptVar::Translate(S::Table<E::EnvEntry> *venv,
                                     S::Table<TY::Ty> *tenv, TR::Level *level,
                                     TEMP::Label *label) const {
  TR::ExpAndTy varResult = var->Translate(venv, tenv, level, label);
  TY::Ty* varType = varResult.ty;
  TR::Exp* varExp = varResult.exp;
  if (!varType || varType->ActualTy()->kind != TY::Ty::Kind::ARRAY) {
    errormsg.Error(pos, "array type required");
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }
  varType = varType->ActualTy();
  TY::ArrayTy* arrayTy = static_cast<TY::ArrayTy *>(varType);
  TY::Ty* elementType = arrayTy->ty;
  TR::ExpAndTy subscriptResult = subscript->Translate(venv, tenv, level, label);
  TY::Ty* subscriptType = subscriptResult.ty;
  TR::Exp* subscriptExp = subscriptResult.exp;
  if (!subscriptType || !subscriptType->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "integer required");
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }

  /* ----------------------------------------------------------------------- */

  TR::Exp* resultExp = new TR::ExExp(
                        new T::MemExp(
                          new T::BinopExp(T::PLUS_OP, varExp->UnEx(), 
                            new T::BinopExp(T::MUL_OP, subscriptExp->UnEx(), new T::ConstExp(F::wordSize)))));
  return TR::ExpAndTy(resultExp, elementType);
}

TR::ExpAndTy VarExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  TR::ExpAndTy result = var->Translate(venv, tenv, level, label);
  if (result.ty)
    return TR::ExpAndTy(result.exp, result.ty->ActualTy());
  return TR::ExpAndTy(result.exp, TY::VoidTy::Instance());
}

TR::ExpAndTy NilExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  return TR::ExpAndTy(new TR::ExExp(new T::ConstExp(0)), TY::NilTy::Instance());
}

TR::ExpAndTy IntExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  return TR::ExpAndTy(new TR::ExExp(new T::ConstExp(i)), TY::IntTy::Instance());
}

TR::ExpAndTy StringExp::Translate(S::Table<E::EnvEntry> *venv,
                                  S::Table<TY::Ty> *tenv, TR::Level *level,
                                  TEMP::Label *label) const {
  // TODO: P166-167
  TEMP::Label* tempLabel = TEMP::NewLabel();
  F::Frag* stringFrag = new F::StringFrag(tempLabel, s);
  AddToGlobalFrags(stringFrag);
  return TR::ExpAndTy(new TR::ExExp(new T::NameExp(tempLabel)), TY::StringTy::Instance());
}

TR::ExpAndTy CallExp::Translate(S::Table<E::EnvEntry> *venv,
                                S::Table<TY::Ty> *tenv, TR::Level *level,
                                TEMP::Label *label) const {
  std::vector<TR::Exp *> formalsVector;
  bool valid = true;
  E::EnvEntry* envEntry = venv->Look(func);
  if (!envEntry) {
    errormsg.Error(pos, "undefined function %s", func->Name().c_str());
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }
  if (envEntry->kind == E::EnvEntry::VAR) {
    errormsg.Error(pos, "%s is not a function", func->Name().c_str());
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }
  E::FunEntry* funEntry = static_cast<E::FunEntry *>(envEntry);
  TY::TyList* formals = funEntry->formals;
  A::ExpList* argList = args;
  while (formals && argList) {
    TY::Ty* formalsHead = formals->head;
    A::Exp* expHead = args->head;
    TR::ExpAndTy expHeadResult = expHead->Translate(venv, tenv, level, label);
    if (!formalsHead->IsSameType(expHeadResult.ty)) {
      errormsg.Error(pos, "para type mismatch");
      valid = false;
    }
    formalsVector.push_back(expHeadResult.exp);
    formals = formals->tail;
    argList = argList->tail;
  }
  TY::Ty *resultType = funEntry->result;
  if (!resultType)
    resultType = TY::VoidTy::Instance();
  else
    resultType = resultType->ActualTy();
  if (formals) {
    errormsg.Error(pos, "missing parameters in function %s", func->Name().c_str());
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }
  if (argList) {
    errormsg.Error(pos, "too many params in function %s", func->Name().c_str());
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }
  if (!valid) {
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }

  /* ----------------------------------------------------------------------- */

  T::Exp* staticLink = new T::TempExp(F::FP());
  while (level != funEntry->level) {
    staticLink = new T::MemExp(new T::BinopExp(T::PLUS_OP, staticLink, new T::ConstExp(-F::wordSize)));
    level = level->parent;
  }
  T::ExpList* expList = ToExpList(formalsVector);
  TR::Exp* resultExp = nullptr;
  if (funEntry->level->parent == nullptr) {
    // External call, no static link
    resultExp = new TR::ExExp(F::externalCall(funEntry->label->Name(), expList));
  }
  else {
    // Add static link
    expList = new T::ExpList(staticLink, expList);
    resultExp = new TR::ExExp(new T::CallExp(new T::NameExp(funEntry->label), expList));
  }
  return TR::ExpAndTy(resultExp, resultType);
}

TR::ExpAndTy OpExp::Translate(S::Table<E::EnvEntry> *venv,
                              S::Table<TY::Ty> *tenv, TR::Level *level,
                              TEMP::Label *label) const {
  TR::Exp* leftExp = nullptr;
  TR::Exp* rightExp = nullptr;
  bool isString = false;
  if (oper == A::PLUS_OP || oper == A::MINUS_OP || oper == A::TIMES_OP || oper == A::DIVIDE_OP) {
    TR::ExpAndTy leftResult = left->Translate(venv, tenv, level, label);
    TR::ExpAndTy rightResult = right->Translate(venv, tenv, level, label);
    leftExp = leftResult.exp;
    rightExp = rightResult.exp;
    if (!leftResult.ty || !leftResult.ty->IsSameType(TY::IntTy::Instance())
       || !rightResult.ty || !rightResult.ty->IsSameType(TY::IntTy::Instance())) {
        errormsg.Error(pos, "integer required");
        return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
    }

    /* ----------------------------------------------------------------------- */

    TR::Exp* resultExp = Calculate(oper, leftExp, rightExp);
    return TR::ExpAndTy(resultExp, TY::IntTy::Instance());
  }

  if (oper == A::EQ_OP || oper == A::NEQ_OP || oper == LT_OP || oper == LE_OP || oper == GT_OP || oper == GE_OP) {
    TR::ExpAndTy leftResult = left->Translate(venv, tenv, level, label);
    TR::ExpAndTy rightResult = right->Translate(venv, tenv, level, label);
    leftExp = leftResult.exp;
    rightExp = rightResult.exp;
    if (!leftResult.ty || !rightResult.ty || !leftResult.ty->IsSameType(rightResult.ty)) {
      errormsg.Error(pos, "same type required");
      return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
    }

    /* ----------------------------------------------------------------------- */

    if (leftResult.ty->IsSameType(TY::StringTy::Instance())) {
      isString = true;
    }
    TR::Exp* resultExp = Conditional(oper, leftExp, rightExp, isString);
    return TR::ExpAndTy(resultExp, TY::IntTy::Instance());
  }
  assert(0);
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy RecordExp::Translate(S::Table<E::EnvEntry> *venv,
                                  S::Table<TY::Ty> *tenv, TR::Level *level,
                                  TEMP::Label *label) const {
  // TODO: Here we assume that the order of a A::EFieldList and that of a TY::FieldList are the same
  std::vector<TR::Exp*> recordFieldsVector;
  TY::Ty *recordType = tenv->Look(typ);
  if (!recordType) {
    errormsg.Error(pos, "undefined type %s", typ->Name().c_str());
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }
  if (recordType->ActualTy()->kind != TY::Ty::RECORD) {
    errormsg.Error(pos, "%s is not a record", typ->Name().c_str());
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }

  recordType = recordType->ActualTy();
  TY::RecordTy* realRecordType = static_cast<TY::RecordTy *>(recordType);
  A::EFieldList* eFieldList = fields;
  TY::FieldList* fieldList = realRecordType->fields;
  while (eFieldList) {
    if (!fieldList) {
      errormsg.Error(pos, "Too many fields in %s", typ->Name().c_str());
      return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
    }
    A::EField* eFieldHead = eFieldList->head;
    TY::Field* fieldHead = fieldList->head;
    TR::ExpAndTy eFieldHeadResult = eFieldHead->exp->Translate(venv, tenv, level, label);
    recordFieldsVector.push_back(eFieldHeadResult.exp);
    if (!eFieldHeadResult.ty->IsSameType(fieldHead->ty)) {
      errormsg.Error(pos, "mismatched field type %s", eFieldHead->name->Name().c_str());
      return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
    }
    eFieldList = eFieldList->tail;
    fieldList = fieldList->tail;
  }
  if (fieldList) {
    errormsg.Error(pos, "Too few fields in %s", typ->Name().c_str());
    return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
  }

  /* ----------------------------------------------------------------------- */

  // TODO: Finish RecordExp
} 

TR::ExpAndTy SeqExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy AssignExp::Translate(S::Table<E::EnvEntry> *venv,
                                  S::Table<TY::Ty> *tenv, TR::Level *level,
                                  TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy IfExp::Translate(S::Table<E::EnvEntry> *venv,
                              S::Table<TY::Ty> *tenv, TR::Level *level,
                              TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy WhileExp::Translate(S::Table<E::EnvEntry> *venv,
                                 S::Table<TY::Ty> *tenv, TR::Level *level,
                                 TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy ForExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy BreakExp::Translate(S::Table<E::EnvEntry> *venv,
                                 S::Table<TY::Ty> *tenv, TR::Level *level,
                                 TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy LetExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy ArrayExp::Translate(S::Table<E::EnvEntry> *venv,
                                 S::Table<TY::Ty> *tenv, TR::Level *level,
                                 TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy VoidExp::Translate(S::Table<E::EnvEntry> *venv,
                                S::Table<TY::Ty> *tenv, TR::Level *level,
                                TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::Exp *FunctionDec::Translate(S::Table<E::EnvEntry> *venv,
                                S::Table<TY::Ty> *tenv, TR::Level *level,
                                TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return nullptr;
}

TR::Exp *VarDec::Translate(S::Table<E::EnvEntry> *venv, S::Table<TY::Ty> *tenv,
                           TR::Level *level, TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return nullptr;
}

TR::Exp *TypeDec::Translate(S::Table<E::EnvEntry> *venv, S::Table<TY::Ty> *tenv,
                            TR::Level *level, TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return nullptr;
}

TY::Ty *NameTy::Translate(S::Table<TY::Ty> *tenv) const {
  // TODO: Put your codes here (lab5).
  return TY::VoidTy::Instance();
}

TY::Ty *RecordTy::Translate(S::Table<TY::Ty> *tenv) const {
  // TODO: Put your codes here (lab5).
  return TY::VoidTy::Instance();
}

TY::Ty *ArrayTy::Translate(S::Table<TY::Ty> *tenv) const {
  // TODO: Put your codes here (lab5).
  return TY::VoidTy::Instance();
}

}  // namespace A

namespace {
  T::ExpList* ToExpList(const std::vector<TR::Exp *> &formalsVector) {
    T::ExpList* result = nullptr;
    T::ExpList* tail = nullptr;
    std::size_t s = formalsVector.size();
    if (s == 0)
      return nullptr;
    if (formalsVector[0])
      result = new T::ExpList(formalsVector[0]->UnEx(), nullptr);
    else
      result = new T::ExpList(nullptr, nullptr);
    tail = result;
    for (std::size_t i = 1; i < s; ++i) {
      if (formalsVector[i])
        tail->tail = new T::ExpList(formalsVector[i]->UnEx(), nullptr);
      else
        tail->tail = new T::ExpList(nullptr, nullptr);
      tail = tail->tail;
    }
    return result;
  }

  TR::Exp* Calculate(A::Oper op, TR::Exp* left, TR::Exp* right) {
    switch (op) {
      case A::PLUS_OP:
        return new TR::ExExp(new T::BinopExp(T::PLUS_OP, left->UnEx(), right->UnEx()));
      case A::MINUS_OP:
        return new TR::ExExp(new T::BinopExp(T::MINUS_OP, left->UnEx(), right->UnEx()));
      case A::TIMES_OP:
        return new TR::ExExp(new T::BinopExp(T::MUL_OP, left->UnEx(), right->UnEx()));
      case A::DIVIDE_OP:
        return new TR::ExExp(new T::BinopExp(T::DIV_OP, left->UnEx(), right->UnEx()));
      default:
        assert(0);
    }
    assert(0);
    return nullptr;
  }

  TR::Exp* Conditional(A::Oper op, TR::Exp* left, TR::Exp* right, bool isString) {
    // P155
    T::CjumpStm* stm = nullptr;
    T::Exp* l = nullptr;
    T::Exp* r = nullptr;
    if (isString) {
      l = F::externalCall(stringEqual, new T::ExpList(left->UnEx(), new T::ExpList(right->UnEx(), nullptr)));
      r = new T::ConstExp(1);
    }
    else {
      l = left->UnEx();
      r = right->UnEx();
    }
    switch (op) {
      case A::EQ_OP:
        stm = new T::CjumpStm(T::EQ_OP, l, r, nullptr, nullptr);
        break;
      case A::NEQ_OP:
        stm = new T::CjumpStm(T::NE_OP, l, r, nullptr, nullptr);
        break;
      case A::LT_OP:
        stm = new T::CjumpStm(T::LT_OP, l, r, nullptr, nullptr);
        break;
      case A::LE_OP:
        stm = new T::CjumpStm(T::LE_OP, l, r, nullptr, nullptr);
        break;
      case A::GT_OP:
        stm = new T::CjumpStm(T::GT_OP, l, r, nullptr, nullptr);
        break;
      case A::GE_OP:
        stm = new T::CjumpStm(T::GE_OP, l, r, nullptr, nullptr);
        break;
      default:
        assert(0);
    }
    TR::PatchList* trues = new TR::PatchList(&stm->true_label, nullptr);
    TR::PatchList* falses = new TR::PatchList(&stm->false_label, nullptr);
    return new TR::CxExp(trues, falses, stm);
  }
}
