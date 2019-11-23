#include "tiger/frame/frame.h"

#include <string>

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
  TEMP::TempList* specialregsList = nullptr;
  TEMP::TempList* argregsList = nullptr;
  TEMP::TempList* calleesavesList = nullptr;
  TEMP::TempList* callersavesList = nullptr;
}

namespace F {

const int wordSize = 8;

void tempInit() {
  tempMap();
  registers();
  specialregs();
  argregs();
  calleesaves();
  callersaves();
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

class X64Frame : public Frame {
  // TODO: Put your codes here (lab6).
};

class InFrameAccess : public Access {
 public:
  int offset;

  InFrameAccess(int offset) : Access(INFRAME), offset(offset) {}

  T::Exp* ToExp(T::Exp* framePtr) const override {
    return nullptr;
  }
};

class InRegAccess : public Access {
 public:
  TEMP::Temp* reg;

  InRegAccess(TEMP::Temp* reg) : Access(INREG), reg(reg) {}

  T::Exp* ToExp(T::Exp* framePtr) const override {
    return nullptr;
  }
};

}  // namespace F