#include "tiger/frame/frame.h"

#include <string>
#include <vector>

namespace {
  TEMP::Temp* rsp = nullptr;
  TEMP::Temp* rax = nullptr;
  TEMP::Temp* rbp = nullptr;
  TEMP::Temp* rbx = nullptr;
  TEMP::Temp* rdi = nullptr;
  TEMP::Temp* rsi = nullptr;
  TEMP::Temp* rdx = nullptr;
  TEMP::Temp* rcx = nullptr;
  TEMP::Temp* r8 = nullptr;
  TEMP::Temp* r9 = nullptr;
  TEMP::Temp* r10 = nullptr;
  TEMP::Temp* r11 = nullptr;
  TEMP::Temp* r12 = nullptr;
  TEMP::Temp* r13 = nullptr;
  TEMP::Temp* r14 = nullptr;
  TEMP::Temp* r15 = nullptr;
  TEMP::Temp* fp = nullptr;
  TEMP::Map* realTempMap = nullptr;
  TEMP::TempList* registersList = nullptr;
  TEMP::TempList* allocatableRegisterList = nullptr;
  TEMP::TempList* specialregsList = nullptr;
  TEMP::TempList* argregsList = nullptr;
  TEMP::TempList* calleesavesList = nullptr;
  TEMP::TempList* callersavesList = nullptr;
  enum RegisterColor { RAX, RBP, RBX, RDI, RSI, RDX, RCX, R8, R9, R10, R11, R12, R13, R14, R15 };
}

namespace F {

const int wordSize = 8;
const int K = 15;

class InFrameAccess : public Access {
 public:
  int offset;

  InFrameAccess(int offset) : Access(INFRAME), offset(offset) {}

  T::Exp* ToExp(T::Exp* framePtr) const override {
    return new T::MemExp(new T::BinopExp(T::PLUS_OP, framePtr, new T::ConstExp(offset)));
  }
};

class InRegAccess : public Access {
 public:
  TEMP::Temp* reg;

  InRegAccess(TEMP::Temp* reg) : Access(INREG), reg(reg) {}

