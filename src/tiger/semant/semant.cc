#include "tiger/semant/semant.h"
#include "tiger/errormsg/errormsg.h"

extern EM::ErrorMsg errormsg;

using VEnvType = S::Table<E::EnvEntry> *;
using TEnvType = S::Table<TY::Ty> *;

namespace {
static TY::TyList *make_formal_tylist(TEnvType tenv, A::FieldList *params) {
  if (params == nullptr) {
    return nullptr;
  }

  TY::Ty *ty = tenv->Look(params->head->typ);
  if (ty == nullptr) {
    errormsg.Error(params->head->pos, "undefined type %s",
                   params->head->typ->Name().c_str());
  }

  return new TY::TyList(ty->ActualTy(), make_formal_tylist(tenv, params->tail));
}

static TY::FieldList *make_fieldlist(TEnvType tenv, A::FieldList *fields) {
  if (fields == nullptr) {
    return nullptr;
  }

  TY::Ty *ty = tenv->Look(fields->head->typ);
  return new TY::FieldList(new TY::Field(fields->head->name, ty),
                           make_fieldlist(tenv, fields->tail));
}

}  // namespace

namespace A {

TY::Ty *SimpleVar::SemAnalyze(VEnvType venv, TEnvType tenv,
                              int labelcount) const {
  E::EnvEntry *envEntry = venv->Look(sym);
  if (!envEntry) {
    errormsg.Error(pos, "undefined variable %s", sym->Name().c_str());
    return TY::VoidTy::Instance();
  }
  if (envEntry->kind == E::EnvEntry::FUN) {
    errormsg.Error(pos, "function variable %s is not a simple value", sym->Name().c_str());
    return TY::VoidTy::Instance();
  }
  return static_cast<E::VarEntry*>(envEntry)->ty;
}

TY::Ty *FieldVar::SemAnalyze(VEnvType venv, TEnvType tenv,
                             int labelcount) const {
  TY::Ty *varType = var->SemAnalyze(venv, tenv, labelcount);
  if (varType->ActualTy()->kind != TY::Ty::Kind::RECORD) {
    errormsg.Error(pos, "not a record variable");
    return TY::VoidTy::Instance();
  }
  TY::RecordTy *recordTy = static_cast<TY::RecordTy *>(varType);
  TY::FieldList *fieldList = recordTy->fields;
  while (fieldList) {
    if (fieldList->head->name->Name() == sym->Name()) {
      return fieldList->head->ty;
    }
    fieldList = fieldList->tail;
  }
  errormsg.Error(pos, "unknown field %s", sym->Name().c_str());
  return TY::VoidTy::Instance();
}

TY::Ty *SubscriptVar::SemAnalyze(VEnvType venv, TEnvType tenv,
                                 int labelcount) const {
  TY::Ty *varType = var->SemAnalyze(venv, tenv, labelcount);
  if (varType->ActualTy()->kind != TY::Ty::Kind::ARRAY) {
    errormsg.Error(pos, "not an array variable");
    return TY::VoidTy::Instance();
  }
  TY::Ty *subscriptType = subscript->SemAnalyze(venv, tenv, labelcount);
  if (!subscriptType->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "integer required");
    return TY::VoidTy::Instance();
  }
  return varType->ActualTy();
}

TY::Ty *VarExp::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  TY::Ty *varType = var->SemAnalyze(venv, tenv, labelcount);
  return varType->ActualTy();
}

TY::Ty *NilExp::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  return TY::NilTy::Instance();
}

TY::Ty *IntExp::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  return TY::IntTy::Instance();
}

TY::Ty *StringExp::SemAnalyze(VEnvType venv, TEnvType tenv,
                              int labelcount) const {
  return TY::StringTy::Instance();
}

