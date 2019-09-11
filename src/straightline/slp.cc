#include "straightline/slp.h"

namespace A {
int A::CompoundStm::MaxArgs() const {
  return std::max(stm1->MaxArgs(), stm2->MaxArgs());
}

Table *A::CompoundStm::Interp(Table *t) const {
  Table * new_table = t;
  new_table = stm1->Interp(new_table);
  new_table = stm2->Interp(new_table);
  return new_table;
}

int A::AssignStm::MaxArgs() const {
  return exp->MaxArgs();
}

Table *A::AssignStm::Interp(Table *t) const {
  IntAndTable * after_right_side_execution = exp->Interp(t);
  Table * result = after_right_side_execution->t->Update(id, after_right_side_execution->i);
  return result;
}

int A::PrintStm::MaxArgs() const {
  int length_of_this_explist = exps->NumExps();
  int max_length_from_child = exps->MaxArgs();
  return std::max(length_of_this_explist, max_length_from_child);
}

Table *A::PrintStm::Interp(Table *t) const {
  std::vector<int> result;
  ExpList *exps_copy = exps;
  if (exps_copy == nullptr)
    return t;
  while (exps_copy != nullptr) {
    IntAndTable *return_value = exps_copy->InterpCurrExp(t);
    result.push_back(return_value->i);
    t = return_value->t;
    exps_copy = exps_copy->GetNextExpList();
  }
  for (int i : result) {
    std::cout << i << " ";
  }
  std::cout << std::endl;
  return t;
}

int IdExp::MaxArgs() const {
  return 0;
}

IntAndTable *IdExp::Interp(Table *t) const {
  return new IntAndTable(t->Lookup(id), t);
}

int NumExp::MaxArgs() const {
  return 0;
}

IntAndTable *NumExp::Interp(Table *t) const {
  return new IntAndTable(num, t);
}

int OpExp::MaxArgs() const {
  return std::max(left->MaxArgs(), right->MaxArgs());
}

IntAndTable *OpExp::Interp(Table *t) const {
  IntAndTable *left_result = left->Interp(t);
  t = left_result->t;
  int left_int = left_result->i;
  IntAndTable *right_result = right->Interp(t);
  int right_int = right_result->i;
  t = right_result->t;
  switch (oper)
  {
    case PLUS:
      return new IntAndTable(left_int + right_int, t);
      break;
    case MINUS:
      return new IntAndTable(left_int - right_int, t);
      break;
    case TIMES:
      return new IntAndTable(left_int * right_int, t);
      break;
    case DIV:
      return new IntAndTable(left_int / right_int, t);
      break;
    default:
      std::cout << "Unknown Operation Type" << std::endl;
      exit(1);
  }
  return nullptr;
}

int EseqExp::MaxArgs() const {
  return std::max(stm->MaxArgs(), exp->MaxArgs());
}

IntAndTable *EseqExp::Interp(Table *t) const {
  t = stm->Interp(t);
  return exp->Interp(t);
}

int PairExpList::MaxArgs() const {
  return std::max(head->MaxArgs(), tail->MaxArgs());
}
IntAndTable *PairExpList::Interp(Table *t) const {
  std::cout << "THIS FUNCTION SHOULD NEVER BE CALLED" << std::endl;
  exit(1);
  return nullptr;
}
int PairExpList::NumExps() const {
  return 1 + tail->NumExps();
}

ExpList *PairExpList::GetNextExpList() const {
  return tail;
}

IntAndTable *PairExpList::InterpCurrExp(Table *t) const {
  IntAndTable *result = head->Interp(t);
  return result;
}

int LastExpList::MaxArgs() const {
  return last->MaxArgs();
}

IntAndTable *LastExpList::Interp(Table *t) const {
  return last->Interp(t);
}

int LastExpList::NumExps() const {
  return 1;
}

ExpList *LastExpList::GetNextExpList() const {
  return nullptr;
}

IntAndTable *LastExpList::InterpCurrExp(Table *t) const {
  IntAndTable *result = last->Interp(t);
  return result;
}

int Table::Lookup(std::string key) const {
  if (id == key) {
    return value;
  } else if (tail != nullptr) {
    return tail->Lookup(key);
  } else {
    assert(false);
  }
}

Table *Table::Update(std::string key, int value) const {
  return new Table(key, value, this);
}
}  // namespace A
