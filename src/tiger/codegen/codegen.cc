#include "tiger/codegen/codegen.h"

#include <iostream>

namespace {
  AS::InstrList* iList = nullptr;
  AS::InstrList* last = nullptr;
  std::string fs;

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

  void munchStm(T::Stm* s);
  TEMP::TempList* munchArgs(T::ExpList* args);
  std::string toOpString(T::RelOp op);
  TEMP::Temp* munchExp(T::Exp* e);
  TEMP::TempList* L(TEMP::Temp* h, TEMP::TempList* t);
}

namespace CG {

AS::InstrList* Codegen(F::Frame* f, T::StmList* stmList) {
  AS::InstrList* list;
  T::StmList* sl;

  fs = f->GetName()->Name() + "_framesize"; // An assembly-language constant, see P213
  for (sl = stmList; sl; sl = sl->tail) {
    munchStm(sl->head);
  }

  list = iList;
  iList = last = nullptr;
  fs = "";
  return F::F_procEntryExit2(list);
}

}  // namespace CG

namespace {
  void munchStm(T::Stm* s) {
    switch(s->kind) {
      case T::Stm::Kind::MOVE: {
        T::MoveStm* moveStm = static_cast<T::MoveStm *>(s);
        T::Exp* dst = moveStm->dst;
        T::Exp* src = moveStm->src;
        TEMP::Temp* srcTemp = munchExp(src);

        if (dst->kind == T::Exp::TEMP) {
          T::TempExp* dstTempExp = static_cast<T::TempExp *>(dst);
          emit(new AS::MoveInstr("movq `s0,`d0", 
                new TEMP::TempList(dstTempExp->temp, nullptr), 
                  new TEMP::TempList(srcTemp, nullptr)));
          return;
        }

        if (dst->kind == T::Exp::MEM) {
          T::MemExp* dstMemExp = static_cast<T::MemExp *>(dst);
          TEMP::Temp* dstTemp = munchExp(dstMemExp->exp);
          emit(new AS::MoveInstr("movq `s0, (`s1)", 
                nullptr,
                  new TEMP::TempList(srcTemp, new TEMP::TempList(dstTemp, nullptr))));
          return;
        }

        std::cerr << "Wrong dst->kind: " << dst->kind << std::endl;
        assert(0);
      }
      case T::Stm::Kind::LABEL: {
        T::LabelStm* labelStm = static_cast<T::LabelStm *>(s);
        std::string labelString(labelStm->label->Name());
        emit(new AS::LabelInstr(labelString, labelStm->label));
        return;
      }
      case T::Stm::Kind::JUMP: {
        T::JumpStm* jumpStm = static_cast<T::JumpStm *>(s);
        std::string labelString = "jmp " + jumpStm->exp->name->Name();
        emit(new AS::OperInstr(labelString, nullptr, nullptr, new AS::Targets(jumpStm->jumps)));
        return;
      }
      case T::Stm::Kind::CJUMP: {
        T::CjumpStm* cjumpStm = static_cast<T::CjumpStm *>(s);
        TEMP::Temp* leftTemp = munchExp(cjumpStm->left);
        TEMP::Temp* rightTemp = munchExp(cjumpStm->right);
        std::string assem;
        std::string opString = toOpString(cjumpStm->op);
        // Every cjump is immediately followed by its false label
        emit(new AS::OperInstr("cmpq `s0,`s1", 
              nullptr, 
                new TEMP::TempList(rightTemp, new TEMP::TempList(leftTemp, nullptr)),
                  new AS::Targets(nullptr)));
        assem = opString + " " + cjumpStm->true_label->Name();
        emit(new AS::OperInstr(assem,
              nullptr,
                nullptr,
                  new AS::Targets(new TEMP::LabelList(cjumpStm->true_label, nullptr))));
        return;
      }
      case T::Stm::Kind::EXP: {
        T::ExpStm* expStm = static_cast<T::ExpStm *>(s);
        munchExp(expStm->exp);
        return;
      }
      default: {
        std::cerr << "T::Stm::Kind unrecognized: " << s->kind << std::endl;
        assert(0);
      }
    }
  }