  T::Exp* ToExp(T::Exp* framePtr) const override {
    return new T::TempExp(reg);
  }
};

TEMP::Temp* RSP() {
  if (!rsp)
    rsp = TEMP::Temp::NewTemp();
  return rsp;
}

TEMP::Temp* RAX() {
  if (!rax)
    rax = TEMP::Temp::NewTemp();
  return rax;
}

TEMP::Temp* RBP() {
  if (!rbp)
    rbp = TEMP::Temp::NewTemp();
  return rbp;
}

TEMP::Temp* RBX() {
  if (!rbx)
    rbx = TEMP::Temp::NewTemp();
  return rbx;
}

TEMP::Temp* RDI() {
  if (!rdi)
    rdi = TEMP::Temp::NewTemp();
  return rdi;
}

TEMP::Temp* RSI() {
  if (!rsi)
    rsi = TEMP::Temp::NewTemp();
  return rsi;
}

TEMP::Temp* RDX() {
  if (!rdx)
    rdx = TEMP::Temp::NewTemp();
  return rdx;
}

TEMP::Temp* RCX() {
  if (!rcx)
    rcx = TEMP::Temp::NewTemp();
  return rcx;
}

TEMP::Temp* R8() {
  if (!r8)
    r8 = TEMP::Temp::NewTemp();
  return r8;
}

TEMP::Temp* R9() {
  if (!r9)
    r9 = TEMP::Temp::NewTemp();
  return r9;
}

TEMP::Temp* R10() {
  if (!r10)
    r10 = TEMP::Temp::NewTemp();
  return r10;
}

TEMP::Temp* R11() {
  if (!r11)
    r11 = TEMP::Temp::NewTemp();
  return r11;
}

TEMP::Temp* R12() {
  if (!r12)
    r12 = TEMP::Temp::NewTemp();
  return r12;
}

TEMP::Temp* R13() {
  if (!r13)
    r13 = TEMP::Temp::NewTemp();
  return r13;
}

TEMP::Temp* R14() {
  if (!r14)
    r14 = TEMP::Temp::NewTemp();
  return r14;
}

TEMP::Temp* R15() {
  if (!r15)
    r15 = TEMP::Temp::NewTemp();
  return r15;
}

void tempInit() {
  tempMap();
  registers();
  specialregs();
  argregs();
  calleesaves();
  callersaves();
  allocatableRegisters();
}

TEMP::Map* tempMap() {
  if (!realTempMap) {
    realTempMap = TEMP::Map::Empty();
    realTempMap->Enter(RSP(), new std::string("%rsp"));
    realTempMap->Enter(RAX(), new std::string("%rax"));
    realTempMap->Enter(RBP(), new std::string("%rbp"));
    realTempMap->Enter(RBX(), new std::string("%rbx"));
    realTempMap->Enter(RDI(), new std::string("%rdi"));
    realTempMap->Enter(RSI(), new std::string("%rsi"));
    realTempMap->Enter(RDX(), new std::string("%rdx"));
    realTempMap->Enter(RCX(), new std::string("%rcx"));
    realTempMap->Enter(R8(), new std::string("%r8"));
    realTempMap->Enter(R9(), new std::string("%r9"));
    realTempMap->Enter(R10(), new std::string("%r10"));
    realTempMap->Enter(R11(), new std::string("%r11"));
    realTempMap->Enter(R12(), new std::string("%r12"));
    realTempMap->Enter(R13(), new std::string("%r13"));
    realTempMap->Enter(R14(), new std::string("%r14"));
    realTempMap->Enter(R15(), new std::string("%r15"));
  }
  return realTempMap;
}

TEMP::TempList* registers() {
  if (!registersList) {
    registersList = new TEMP::TempList(RSP(), 
                    new TEMP::TempList(RAX(), 
                    new TEMP::TempList(RBP(), 
                    new TEMP::TempList(RBX(), 
                    new TEMP::TempList(RDI(),
                    new TEMP::TempList(RSI(),
                    new TEMP::TempList(RDX(),
                    new TEMP::TempList(RCX(),
                    new TEMP::TempList(R8(),
                    new TEMP::TempList(R9(),
                    new TEMP::TempList(R10(),
                    new TEMP::TempList(R11(),
                    new TEMP::TempList(R12(),
                    new TEMP::TempList(R13(),
                    new TEMP::TempList(R14(),
                    new TEMP::TempList(R15(), nullptr))))))))))))))));
  }
  return registersList;
}

TEMP::TempList* allocatableRegisters() {
  if (!allocatableRegisterList) {
    allocatableRegisterList = new TEMP::TempList(RAX(), // No %rsp here
                              new TEMP::TempList(RBP(), 
                              new TEMP::TempList(RBX(), 
                              new TEMP::TempList(RDI(),
                              new TEMP::TempList(RSI(),
                              new TEMP::TempList(RDX(),
                              new TEMP::TempList(RCX(),
                              new TEMP::TempList(R8(),
                              new TEMP::TempList(R9(),
                              new TEMP::TempList(R10(),
                              new TEMP::TempList(R11(),
                              new TEMP::TempList(R12(),
                              new TEMP::TempList(R13(),
                              new TEMP::TempList(R14(),
                              new TEMP::TempList(R15(), nullptr)))))))))))))));
  }
}

TEMP::TempList* specialregs() {
  if (!specialregsList) {
    specialregsList = new TEMP::TempList(SP(),
                      new TEMP::TempList(FP(),
                      new TEMP::TempList(RV(), nullptr)));
  }
  return specialregsList;
}

TEMP::TempList* argregs() {
  if (!argregsList) {
    argregsList = new TEMP::TempList(RDI(),
                  new TEMP::TempList(RSI(),
                  new TEMP::TempList(RDX(),
                  new TEMP::TempList(RCX(),
                  new TEMP::TempList(R8(),
                  new TEMP::TempList(R9(), nullptr))))));
  }
  return argregsList;
}

TEMP::TempList* calleesaves() {
  if (!calleesavesList) {
    calleesavesList = new TEMP::TempList(RBX(),
                      new TEMP::TempList(RBP(),
                      new TEMP::TempList(R12(),
                      new TEMP::TempList(R13(),
                      new TEMP::TempList(R14(),
                      new TEMP::TempList(R15(), nullptr))))));
  }
  return calleesavesList;
}

TEMP::TempList* callersaves() {
  if (!callersavesList) {
    callersavesList = new TEMP::TempList(R10(),
                      new TEMP::TempList(R11(), nullptr));
  }
  return callersavesList;
}

TEMP::TempList* notCalleesaves() {
  static TEMP::TempList* notCalleesavesList = nullptr;
  if (!notCalleesavesList) {
    notCalleesavesList = new TEMP::TempList(RAX(), // Other 9 registers
                         new TEMP::TempList(RDI(),
                         new TEMP::TempList(RSI(),
                         new TEMP::TempList(RDX(),
                         new TEMP::TempList(RCX(),
                         new TEMP::TempList(R8(),
                         new TEMP::TempList(R9(),
                         new TEMP::TempList(R10(),
                         new TEMP::TempList(R11(), nullptr)))))))));
  }
  return notCalleesavesList;
}

TEMP::Temp* SP() {
  return RSP();
}

TEMP::Temp* FP() {
  if (!fp)
    fp = TEMP::Temp::NewTemp();
  return fp;
}

TEMP::Temp* RV() {
  return RAX();
}

TEMP::Temp* NUMERATOR() {
  return RAX();
}

TEMP::Temp* NUMERATOR_HIGHER_64() {
  return RDX();
}

TEMP::Temp* QUOTIENT() {
  return RAX();
}

TEMP::Temp* REMAINDER() {
  return RDX();
}

int defaultRegisterColor(TEMP::Temp* t) {
  assert(t != RSP());
  assert(t);
  if (t == RAX())
    return RegisterColor::RAX;
  else if (t == RBP())
    return RegisterColor::RBP;
  else if (t == RBX())
    return RegisterColor::RBX;
  else if (t == RDI())
    return RegisterColor::RDI;
  else if (t == RSI())
    return RegisterColor::RSI;
  else if (t == RDX())
    return RegisterColor::RDX;
  else if (t == RCX())
    return RegisterColor::RCX;
  else if (t == R8())
    return RegisterColor::R8;
  else if (t == R9())
    return RegisterColor::R9;
  else if (t == R10())
    return RegisterColor::R10;
  else if (t == R11())
    return RegisterColor::R11;
  else if (t == R12())
    return RegisterColor::R12;
  else if (t == R13())
    return RegisterColor::R13;
  else if (t == R14())
    return RegisterColor::R14;
  else if (t == R15())
    return RegisterColor::R15;
  return -1;
}

std::string* color2register(int color) {
  switch (color) {
    case RegisterColor::RAX:
      return new std::string("%rax");
    case RegisterColor::RBP:
      return new std::string("%rbp");
    case RegisterColor::RBX:
      return new std::string("%rbx");
    case RegisterColor::RDI:
      return new std::string("%rdi");
    case RegisterColor::RSI:
      return new std::string("%rsi");
    case RegisterColor::RDX:
      return new std::string("%rdx");
    case RegisterColor::RCX:
      return new std::string("%rcx");
    case RegisterColor::R8:
      return new std::string("%r8");
    case RegisterColor::R9:
      return new std::string("%r9");
    case RegisterColor::R10:
      return new std::string("%r10");
    case RegisterColor::R11:
      return new std::string("%r11");
    case RegisterColor::R12:
      return new std::string("%r12");
    case RegisterColor::R13:
      return new std::string("%r13");
    case RegisterColor::R14:
      return new std::string("%r14");
    case RegisterColor::R15:
      return new std::string("%r15");
    default:
      assert(0);
  }
  assert(0);
  return nullptr;
}

// Returns true when this move instruction can be omitted
bool checkMoveInstr(std::string instr) {
  std::size_t firstPos = instr.find_first_of('%');
  std::size_t secondPos = instr.find_first_of('%', firstPos+1);
  assert(firstPos != std::string::npos && secondPos != std::string::npos);
  if (instr[firstPos+1] == 'r' && (instr[firstPos+2] == '8' || instr[firstPos+2] == '9')) {
    return (instr[firstPos+1] == instr[secondPos+1] && instr[firstPos+2] == instr[secondPos+2]);
  }
  else {
    return (instr[firstPos+1] == instr[secondPos+1] && 
            instr[firstPos+2] == instr[secondPos+2] &&
            instr[firstPos+3] == instr[secondPos+3]);
  }
}

class X64Frame : public Frame {
  // TODO: Put your codes here (lab6).
  private:
    void AddToFormalList(Access* access) {
      AccessList* tail = formalList;
      if (!tail) {
        formalList = new AccessList(access, nullptr);
        return;
      }
      else {
        while (tail->tail) {
          tail = tail->tail;
        }
        tail->tail = new AccessList(access, nullptr);
        return;
      }
      return;
    }

