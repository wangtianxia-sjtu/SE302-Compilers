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

using VEnvType = S::Table<E::EnvEntry> *;
using TEnvType = S::Table<TY::Ty> *;

namespace TR {
  class AccessList;
}

namespace {
  F::FragList* globalFrags = nullptr;
  const std::string stringEqual = "stringEqual";
  const std::string allocRecord = "allocRecord";
  const std::string initArray = "initArray";

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
  TR::Exp* Record(const std::vector<TR::Exp *> &recordVector);
  TR::Exp* Seq(TR::Exp* before, TR::Exp* newExp);
  TR::Exp* Assign(TR::Exp* left, TR::Exp* right);
  TR::Exp* If(TR::Exp* test, TR::Exp* then, TR::Exp* elsee);
  TR::Exp* While(TR::Exp* test, TR::Exp* body, TEMP::Label* done);
  TR::Exp* For(TR::Access* loopVarAccess, TR::Exp* lo, TR::Exp* hi, TR::Exp* body, TR::Level* level, TEMP::Label* done);
  TR::Exp* Let(const std::vector<TR::Exp *> &decsVector, TR::Exp* body);
  TR::Exp* Array(TR::Exp* init, TR::Exp* size);
  TR::Exp* EmptyExp();
  TR::Exp* VarDecInit(TR::Access* access, TR::Exp* exp);

  void procEntryExit(TR::Level* level, TR::Exp* body, TR::AccessList* formals);

  static TY::TyList *make_formal_tylist(TEnvType tenv, A::FieldList *params) {
  if (params == nullptr) {
    return nullptr;
  }

  TY::Ty *ty = tenv->Look(params->head->typ);
  if (ty == nullptr) {
    errormsg.Error(params->head->pos, "undefined type %s",
                   params->head->typ->Name().c_str());
  }

  if (ty)
    return new TY::TyList(ty->ActualTy(), make_formal_tylist(tenv, params->tail));
  else
    return new TY::TyList(TY::VoidTy::Instance(), make_formal_tylist(tenv, params->tail));
}

static TY::FieldList *make_fieldlist(TEnvType tenv, A::FieldList *fields) {
  if (fields == nullptr) {
    return nullptr;
  }

  TY::Ty *ty = tenv->Look(fields->head->typ);
  if (ty == nullptr) {
    errormsg.Error(fields->head->pos, "undefined type %s",
                   fields->head->typ->Name().c_str());
  }
  return new TY::FieldList(new TY::Field(fields->head->name, ty),
                           make_fieldlist(tenv, fields->tail));
}

static U::BoolList* make_boollist(A::FieldList* param) {
  if (param == nullptr) {
    return nullptr;
  }
  return new U::BoolList(param->head->escape, make_boollist(param->tail));
}
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

  static AccessList *Formals(Level *level);

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

  lv = new Level(F::NewX64Frame(TEMP::NamedLabel("tigermain"), new U::BoolList(true, nullptr)), nullptr);
  return lv;
}