  TEMP::Temp* munchExp(T::Exp* e) {
    // TODO: Finsh munchExp
    TEMP::Temp* r = TEMP::Temp::NewTemp();
    switch (e->kind) {
      case T::Exp::Kind::BINOP: {
        T::BinopExp* binopExp = static_cast<T::BinopExp *>(e);
        TEMP::Temp* left = munchExp(binopExp->left);
        TEMP::Temp* right = munchExp(binopExp->right);
        switch (binopExp->op) {
          case T::BinOp::PLUS_OP: {
            emit(new AS::MoveInstr("movq `s0,`d0", L(r, nullptr), L(left, nullptr)));
            emit(new AS::OperInstr("addq `s0,`d0", L(r, nullptr), L(right, L(r, nullptr)), new AS::Targets(nullptr)));
            break;
          }
          case T::BinOp::MINUS_OP: {
            emit(new AS::MoveInstr("movq `s0,`d0", L(r, nullptr), L(left, nullptr)));
            emit(new AS::OperInstr("subq `s0,`d0", L(r, nullptr), L(right, L(r, nullptr)), new AS::Targets(nullptr)));
            break;
          }
          case T::BinOp::MUL_OP: {
            emit(new AS::MoveInstr("movq `s0,`d0", L(r, nullptr), L(left, nullptr)));
            emit(new AS::OperInstr("imulq `s0,`d0", L(r, nullptr), L(right, L(r, nullptr)), new AS::Targets(nullptr)));
            break;
          }
          case T::BinOp::DIV_OP: {
            emit(new AS::MoveInstr("movq `s0,`d0", L(F::NUMERATOR(), nullptr), L(left, nullptr)));
            emit(new AS::OperInstr("cqto", L(F::NUMERATOR(), L(F::NUMERATOR_HIGHER_64(), nullptr)), L(F::NUMERATOR(), nullptr), new AS::Targets(nullptr)));
            emit(new AS::OperInstr("idivq `s0", L(F::QUOTIENT(), nullptr), L(right, L(F::NUMERATOR(), L(F::NUMERATOR_HIGHER_64(), nullptr))), new AS::Targets(nullptr)));
            emit(new AS::MoveInstr("movq `s0,`d0", L(r, nullptr), L(F::QUOTIENT(), nullptr)));
            break;
          }
          default:
            std::cerr << "T::BinOp not recognized: " << binopExp->op << std::endl;
            assert(0);
        }
        return r;
      }
      case T::Exp::Kind::MEM: {
        T::MemExp* memExp = static_cast<T::MemExp *>(e);
        TEMP::Temp* addr = munchExp(memExp->exp);
        emit(new AS::MoveInstr("movq (`s0),`d0", L(r, nullptr), L(addr, nullptr)));
        return r;
      }
      case T::Exp::Kind::TEMP: {
        T::TempExp* tempExp = static_cast<T::TempExp *>(e);
        if (tempExp->temp == F::FP()) {
          std::string instr = "leaq " + fs + "(`s0),`d0";
          emit(new AS::OperInstr(instr, L(r, nullptr), L(F::SP(), nullptr), new AS::Targets(nullptr)));
          return r;
        }
        else
          return tempExp->temp;
        assert(0);
        break;
      }
      case T::Exp::Kind::NAME: {
        T::NameExp* nameExp = static_cast<T::NameExp *>(e);
        std::string instr = "movq $" + nameExp->name->Name() + ",`d0";
        emit(new AS::OperInstr(instr, L(r, nullptr), nullptr, new AS::Targets(nullptr)));
        return r;
      }
      case T::Exp::Kind::CONST: {
        T::ConstExp* constExp = static_cast<T::ConstExp *>(e);
        std::string constString = std::to_string(constExp->consti);
        std::string instr = "movq $" + constString + ",`d0";
        emit(new AS::MoveInstr(instr, L(r, nullptr), nullptr));
        return r;
      }
      case T::Exp::Kind::CALL: {
        T::CallExp* callExp = static_cast<T::CallExp *>(e);
        T::NameExp* funcExp = static_cast<T::NameExp *>(callExp->fun);
        TEMP::TempList* argsTemps = munchArgs(callExp->args);
        std::string instr = "call " + funcExp->name->Name();
        emit(new AS::OperInstr(instr, F::callersaves(), argsTemps, new AS::Targets(nullptr)));
        emit(new AS::MoveInstr("movq `s0, `d0", L(r, nullptr), L(F::RV(), nullptr)));
        return r;
      }
      default: {
        std::cerr << "T::Exp::Kind not recognized: " << e->kind << std::endl;
        assert(0);
      }
    }
    return nullptr;
  }

  TEMP::TempList* L(TEMP::Temp* h, TEMP::TempList* t) {
    return new TEMP::TempList(h, t);
  }

  std::string toOpString(T::RelOp op) {
    std::string opString;
    switch (op) {
      case T::RelOp::EQ_OP:
        opString = "je";
        break;
      case T::RelOp::NE_OP:
        opString = "jne";
        break;
      case T::RelOp::GE_OP:
        opString = "jge";
        break;
      case T::RelOp::GT_OP:
        opString = "jg";
        break;
      case T::RelOp::LE_OP:
        opString = "jle";
        break;
      case T::RelOp::LT_OP:
        opString = "jl";
        break;
      default:
        std::cerr << "T::RelOp not recognized: " << op << std::endl;
        assert(0);
    }
    return opString;
  }

  TEMP::TempList* munchArgs(T::ExpList* args) {
    int count = 0;
    TEMP::TempList* argsregs = F::argregs();
    TEMP::TempList* result = nullptr;
    for (T::ExpList* head = args; head; head = head->tail) {
      TEMP::Temp* arg = munchExp(head->head);
      if (count <= 5) {
        emit(new AS::MoveInstr("movq `s0,`d0", L(argsregs->head, nullptr), L(arg, nullptr)));
        result = new TEMP::TempList(argsregs->head, result);
        argsregs = argsregs->tail;
      }
      else {
        emit(new AS::OperInstr("pushq `s0", nullptr, L(arg, nullptr), new AS::Targets(nullptr))); // No need to consider F::SP() here
      }
      count++;
    }
    return result;
  }
}