    void AddToPrologue(T::Stm* stm) {
      if (!prologue) {
        prologue = stm;
      }
      else {
        prologue = new T::SeqStm(prologue, stm);
      }
    }

  public:
    std::string label; // name->Name()? TBD
    TEMP::Label* name;
    AccessList* formalList; // formal parameters list
    AccessList* localList; // the number of locals allocated so far. TBD
    int frameSize;
    T::Stm* prologue; // view shift, move parameters to other places
    int maxArgNumber;

    X64Frame(TEMP::Label* name, U::BoolList* formals) : Frame(X64), name(name) {
      localList = nullptr;
      formalList = nullptr;
      frameSize = 0;
      maxArgNumber = 0;

      U::BoolList* formalsPtr = formals;

      int num = 0;
      
      for (; formalsPtr; formalsPtr = formalsPtr->tail) {
        // walk through formal paramters list
        if (formalsPtr->head) {
          // this parameter escapes
          switch (num) {
            case 0: {
              Access* access = AllocLocal(true);
              T::Stm* stm = new T::MoveStm(
                new T::MemExp(new T::BinopExp(T::PLUS_OP, new T::TempExp(FP()), new T::ConstExp(-frameSize))),
                new T::TempExp(RDI())
              );
              AddToPrologue(stm);
              AddToFormalList(access);
              break;
            }
            case 1: {
              Access* access = AllocLocal(true);
              T::Stm* stm = new T::MoveStm(
                new T::MemExp(new T::BinopExp(T::PLUS_OP, new T::TempExp(FP()), new T::ConstExp(-frameSize))),
                new T::TempExp(RSI())
              );
              AddToPrologue(stm);
              AddToFormalList(access);
              break;
            }
            case 2: {
              Access* access = AllocLocal(true);
              T::Stm* stm = new T::MoveStm(
                new T::MemExp(new T::BinopExp(T::PLUS_OP, new T::TempExp(FP()), new T::ConstExp(-frameSize))),
                new T::TempExp(RDX())
              );
              AddToPrologue(stm);
              AddToFormalList(access);
              break;
            }
            case 3: {
              Access* access = AllocLocal(true);
              T::Stm* stm = new T::MoveStm(
                new T::MemExp(new T::BinopExp(T::PLUS_OP, new T::TempExp(FP()), new T::ConstExp(-frameSize))),
                new T::TempExp(RCX())
              );
              AddToPrologue(stm);
              AddToFormalList(access);
              break;
            }
            case 4: {
              Access* access = AllocLocal(true);
              T::Stm* stm = new T::MoveStm(
                new T::MemExp(new T::BinopExp(T::PLUS_OP, new T::TempExp(FP()), new T::ConstExp(-frameSize))),
                new T::TempExp(R8())
              );
              AddToPrologue(stm);
              AddToFormalList(access);
              break;
            }
            case 5: {
              Access* access = AllocLocal(true);
              T::Stm* stm = new T::MoveStm(
                new T::MemExp(new T::BinopExp(T::PLUS_OP, new T::TempExp(FP()), new T::ConstExp(-frameSize))),
                new T::TempExp(R9())
              );
              AddToPrologue(stm);
              AddToFormalList(access);
              break;
            }
            default: {
              Access* access = new InFrameAccess((num - 5) * wordSize);
              AddToFormalList(access);
              break;
            }
          }
        }
        else {
          // this parameter does not escape
          InRegAccess* inRegAccess = new InRegAccess(TEMP::Temp::NewTemp());
          AddToFormalList(inRegAccess);
          switch (num) {
            case 0: {
              T::Stm* stm = new T::MoveStm(
                new T::TempExp(inRegAccess->reg),
                new T::TempExp(RDI())
              );
              AddToPrologue(stm);
              break;
            }
            case 1: {
              T::Stm* stm = new T::MoveStm(
                new T::TempExp(inRegAccess->reg),
                new T::TempExp(RSI())
              );
              AddToPrologue(stm);
              break;
            }
            case 2: {
              T::Stm* stm = new T::MoveStm(
                new T::TempExp(inRegAccess->reg),
                new T::TempExp(RDX())
              );
              AddToPrologue(stm);
              break;
            }
            case 3: {
              T::Stm* stm = new T::MoveStm(
                new T::TempExp(inRegAccess->reg),
                new T::TempExp(RCX())
              );
              AddToPrologue(stm);
              break;
            }
            case 4: {
              T::Stm* stm = new T::MoveStm(
                new T::TempExp(inRegAccess->reg),
                new T::TempExp(R8())
              );
              AddToPrologue(stm);
              break;
            }
            case 5: {
              T::Stm* stm = new T::MoveStm(
                new T::TempExp(inRegAccess->reg),
                new T::TempExp(R9())
              );
              AddToPrologue(stm);
              break;
            }
            default: {
              T::Stm* stm = new T::MoveStm(
                new T::TempExp(inRegAccess->reg),
                new T::MemExp(new T::BinopExp(T::PLUS_OP, new T::TempExp(FP()), new T::ConstExp((num - 5) * wordSize)))
              );
              break;
            }
          }
        }
        num++;
      }
    }