TY::Ty *CallExp::SemAnalyze(VEnvType venv, TEnvType tenv,
                            int labelcount) const {
  // TODO: Put your codes here (lab4).
  E::EnvEntry *envEntry = venv->Look(func);
  if (!envEntry) {
    errormsg.Error(pos, "undefined function %s", func->Name().c_str());
    return TY::VoidTy::Instance();
  }
  if (envEntry->kind == E::EnvEntry::VAR) {
    errormsg.Error(pos, "%s is not a function", func->Name().c_str());
    return TY::VoidTy::Instance();
  }
  E::FunEntry *funEntry = static_cast<E::FunEntry*>(envEntry);
  TY::TyList *formals = funEntry->formals;
  A::ExpList *argList = args;
  while (formals && argList) {
    TY::Ty *type_head = formals->head;
    A::Exp *arg_head = argList->head;
    TY::Ty *arg_type = arg_head->SemAnalyze(venv, tenv, labelcount);
    if (!type_head->IsSameType(arg_type)) {
      errormsg.Error(pos, "parameter type mismatch");
    }
    formals = formals->tail;
    argList = argList->tail;
  }
  if (formals) {
    errormsg.Error(pos, "missing parameters in function %s", func->Name().c_str());
  }
  if (argList) {
    errormsg.Error(pos, "too many parameters in function %s", func->Name().c_str());
  }
  TY::Ty *result = funEntry->result;
  if (!result)
    return result->ActualTy();
  else
    return TY::VoidTy::Instance();
}

TY::Ty *OpExp::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  // TODO: Put your codes here (lab4).
  if (oper == A::PLUS_OP || oper == A::MINUS_OP || oper == A::TIMES_OP || oper == A::DIVIDE_OP) {
    TY::Ty *left_type = left->SemAnalyze(venv, tenv, labelcount);
    TY::Ty *right_type = right->SemAnalyze(venv, tenv, labelcount);
    if (!left_type->IsSameType(TY::IntTy::Instance()) || !right_type->IsSameType(TY::IntTy::Instance())) {
      errormsg.Error(pos, "integer is required");
      return TY::IntTy::Instance();
    }
  }
  if (oper == A::EQ_OP || oper == A::NEQ_OP || oper == LT_OP || oper == LE_OP || oper == GT_OP || oper == GE_OP) {
    TY::Ty *left_type = left->SemAnalyze(venv, tenv, labelcount);
    TY::Ty *right_type = right->SemAnalyze(venv, tenv, labelcount);
    if (!left_type->IsSameType(right_type)) {
      errormsg.Error(pos, "same type required");
      return TY::IntTy::Instance();
    }
  }
  return TY::IntTy::Instance();
}

TY::Ty *RecordExp::SemAnalyze(VEnvType venv, TEnvType tenv,
                              int labelcount) const {
  // TODO: Put your codes here (lab4).
  TY::Ty *record_type = tenv->Look(typ);
  if (!record_type) {
    errormsg.Error(pos, "undefined type %s", typ->Name().c_str());
    return TY::VoidTy::Instance();
  }
  else {
    if (record_type->ActualTy()->kind != TY::Ty::RECORD) {
      errormsg.Error(pos, "%s is not a record", typ->Name().c_str());
      return TY::VoidTy::Instance();
    }
    else {
      TY::RecordTy *this_record_type = static_cast<TY::RecordTy*>(record_type);
      TY::FieldList *fieldList = this_record_type->fields;
      A::EFieldList *eFieldList = fields;
      while (eFieldList) {
        A::EField *eField = eFieldList->head;
        TY::FieldList *fieldList_local = fieldList;
        while (fieldList_local) {
          if (eField->name->Name() == fieldList_local->head->name->Name())
            break;
          fieldList_local = fieldList_local->tail;
        }
        if (!fieldList_local) {
          errormsg.Error(pos, "unknown field %s", eField->name->Name().c_str());
          eFieldList = eFieldList->tail;
          continue;
        }
        TY::Ty *fieldType = eField->exp->SemAnalyze(venv, tenv, labelcount);
        if (!fieldType->IsSameType(fieldList_local->head->ty)) {
          errormsg.Error(pos, "mismatched field type %s", eField->name->Name().c_str());
          eFieldList = eFieldList->tail;
          continue;
        }
        eFieldList = eFieldList->tail;
      }
    }
  }
  return record_type->ActualTy();
}

TY::Ty *SeqExp::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  // TODO: Put your codes here (lab4).
  A::ExpList *this_seq = seq;
  TY::Ty *result;
  while (this_seq) {
    result = this_seq->head->SemAnalyze(venv, tenv, labelcount);
    if (!this_seq->tail) {
      return result->ActualTy();
    }
    this_seq = this_seq->tail;
  }
  return result->ActualTy();
}

