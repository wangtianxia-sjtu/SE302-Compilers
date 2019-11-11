#include "tiger/semant/semant.h"
#include "tiger/errormsg/errormsg.h"
#include <set>

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
  if (ty == nullptr) {
    errormsg.Error(fields->head->pos, "undefined type %s",
                   fields->head->typ->Name().c_str());
  }
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
    return TY::IntTy::Instance();
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
  varType = varType->ActualTy();
  if (varType->kind != TY::Ty::Kind::RECORD) {
    errormsg.Error(pos, "not a record type");
    return TY::VoidTy::Instance();
  }
  TY::RecordTy *recordTy = static_cast<TY::RecordTy *>(varType);
  TY::FieldList *fieldList = recordTy->fields;
  while (fieldList) {
    if (!fieldList->head->name->Name().compare(sym->Name())) {
      return fieldList->head->ty;
    }
    fieldList = fieldList->tail;
  }
  errormsg.Error(pos, "field %s doesn't exist", sym->Name().c_str());
  return TY::VoidTy::Instance();
}

TY::Ty *SubscriptVar::SemAnalyze(VEnvType venv, TEnvType tenv,
                                 int labelcount) const {
  TY::Ty *varType = var->SemAnalyze(venv, tenv, labelcount);
  if (varType->ActualTy()->kind != TY::Ty::Kind::ARRAY) {
    errormsg.Error(pos, "array type required");
    return TY::VoidTy::Instance();
  }
  TY::Ty *subscriptType = subscript->SemAnalyze(venv, tenv, labelcount);
  if (!subscriptType->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "integer required");
    return TY::VoidTy::Instance();
  }
  TY::Ty *elementType = (static_cast<TY::ArrayTy*>(varType->ActualTy()))->ty;
  return elementType;
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
      errormsg.Error(pos, "para type mismatch");
    }
    formals = formals->tail;
    argList = argList->tail;
  }
  if (formals) {
    errormsg.Error(pos, "missing parameters in function %s", func->Name().c_str());
  }
  if (argList) {
    errormsg.Error(pos, "too many params in function %s", func->Name().c_str());
  }
  TY::Ty *result = funEntry->result;
  if (result)
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
      errormsg.Error(pos, "integer required");
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
      record_type = record_type->ActualTy();
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
    errormsg.Error(pos, "unmatched assign exp");
  }
  if (var->kind == A::Var::SIMPLE) {
    A::SimpleVar *simpleVar = static_cast<A::SimpleVar*>(var);
    E::VarEntry *varEntry = static_cast<E::VarEntry*>(venv->Look(simpleVar->sym));
    if (varEntry->readonly) {
      errormsg.Error(pos, "loop variable can't be assigned");
    }
  }
  return TY::VoidTy::Instance();
}

TY::Ty *IfExp::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  TY::Ty *testTy = test->SemAnalyze(venv, tenv, labelcount);
  if (!testTy || !testTy->IsSameType(TY::IntTy::Instance())) {
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
      errormsg.Error(pos, "if-then exp's body must produce no value");
    }
    return TY::VoidTy::Instance();
  }
  else {
    // if ... then ... else ...
    TY::Ty *elseTy = elsee->SemAnalyze(venv, tenv, labelcount);
    if (!thenTy->IsSameType(elseTy)) {
      errormsg.Error(pos, "then exp and else exp type mismatch");
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
  TY::Ty *bodyTy = body->SemAnalyze(venv, tenv, labelcount);
  if (!bodyTy->IsSameType(TY::VoidTy::Instance())) {
    errormsg.Error(pos, "while body must produce no value");
  }
  return TY::VoidTy::Instance();
}

TY::Ty *ForExp::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  // TODO: Put your codes here (lab4).
  TY::Ty *loTy = lo->SemAnalyze(venv, tenv, labelcount);
  TY::Ty *hiTy = hi->SemAnalyze(venv, tenv, labelcount);
  if (!loTy || !loTy->IsSameType(TY::IntTy::Instance()) || !hiTy || !hiTy->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "for exp's range type is not integer");
  }
  venv->BeginScope();
  venv->Enter(var, new E::VarEntry(TY::IntTy::Instance(), true));
  TY::Ty *bodyTy = body->SemAnalyze(venv, tenv, labelcount);
  if (!body || !bodyTy->IsSameType(TY::VoidTy::Instance())) {
    errormsg.Error(pos, "for body is not no value");
  }
  venv->EndScope();
  return TY::VoidTy::Instance();
}

TY::Ty *BreakExp::SemAnalyze(VEnvType venv, TEnvType tenv,
                             int labelcount) const {
  // TODO: Put your codes here (lab4).
  return TY::VoidTy::Instance();
}

TY::Ty *LetExp::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  // TODO: Put your codes here (lab4).
  A::DecList *this_dec_head = decs;
  venv->BeginScope();
  tenv->BeginScope();
  while (this_dec_head) {
    A::Dec* this_dec = this_dec_head->head;
    this_dec->SemAnalyze(venv, tenv, labelcount);
    this_dec_head = this_dec_head->tail;
  }
  TY::Ty *result = body->SemAnalyze(venv, tenv, labelcount);
  venv->BeginScope();
  tenv->BeginScope();
  return result;
}