    std::string GetLabel() const override {
      return label;
    }

    TEMP::Label* GetName() const override {
      return name;
    }

    AccessList* GetFormalList() const override {
      return formalList;
    }

    AccessList* GetLocalList() const override {
      return localList;
    }

    T::Stm* GetPrologue() const override {
      return prologue;
    }

    int GetSize() const override {
      return frameSize;
    }

    Access* AllocLocal(bool escape) override {
      if (escape) {
        frameSize += wordSize;
        return new InFrameAccess(-frameSize);
      }
      else {
        return new InRegAccess(TEMP::Temp::NewTemp());
      }
    }

    int GetMaxArgNumber() const override {
      return maxArgNumber;
    }

    void SetMaxArgNumber(int maxArgNumber) override {
      this->maxArgNumber = maxArgNumber;
    }
};

T::Exp* externalCall(std::string s, T::ExpList* args) {
  return new T::CallExp(new T::NameExp(TEMP::NamedLabel(s)), args); // P169, no need to place static link
}

Frame* NewX64Frame(TEMP::Label* name, U::BoolList* formals) {
  return new X64Frame(name, formals);
}

T::Stm* F_procEntryExit1(Frame* frame, T::Stm* stm) {
  std::vector<TEMP::Temp *> calleeSaveTemps;
  T::Stm* save = nullptr;
  T::Stm* restore = nullptr;

  // Save registers
  TEMP::TempList* calleeSaveRegisters = calleesaves();
  for (; calleeSaveRegisters; calleeSaveRegisters = calleeSaveRegisters->tail) {
    TEMP::Temp* temp = TEMP::Temp::NewTemp();
    T::Exp* tempExp = new T::TempExp(temp);
    calleeSaveTemps.push_back(temp);
    if (save) {
      save = new T::SeqStm(save, new T::MoveStm(tempExp, new T::TempExp(calleeSaveRegisters->head)));
    }
    else {
      save = new T::MoveStm(tempExp, new T::TempExp(calleeSaveRegisters->head));
    }
  }

  // Restore registers
  calleeSaveRegisters = calleesaves();
  int count = 0;
  for (; calleeSaveRegisters; calleeSaveRegisters = calleeSaveRegisters->tail) {
    T::Exp* tempExp = new T::TempExp(calleeSaveTemps[count]);
    if (restore) {
      restore = new T::SeqStm(restore, new T::MoveStm(new T::TempExp(calleeSaveRegisters->head), tempExp));
    }
    else {
      restore = new T::MoveStm(new T::TempExp(calleeSaveRegisters->head), tempExp);
    }
    count++;
  }

  T::Stm* prologue = frame->GetPrologue();
  if (!prologue) {
    prologue = new T::ExpStm(new T::ConstExp(0));
  }
  return new T::SeqStm(prologue, new T::SeqStm(save, new T::SeqStm(stm, restore)));
}

AS::InstrList* F_procEntryExit2(AS::InstrList* body) {
  // P215
  static TEMP::TempList* returnSink = nullptr;
  if (!returnSink)
    returnSink = 
    new TEMP::TempList(SP(), 
      new TEMP::TempList(RV(), 
        calleesaves()));
  return AS::InstrList::Splice(body, 
          new AS::InstrList(new AS::OperInstr("", nullptr, returnSink, nullptr), nullptr));
}

AS::Proc* F_procEntryExit3(Frame* frame, AS::InstrList* body) {
  int extraArgs = 0;
  if (frame->GetMaxArgNumber() > 6)
    extraArgs = frame->GetMaxArgNumber() - 6;
  std::string fs = frame->GetName()->Name() + "_framesize";
  std::string prolog = ".set " + fs + "," + std::to_string(frame->GetSize() + wordSize * extraArgs) + "\n";
  prolog = prolog + frame->GetName()->Name() + ":\n";
  prolog = prolog + "subq $" + std::to_string(frame->GetSize() + wordSize * extraArgs) + ",%rsp\n";
  std::string epilog = "addq $" + std::to_string(frame->GetSize() + wordSize * extraArgs) + ",%rsp\n";
  epilog = epilog + "ret\n";
  return new AS::Proc(prolog, body, epilog);
}

}  // namespace F