F::FragList *TranslateProgram(A::Exp *root) {
  // Needs Tr_procEntryExit and Tr_getResult here P173
  Level* mainFrame = Outermost();
  TEMP::Label* mainLabel = TEMP::NewLabel();
  ExpAndTy result = root->Translate(E::BaseVEnv(), E::BaseTEnv(), mainFrame, mainLabel);
  procEntryExit(mainFrame, result.exp, nullptr);
  return globalFrags;
}

 AccessList* Level::Formals(Level *level) {
    F::AccessList* f_accessList = level->frame->GetFormalList();
    AccessList* result = nullptr;
    f_accessList = f_accessList->tail; // Skip static link here, translate.cc knows nothing about static link
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
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  if (envEntry->kind == E::EnvEntry::FUN) {
    errormsg.Error(pos, "function variable %s is not a simple value", sym->Name().c_str());
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
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
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
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
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
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
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  varType = varType->ActualTy();
  TY::ArrayTy* arrayTy = static_cast<TY::ArrayTy *>(varType);
  TY::Ty* elementType = arrayTy->ty;
  TR::ExpAndTy subscriptResult = subscript->Translate(venv, tenv, level, label);
  TY::Ty* subscriptType = subscriptResult.ty;
  TR::Exp* subscriptExp = subscriptResult.exp;
  if (!subscriptType || !subscriptType->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "integer required");
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
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
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  if (envEntry->kind == E::EnvEntry::VAR) {
    errormsg.Error(pos, "%s is not a function", func->Name().c_str());
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  E::FunEntry* funEntry = static_cast<E::FunEntry *>(envEntry);
  TY::TyList* formals = funEntry->formals;
  A::ExpList* argList = args;
  while (formals && argList) {
    TY::Ty* formalsHead = formals->head;
    A::Exp* expHead = argList->head;
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
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  if (argList) {
    errormsg.Error(pos, "too many params in function %s", func->Name().c_str());
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  if (!valid) {
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }

  /* ----------------------------------------------------------------------- */

  T::Exp* staticLink = new T::TempExp(F::FP());
  while (level != funEntry->level->parent) {
    staticLink = new T::MemExp(new T::BinopExp(T::PLUS_OP, staticLink, new T::ConstExp(-F::wordSize)));
    level = level->parent;
  }
  T::ExpList* expList = ToExpList(formalsVector);
  TR::Exp* resultExp = nullptr;
  if (funEntry->level->parent == nullptr) {
    // External call, no static link
    resultExp = new TR::ExExp(F::externalCall(func->Name(), expList)); // External functions will have a null label, see env.cc
  }
  else {
    // Add static link
    expList = new T::ExpList(staticLink, expList);
    resultExp = new TR::ExExp(new T::CallExp(new T::NameExp(func), expList));
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
        return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
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
      return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
    }

    /* ----------------------------------------------------------------------- */

    if (leftResult.ty->IsSameType(TY::StringTy::Instance())) {
      isString = true;
    }
    TR::Exp* resultExp = Conditional(oper, leftExp, rightExp, isString);
    return TR::ExpAndTy(resultExp, TY::IntTy::Instance());
  }
  assert(0);
  return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
}

TR::ExpAndTy RecordExp::Translate(S::Table<E::EnvEntry> *venv,
                                  S::Table<TY::Ty> *tenv, TR::Level *level,
                                  TEMP::Label *label) const {
  // TODO: Here we assume that the order of a A::EFieldList and that of a TY::FieldList are the same
  // P167-168
  std::vector<TR::Exp*> recordFieldsVector;
  TY::Ty *recordType = tenv->Look(typ);
  if (!recordType) {
    errormsg.Error(pos, "undefined type %s", typ->Name().c_str());
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  if (recordType->ActualTy()->kind != TY::Ty::RECORD) {
    errormsg.Error(pos, "%s is not a record", typ->Name().c_str());
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }

  recordType = recordType->ActualTy();
  TY::RecordTy* realRecordType = static_cast<TY::RecordTy *>(recordType);
  A::EFieldList* eFieldList = fields;
  TY::FieldList* fieldList = realRecordType->fields;
  while (eFieldList) {
    if (!fieldList) {
      errormsg.Error(pos, "Too many fields in %s", typ->Name().c_str());
      return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
    }
    A::EField* eFieldHead = eFieldList->head;
    TY::Field* fieldHead = fieldList->head;
    TR::ExpAndTy eFieldHeadResult = eFieldHead->exp->Translate(venv, tenv, level, label);
    recordFieldsVector.push_back(eFieldHeadResult.exp);
    if (!eFieldHeadResult.ty->IsSameType(fieldHead->ty)) {
      errormsg.Error(pos, "mismatched field type %s", eFieldHead->name->Name().c_str());
      return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
    }
    eFieldList = eFieldList->tail;
    fieldList = fieldList->tail;
  }
  if (fieldList) {
    errormsg.Error(pos, "Too few fields in %s", typ->Name().c_str());
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }

  /* ----------------------------------------------------------------------- */

  TR::Exp* exp = Record(recordFieldsVector);
  return TR::ExpAndTy(exp, recordType);
} 

TR::ExpAndTy SeqExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  A::ExpList* head = seq;
  TR::Exp* resultExp = nullptr;
  TY::Ty* resultTy = nullptr;
  if (!head) {
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  while (head) {
    A::Exp* expHead = head->head;
    TR::ExpAndTy expHeadResult = expHead->Translate(venv, tenv, level, label);
    resultTy = expHeadResult.ty;
    resultExp = Seq(resultExp, expHeadResult.exp);
    head = head->tail;
  }
  return TR::ExpAndTy(resultExp, resultTy);
}

TR::ExpAndTy AssignExp::Translate(S::Table<E::EnvEntry> *venv,
                                  S::Table<TY::Ty> *tenv, TR::Level *level,
                                  TEMP::Label *label) const {
  bool valid = true;
  TR::ExpAndTy expResult = exp->Translate(venv, tenv, level, label);
  TR::ExpAndTy varResult = var->Translate(venv, tenv, level, label);
  TR::Exp* expExp = expResult.exp;
  TY::Ty* expType = expResult.ty;
  TR::Exp* varExp = varResult.exp;
  TY::Ty* varType = varResult.ty;
  if (!expType || !varType || !expType->IsSameType(varType)) {
    errormsg.Error(pos, "unmatched assign exp");
    valid = false;
  }
  if (var->kind == A::Var::SIMPLE) {
    A::SimpleVar* simpleVar = static_cast<A::SimpleVar *>(var);
    E::VarEntry* varEntry = static_cast<E::VarEntry *>(venv->Look(simpleVar->sym));
    if (varEntry->readonly) {
      errormsg.Error(pos, "loop variable can't be assigned");
      valid = false;
    }
  }
  if (!valid) {
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }

  TR::Exp* resultExp = Assign(varExp, expExp);
  return TR::ExpAndTy(resultExp, TY::VoidTy::Instance());
}

TR::ExpAndTy IfExp::Translate(S::Table<E::EnvEntry> *venv,
                              S::Table<TY::Ty> *tenv, TR::Level *level,
                              TEMP::Label *label) const {
  TR::ExpAndTy testResult = test->Translate(venv, tenv, level, label);
  TR::Exp* testExp = testResult.exp;
  TY::Ty* testType = testResult.ty;
  if (!testType || !testType->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "if integer required");
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }

  TR::ExpAndTy thenResult = then->Translate(venv, tenv, level, label);
  TR::Exp* thenExp = thenResult.exp;
  TY::Ty* thenType = thenResult.ty;
  if (!elsee) {
    // if ... then ...
    if (!thenType || !thenType->IsSameType(TY::VoidTy::Instance())) {
      errormsg.Error(pos, "if-then exp's body must produce no value");
      return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
    }
    TR::Exp* resultExp = If(testExp, thenExp, nullptr);
    return TR::ExpAndTy(resultExp, TY::VoidTy::Instance());
  }
  else {
    // if ... then ... else ...
    TR::ExpAndTy elseResult = elsee->Translate(venv, tenv, level, label);
    TR::Exp* elseExp = elseResult.exp;
    TY::Ty* elseType = elseResult.ty;
    if (!elseType || !thenType->IsSameType(elseType)) {
      errormsg.Error(pos, "then exp and else exp type mismatch");
      return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
    }
    TR::Exp* resultExp = If(testExp, thenExp, elseExp);
    return TR::ExpAndTy(resultExp, thenType->ActualTy());
  }
  assert(0);
  return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
}

TR::ExpAndTy WhileExp::Translate(S::Table<E::EnvEntry> *venv,
                                 S::Table<TY::Ty> *tenv, TR::Level *level,
                                 TEMP::Label *label) const {
  TEMP::Label* done = TEMP::NewLabel();
  TR::ExpAndTy testResult = test->Translate(venv, tenv, level, label);
  TR::Exp* testExp = testResult.exp;
  TY::Ty* testType = testResult.ty;
  TR::ExpAndTy bodyResult = body->Translate(venv, tenv, level, done); // Pass label "done" as a parameter
  TR::Exp* bodyExp = bodyResult.exp;
  TY::Ty* bodyType = bodyResult.ty;
  if (!testType->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "while test should be integer");
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  if (!bodyType->IsSameType(TY::VoidTy::Instance())) {
    errormsg.Error(pos, "while body must produce no value");
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }

  TR::Exp* resultExp = While(testExp, bodyExp, done);
  return TR::ExpAndTy(resultExp, TY::VoidTy::Instance());
}

TR::ExpAndTy ForExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  bool valid = true;
  TR::ExpAndTy loResult = lo->Translate(venv, tenv, level, label);
  TR::ExpAndTy hiResult = hi->Translate(venv, tenv, level, label);
  TEMP::Label* done = TEMP::NewLabel();
  if (!loResult.ty || !loResult.ty->IsSameType(TY::IntTy::Instance()) || !hiResult.ty || !hiResult.ty->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "for exp's range type is not integer");
    valid = false;
  }
  TR::Access* loopVarAccess = TR::Access::AllocLocal(level, escape);
  venv->BeginScope();
  venv->Enter(var, new E::VarEntry(loopVarAccess, TY::IntTy::Instance(), true));
  TR::ExpAndTy bodyResult = body->Translate(venv, tenv, level, done); // Pass label "done" as a parameter
  TR::Exp* result = For(loopVarAccess, loResult.exp, hiResult.exp, bodyResult.exp, level, done);
  if (!bodyResult.ty || !bodyResult.ty->IsSameType(TY::VoidTy::Instance())) {
    errormsg.Error(pos, "for body is not no value");
    valid = false;
  }
  venv->EndScope();
  if (!valid) {
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  return TR::ExpAndTy(result, TY::VoidTy::Instance());
}

TR::ExpAndTy BreakExp::Translate(S::Table<E::EnvEntry> *venv,
                                 S::Table<TY::Ty> *tenv, TR::Level *level,
                                 TEMP::Label *label) const {
  // Simply jump to done (label)
  // TODO: Detect out-of-loop breaks
  T::Stm* stm = new T::JumpStm(new T::NameExp(label), new TEMP::LabelList(label, nullptr));
  return TR::ExpAndTy(new TR::NxExp(stm), TY::VoidTy::Instance());
}

TR::ExpAndTy LetExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  A::DecList* decHead = decs;
  std::vector<TR::Exp *> decResults;
  venv->BeginScope();
  tenv->BeginScope();
  while (decHead) {
    A::Dec* dec = decHead->head;
    TR::Exp* decResult = dec->Translate(venv, tenv, level, label);
    decResults.push_back(decResult);
    decHead = decHead->tail;
  }
  TR::ExpAndTy result = body->Translate(venv, tenv, level, label);
  TR::Exp* resultExp = Let(decResults, result.exp);
  venv->EndScope();
  tenv->EndScope();
  return TR::ExpAndTy(resultExp, result.ty);
}

TR::ExpAndTy ArrayExp::Translate(S::Table<E::EnvEntry> *venv,
                                 S::Table<TY::Ty> *tenv, TR::Level *level,
                                 TEMP::Label *label) const {
  TY::Ty* arrayType = tenv->Look(typ);
  if (!arrayType) {
    errormsg.Error(pos, "undefined type %s", typ->Name().c_str());
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  if (!arrayType->ActualTy()->kind == TY::Ty::ARRAY) {
    errormsg.Error(pos, "%s not an array type", typ->Name().c_str());
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  TY::ArrayTy* actualTy = static_cast<TY::ArrayTy*>(arrayType->ActualTy());
  TR::ExpAndTy sizeResult = size->Translate(venv, tenv, level, label);
  if (!sizeResult.ty || !sizeResult.ty->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "size must be an integer");
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  TR::ExpAndTy initResult = init->Translate(venv, tenv, level, label);
  if (!initResult.ty || !initResult.ty->IsSameType(actualTy->ty)) {
    errormsg.Error(pos, "type mismatch");
    return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
  }
  TR::Exp* initExp = Array(initResult.exp, sizeResult.exp);
  return TR::ExpAndTy(initExp, actualTy);
}

TR::ExpAndTy VoidExp::Translate(S::Table<E::EnvEntry> *venv,
                                S::Table<TY::Ty> *tenv, TR::Level *level,
                                TEMP::Label *label) const {
  return TR::ExpAndTy(EmptyExp(), TY::VoidTy::Instance());
}

TR::Exp *FunctionDec::Translate(S::Table<E::EnvEntry> *venv,
                                S::Table<TY::Ty> *tenv, TR::Level *level,
                                TEMP::Label *label) const {
  A::FunDecList* funHead = functions;
  std::set<std::string> names;

  // First walkthrough
  while (funHead) {
    A::FunDec* thisFun = funHead->head;
    std::string name = thisFun->name->Name();
    TY::Ty *resultTy = nullptr;
    if (thisFun->result) {
      resultTy = tenv->Look(thisFun->result);
      if (!resultTy) {
        errormsg.Error(pos, "undefined type %s", thisFun->result->Name().c_str());
      }
    }
    if (!resultTy) {
      resultTy = TY::VoidTy::Instance();
    }
    if (names.find(name) != names.end()) {
      errormsg.Error(pos, "two functions have the same name");
      return new TR::ExExp(new T::ConstExp(0)); // Abort here, do not continue
    }
    else {
      names.insert(name);
      TY::TyList* formals = make_formal_tylist(tenv, thisFun->params);
      U::BoolList* args = make_boollist(thisFun->params);
      TR::Level* newLevel = TR::Level::NewLevel(level, thisFun->name, args);
      E::FunEntry* newFunEntry = new E::FunEntry(newLevel, thisFun->name, formals, resultTy); // Pass thisFun->name as TEMP::Label*? TBD
      venv->Enter(thisFun->name, newFunEntry);
    }
    funHead = funHead->tail;
  }

  // Second walkthrough
  funHead = functions;
  while (funHead) {
    A::FunDec* thisFun = funHead->head;
    A::FieldList* thisFieldList = thisFun->params;
    E::FunEntry* thisFunEntry = static_cast<E::FunEntry *>(venv->Look(thisFun->name));
    TR::AccessList* thisAccessList = TR::Level::Formals(thisFunEntry->level);
    TR::AccessList* thisAccessListHead = thisAccessList;
    TY::TyList* thisTyList = thisFunEntry->formals;
    venv->BeginScope();
    for (; thisFieldList; thisFieldList = thisFieldList->tail, thisAccessList = thisAccessList->tail, thisTyList = thisTyList->tail) {
      assert(thisAccessList);
      assert(thisTyList);
      venv->Enter(thisFieldList->head->name, new E::VarEntry(thisAccessList->head, thisTyList->head));
    }

    // Translate function body
    TR::ExpAndTy thisFunResult = thisFun->body->Translate(venv, tenv, thisFunEntry->level, label); // Pass thisFunEntry->level as parameter
    if (!thisFunResult.ty || !thisFunResult.ty->IsSameType(thisFunEntry->result)) {
      if (thisFunEntry->result->IsSameType(TY::VoidTy::Instance())) {
        errormsg.Error(pos, "procedure returns value");
      }
      else {
        errormsg.Error(pos, "return value mismatch");
      }
    }
    procEntryExit(thisFunEntry->level, thisFunResult.exp, thisAccessListHead);
    venv->EndScope();
    funHead = funHead->tail;
  }
  return EmptyExp();
}

TR::Exp *VarDec::Translate(S::Table<E::EnvEntry> *venv, S::Table<TY::Ty> *tenv,
                           TR::Level *level, TEMP::Label *label) const {
  TR::ExpAndTy initResult = init->Translate(venv, tenv, level, label);
  TR::Access* access = TR::Access::AllocLocal(level, true);
  if (typ) {
    TY::Ty* type = tenv->Look(typ);
    if (!type) {
      errormsg.Error(pos, "undefined type %s", typ->Name().c_str());
      type = TY::VoidTy::Instance();
    }
    else {
      if (!initResult.ty || !initResult.ty->IsSameType(type)) {
        errormsg.Error(pos, "type mismatch");
      }
    }
    venv->Enter(var, new E::VarEntry(access, type));
  }
  else {
    if (initResult.ty->ActualTy()->kind == TY::Ty::Kind::NIL) {
      errormsg.Error(pos, "init should not be nil without type specified");
      venv->Enter(var, new E::VarEntry(access, TY::IntTy::Instance()));
    }
    else {
      venv->Enter(var, new E::VarEntry(access, initResult.ty));
    }
  }
  return VarDecInit(access, initResult.exp);
}

TR::Exp *TypeDec::Translate(S::Table<E::EnvEntry> *venv, S::Table<TY::Ty> *tenv,
                            TR::Level *level, TEMP::Label *label) const {
  // Copied from lab4
  A::NameAndTyList *this_type = types;
  std::set<std::string> type_string_set;

  while (this_type) {
    A::NameAndTy *this_type_head = this_type->head;
    if (type_string_set.find(this_type_head->name->Name()) != type_string_set.end()) {
      errormsg.Error(pos, "two types have the same name");
    }
    else {
      type_string_set.insert(this_type_head->name->Name());
      tenv->Enter(this_type_head->name, new TY::NameTy(this_type_head->name, nullptr));
    }
    this_type = this_type->tail;
  }

  this_type = types;
  while (this_type) {
    A::NameAndTy *this_type_head = this_type->head;
    TY::Ty *ty = tenv->Look(this_type_head->name);
    TY::NameTy *nameTy = static_cast<TY::NameTy*>(ty);
    nameTy->ty = this_type_head->ty->Translate(tenv);
    this_type = this_type->tail;
  }

  this_type = types;
  while (this_type) {
    A::NameAndTy *this_type_head = this_type->head;
    TY::Ty *ty = tenv->Look(this_type_head->name);
    if (!ty->ActualTy()) {
      errormsg.Error(pos, "illegal type cycle");
      break;
    }
    this_type = this_type->tail;
  }

  return EmptyExp();
}

TY::Ty *NameTy::Translate(S::Table<TY::Ty> *tenv) const {
  // Copied from lab4
  TY::Ty *ty = tenv->Look(name);
  if (!ty) {
    errormsg.Error(pos, "undefined type %s", name->Name().c_str());
    return TY::VoidTy::Instance();
  }
  return ty;
}

TY::Ty *RecordTy::Translate(S::Table<TY::Ty> *tenv) const {
  // Copied from lab4
  TY::FieldList *fieldList = make_fieldlist(tenv, record);
  return new TY::RecordTy(fieldList);
}

TY::Ty *ArrayTy::Translate(S::Table<TY::Ty> *tenv) const {
  // Copied from lab4
  TY::Ty *arrayTy = tenv->Look(array);
  if (!arrayTy) {
    errormsg.Error(pos, "undefined type %s", array->Name().c_str());
    arrayTy = TY::IntTy::Instance();
  }
  return new TY::ArrayTy(arrayTy);
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
      result = new T::ExpList(new T::ConstExp(0), nullptr);
    tail = result;
    for (std::size_t i = 1; i < s; ++i) {
      if (formalsVector[i])
        tail->tail = new T::ExpList(formalsVector[i]->UnEx(), nullptr);
      else
        tail->tail = new T::ExpList(new T::ConstExp(0), nullptr);
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

  TR::Exp* Record(const std::vector<TR::Exp *> &recordVector) {
    // TODO: Eliminate NewTemp here
    // P168
    std::size_t s = recordVector.size();
    if (s == 0)
      return EmptyExp();
    TEMP::Temp* r = TEMP::Temp::NewTemp();
    T::Exp* initRecord = F::externalCall(allocRecord, new T::ExpList(new T::ConstExp(s * F::wordSize), nullptr));
    T::Stm* stm = new T::MoveStm(new T::TempExp(r), initRecord);
    for (std::size_t i = 0; i < s; ++i) {
      T::Exp* exp = recordVector[i]->UnEx();
      stm = new T::SeqStm(stm, 
        new T::MoveStm(new T::MemExp(new T::BinopExp(T::PLUS_OP, new T::TempExp(r), new T::ConstExp(i * F::wordSize))),
          exp));
    }
    return new TR::ExExp(new T::EseqExp(stm, new T::TempExp(r)));
  }

  TR::Exp* EmptyExp() {
    return new TR::ExExp(new T::ConstExp(0));
  }

  TR::Exp* Seq(TR::Exp* before, TR::Exp* newExp) {
    if (!before) {
      return new TR::ExExp(new T::EseqExp(new T::ExpStm(new T::ConstExp(0)), newExp->UnEx()));
    }
    return new TR::ExExp(new T::EseqExp(before->UnNx(), newExp->UnEx()));
  }

  TR::Exp* If(TR::Exp* test, TR::Exp* then, TR::Exp* elsee) {
    // TODO: Eliminate NewTemp here
    TR::Cx testCx = test->UnCx();
    TEMP::Label* t = TEMP::NewLabel();
    TEMP::Label* f = TEMP::NewLabel();
    TR::do_patch(testCx.trues, t);
    TR::do_patch(testCx.falses, f);
    if (elsee) {
      // TODO: Nx will be treated as a T::EseqExp(..., T::ConstExp(0)), see UnEx()
      TEMP::Label* join = TEMP::NewLabel();
      TEMP::Temp* r = TEMP::Temp::NewTemp();
      T::Exp* e = 
      new T::EseqExp(testCx.stm, 
        new T::EseqExp(new T::LabelStm(t), 
          new T::EseqExp(new T::MoveStm(new T::TempExp(r), then->UnEx()),
            new T::EseqExp(new T::JumpStm(new T::NameExp(join), new TEMP::LabelList(join, nullptr)),
              new T::EseqExp(new T::LabelStm(f), 
                new T::EseqExp(new T::MoveStm(new T::TempExp(r), elsee->UnEx()),
                  new T::EseqExp(new T::JumpStm(new T::NameExp(join), new TEMP::LabelList(join, nullptr)),
                    new T::EseqExp(new T::LabelStm(join),
                      new T::TempExp(r)))))))));
      return new TR::ExExp(e);
    }
    else {
      // translated as a statement
      T::Stm* s =
      new T::SeqStm(testCx.stm,
        new T::SeqStm(new T::LabelStm(t),
          new T::SeqStm(then->UnNx(),
            new T::LabelStm(f))));
      return new TR::NxExp(s);
    }
    assert(0);
    return nullptr;
  }

  TR::Exp* While(TR::Exp* test, TR::Exp* body, TEMP::Label* done) {
    // P169
    TEMP::Label* testLabel = TEMP::NewLabel();
    TEMP::Label* bodyLabel = TEMP::NewLabel();
    TR::Cx testCx = test->UnCx();
    TR::do_patch(testCx.trues, bodyLabel);
    TR::do_patch(testCx.falses, done);
    T::Stm* stm = 
    new T::SeqStm(new T::LabelStm(testLabel),
      new T::SeqStm(testCx.stm,
        new T::SeqStm(new T::LabelStm(bodyLabel),
          new T::SeqStm(body->UnNx(),
            new T::SeqStm(new T::JumpStm(new T::NameExp(testLabel), new TEMP::LabelList(testLabel, nullptr)),
              new T::LabelStm(done))))));
    return new TR::NxExp(stm);
  }

  TR::Exp* For(TR::Access* loopVarAccess, TR::Exp* lo, TR::Exp* hi, TR::Exp* body, TR::Level* level, TEMP::Label* done) {
    
    /*
     * move lo to loopVar
     * move hi to hiTemp
     * if loopVar <= hi goto bodyLabel else goto done
     * bodyLabel:
     * body->UnNx()
     * if loopVar < hi goto addLabel else goto done
     * addLabel:
     * move loopVar+1 to loopVar
     * goto bodyLabel
     * done:
     */

    TEMP::Label* bodyLabel = TEMP::NewLabel();
    TEMP::Label* addLabel = TEMP::NewLabel();
    TEMP::Temp* hiTemp = TEMP::Temp::NewTemp();
    T::Exp* loopVarExp = loopVarAccess->access->ToExp(new T::TempExp(F::FP()));
    T::Stm* stm = 
    new T::SeqStm(new T::MoveStm(loopVarExp, lo->UnEx()),
      new T::SeqStm(new T::MoveStm(new T::TempExp(hiTemp), hi->UnEx()),
        new T::SeqStm(new T::CjumpStm(T::LE_OP, loopVarExp, new T::TempExp(hiTemp), bodyLabel, done),
          new T::SeqStm(new T::LabelStm(bodyLabel),
            new T::SeqStm(body->UnNx(),
              new T::SeqStm(new T::CjumpStm(T::LT_OP, loopVarExp, new T::TempExp(hiTemp), addLabel, done),
                new T::SeqStm(new T::LabelStm(addLabel),
                  new T::SeqStm(new T::MoveStm(loopVarExp, new T::BinopExp(T::PLUS_OP, loopVarExp, new T::ConstExp(1))),
                    new T::SeqStm(new T::JumpStm(new T::NameExp(bodyLabel), new TEMP::LabelList(bodyLabel, nullptr)),
                      new T::LabelStm(done))))))))));
    return new TR::NxExp(stm);
  }
  
  TR::Exp* Assign(TR::Exp* left, TR::Exp* right) {
    return new TR::NxExp(new T::MoveStm(left->UnEx(), right->UnEx()));
  }

  TR::Exp* Let(const std::vector<TR::Exp *> &decsVector, TR::Exp* body) {
    std::size_t s = decsVector.size();
    if (s == 0)
      return body;
    T::Stm* stm = decsVector[0]->UnNx();
    for (std::size_t i = 1; i < s; ++i) {
      stm = new T::SeqStm(stm, decsVector[i]->UnNx());
    }
    return new TR::ExExp(new T::EseqExp(stm, body->UnEx()));
  }

  TR::Exp* Array(TR::Exp* init, TR::Exp* size) {
    T::Exp* initCall = F::externalCall(initArray, new T::ExpList(size->UnEx(), new T::ExpList(init->UnEx(), nullptr)));
    return new TR::ExExp(initCall);
  }

  TR::Exp* VarDecInit(TR::Access* access, TR::Exp* exp) {
    return new TR::NxExp(new T::MoveStm(access->access->ToExp(new T::TempExp(F::FP())), exp->UnEx()));
  }

  void procEntryExit(TR::Level* level, TR::Exp* body, TR::AccessList* formals) {
    // P171-173
    // TODO: Consider procedure call without return value
    T::Stm* stm = new T::MoveStm(new T::TempExp(F::RV()), body->UnEx());
    stm = F::F_procEntryExit1(level->frame, stm);
    F::Frag* funFrag = new F::ProcFrag(stm, level->frame);
    AddToGlobalFrags(funFrag);
  }
}