TY::Ty *ArrayExp::SemAnalyze(VEnvType venv, TEnvType tenv,
                             int labelcount) const {
  // TODO: Put your codes here (lab4).
  TY::Ty *arrTy = tenv->Look(typ);
  if (!arrTy) {
    errormsg.Error(pos, "undefined type %s", typ->Name().c_str());
  }
  if (!arrTy->ActualTy()->kind == TY::Ty::ARRAY) {
    errormsg.Error(pos, "%s not an array type", typ->Name().c_str());
  }
  TY::ArrayTy *actualTy = static_cast<TY::ArrayTy*>(arrTy->ActualTy());
  TY::Ty *sizeTy = size->SemAnalyze(venv, tenv, labelcount);
  if (!sizeTy || !sizeTy->IsSameType(TY::IntTy::Instance())) {
    errormsg.Error(pos, "size must be an integer");
  }
  TY::Ty *initTy = init->SemAnalyze(venv, tenv, labelcount);
  if (!initTy->IsSameType(actualTy->ty)) {
    errormsg.Error(pos, "type mismatch");
  }
  return actualTy;
}

TY::Ty *VoidExp::SemAnalyze(VEnvType venv, TEnvType tenv,
                            int labelcount) const {
  // TODO: Put your codes here (lab4).
  return TY::VoidTy::Instance();
}

void FunctionDec::SemAnalyze(VEnvType venv, TEnvType tenv,
                             int labelcount) const {
  // TODO: Put your codes here (lab4).
  A::FunDecList *fun_head = functions;
  std::set<std::string> names;
  while (fun_head) {
    A::FunDec *this_fun = fun_head->head;
    std::string name = this_fun->name->Name();
    TY::Ty *resultTy = nullptr;
    if (this_fun->result) {
      resultTy = tenv->Look(this_fun->result);
      if (!resultTy) {
        errormsg.Error(pos, "undefined type %s", this_fun->result->Name().c_str());
      }
    }
    if (!resultTy) {
      resultTy = TY::VoidTy::Instance();
    }
    if (names.find(name) != names.end()) {
      errormsg.Error(pos, "two functions have the same name");
    }
    else {
      names.insert(name);
      TY::TyList *formals = make_formal_tylist(tenv, this_fun->params);
      E::FunEntry *funEntry = new E::FunEntry(formals, resultTy);
      venv->Enter(this_fun->name, funEntry);
    }
    fun_head = fun_head->tail;
  }
  fun_head = functions;
  while (fun_head) {
    A::FunDec *this_fun = fun_head->head;
    A::FieldList *this_field_list = this_fun->params;
    venv->BeginScope();
    while (this_field_list) {
      venv->Enter(this_field_list->head->name, new E::VarEntry(tenv->Look(this_field_list->head->typ)));
      this_field_list = this_field_list->tail;
    }
    TY::Ty *returnTy = this_fun->body->SemAnalyze(venv, tenv, labelcount);
    E::FunEntry *funEntry = static_cast<E::FunEntry*>(venv->Look(this_fun->name));
    if (!returnTy->IsSameType(funEntry->result)) {
      if (funEntry->result->IsSameType(TY::VoidTy::Instance())) {
        errormsg.Error(pos, "procedure returns value");
      }
      else {
        errormsg.Error(pos, "return value mismatch");
      }
    }
    venv->EndScope();
    fun_head = fun_head->tail;
  }
}

void VarDec::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  // TODO: Put your codes here (lab4).
  TY::Ty *initTy = init->SemAnalyze(venv, tenv, labelcount);
  if (typ) {
    TY::Ty *type = tenv->Look(typ);
    if (!type) {
      errormsg.Error(pos, "undefined type %s", typ->Name().c_str());
    }
    else {
      if (!initTy->IsSameType(type)) {
        errormsg.Error(pos, "type mismatch");
      }
    }
    venv->Enter(var, new E::VarEntry(type));
  }
  else {
    if (initTy->ActualTy()->kind == TY::Ty::Kind::NIL) {
      errormsg.Error(pos, "init should not be nil without type specified");
      venv->Enter(var, new E::VarEntry(TY::IntTy::Instance()));
    }
    else {
      venv->Enter(var, new E::VarEntry(initTy));
    }
  }
}

void TypeDec::SemAnalyze(VEnvType venv, TEnvType tenv, int labelcount) const {
  // TODO: Put your codes here (lab4).
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
    nameTy->ty = this_type_head->ty->SemAnalyze(tenv);
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
}

TY::Ty *NameTy::SemAnalyze(TEnvType tenv) const {
  // TODO: Put your codes here (lab4).
  TY::Ty *ty = tenv->Look(name);
  if (!ty) {
    errormsg.Error(pos, "undefined type %s", name->Name().c_str());
    // return TY::IntTy::Instance();
  }
  return ty;
}

TY::Ty *RecordTy::SemAnalyze(TEnvType tenv) const {
  // TODO: Put your codes here (lab4).
  TY::FieldList *fieldList = make_fieldlist(tenv, record);
  return new TY::RecordTy(fieldList);
}

TY::Ty *ArrayTy::SemAnalyze(TEnvType tenv) const {
  // TODO: Put your codes here (lab4).
  TY::Ty *arrayTy = tenv->Look(array);
  if (!arrayTy) {
    errormsg.Error(pos, "undefined type %s", array->Name().c_str());
  }
  return new TY::ArrayTy(arrayTy);
}

}  // namespace A

namespace SEM {
void SemAnalyze(A::Exp *root) {
  if (root) root->SemAnalyze(E::BaseVEnv(), E::BaseTEnv(), 0);
}

}  // namespace SEM
