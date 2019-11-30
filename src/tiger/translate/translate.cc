#include "tiger/translate/translate.h"

#include <cstdio>
#include <set>
#include <string>
#include <iostream>

#include "tiger/errormsg/errormsg.h"
#include "tiger/frame/temp.h"
#include "tiger/semant/semant.h"
#include "tiger/semant/types.h"
#include "tiger/util/util.h"

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
  // TODO: Put your codes here (lab5).
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
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy FieldVar::Translate(S::Table<E::EnvEntry> *venv,
                                 S::Table<TY::Ty> *tenv, TR::Level *level,
                                 TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy SubscriptVar::Translate(S::Table<E::EnvEntry> *venv,
                                     S::Table<TY::Ty> *tenv, TR::Level *level,
                                     TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy VarExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy NilExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy IntExp::Translate(S::Table<E::EnvEntry> *venv,
                               S::Table<TY::Ty> *tenv, TR::Level *level,
                               TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy StringExp::Translate(S::Table<E::EnvEntry> *venv,
                                  S::Table<TY::Ty> *tenv, TR::Level *level,
                                  TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy CallExp::Translate(S::Table<E::EnvEntry> *venv,
                                S::Table<TY::Ty> *tenv, TR::Level *level,
                                TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy OpExp::Translate(S::Table<E::EnvEntry> *venv,
                              S::Table<TY::Ty> *tenv, TR::Level *level,
                              TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
}

TR::ExpAndTy RecordExp::Translate(S::Table<E::EnvEntry> *venv,
                                  S::Table<TY::Ty> *tenv, TR::Level *level,
                                  TEMP::Label *label) const {
  // TODO: Put your codes here (lab5).
  return TR::ExpAndTy(nullptr, TY::VoidTy::Instance());
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