TY::Ty *AssignExp::SemAnalyze(VEnvType venv, TEnvType tenv,
                              int labelcount) const {
  // TODO: Put your codes here (lab4).
  TY::Ty *right_type = exp->SemAnalyze(venv, tenv, labelcount);
  TY::Ty *left_type = var->SemAnalyze(venv, tenv, labelcount);
  if (!left_type->IsSameType(right_type)) {
    errormsg.Error(pos, "unmatched type");
  }
  if (var->kind == A::Var::SIMPLE) {
    A::SimpleVar *simpleVar = static_cast<A::SimpleVar*>(var);
    E::VarEntry *varEntry = static_cast<E::VarEntry*>(venv->Look(simpleVar->sym));
    if (varEntry->readonly) {
      errormsg.Error(pos, "loop variable cannot be assigned");
    }
  }
  return TY::VoidTy::Instance();
}

TY::Ty *IfExp::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  TY::Ty *testTy = test->SemAnalyze(venv, tenv, labelcount);
  if (!testTy || testTy->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "if integer required");
  }
  TY::Ty *thenTy = then->SemAnalyze(venv, tenv, labelcount);
  if (!thenTy) {
    errormsg.Error(pos, "then required");
    return TY::VoidTy::Instance();
  }
  if (!elsee) {
    // if ... then ...
    if (!thenTy->IsSameType(TY::VoidTy::Instance())) {
      errormsg.Error(pos, "then should produce no value");
    }
    return TY::VoidTy::Instance();
  }
  else {
    // if ... then ... else ...
    TY::Ty *elseTy = elsee->SemAnalyze(venv, tenv, labelcount);
    if (!thenTy->IsSameType(elseTy)) {
      errormsg.Error(pos, "then and else type mismatch");
    }
    return thenTy->ActualTy();
  }
}

TY::Ty *WhileExp::SemAnalyze(VEnvType venv, TEnvType tenv,
                             int labelcount) const {
  // TODO: Put your codes here (lab4).
  TY::Ty *testTy = test->SemAnalyze(venv, tenv, labelcount);
  if (!testTy->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "while test should be integer");
  }
  // TY::Ty TODO
  return TY::VoidTy::Instance();
}

TY::Ty *ForExp::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  // TODO: Put your codes here (lab4).
  return TY::VoidTy::Instance();
}

TY::Ty *BreakExp::SemAnalyze(VEnvType venv, TEnvType tenv,
                             int labelcount) const {
  // TODO: Put your codes here (lab4).
  return TY::VoidTy::Instance();
}

TY::Ty *LetExp::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  // TODO: Put your codes here (lab4).
  return TY::VoidTy::Instance();
}

TY::Ty *ArrayExp::SemAnalyze(VEnvType venv, TEnvType tenv,
                             int labelcount) const {
  // TODO: Put your codes here (lab4).
  return TY::VoidTy::Instance();
}

TY::Ty *VoidExp::SemAnalyze(VEnvType venv, TEnvType tenv,
                            int labelcount) const {
  // TODO: Put your codes here (lab4).
  return TY::VoidTy::Instance();
}

void FunctionDec::SemAnalyze(VEnvType venv, TEnvType tenv,
                             int labelcount) const {
  // TODO: Put your codes here (lab4).
}

void VarDec::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  // TODO: Put your codes here (lab4).
}

void TypeDec::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  // TODO: Put your codes here (lab4).
}

TY::Ty *NameTy::SemAnalyze(TEnvType tenv) const {
  // TODO: Put your codes here (lab4).
  return TY::VoidTy::Instance();
}

TY::Ty *RecordTy::SemAnalyze(TEnvType tenv) const {
  // TODO: Put your codes here (lab4).
  return TY::VoidTy::Instance();
}

TY::Ty *ArrayTy::SemAnalyze(TEnvType tenv) const {
  // TODO: Put your codes here (lab4).
  return TY::VoidTy::Instance();
}

}  // namespace A

namespace SEM {
void SemAnalyze(A::Exp *root) {
  if (root) root->SemAnalyze(E::BaseVEnv(), E::BaseTEnv(), 0);
}

}  // namespace SEM
