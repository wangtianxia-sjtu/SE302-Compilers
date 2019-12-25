#ifndef TIGER_FRAME_FRAME_H_
#define TIGER_FRAME_FRAME_H_

#include <string>

#include "tiger/codegen/assem.h"
#include "tiger/translate/tree.h"
#include "tiger/util/util.h"

namespace F {

class Frame;
class Access;
class AccessList;

extern const int wordSize;
extern const int K; // Used in register allocation phase

TEMP::Map* tempMap();
TEMP::TempList* registers();
void tempInit();
TEMP::TempList* allocatableRegisters();

TEMP::TempList* specialregs();
TEMP::TempList* argregs();
TEMP::TempList* calleesaves();
TEMP::TempList* callersaves();
TEMP::TempList* notCalleesaves();

TEMP::Temp* SP();
TEMP::Temp* FP();
TEMP::Temp* RV();

TEMP::Temp* NUMERATOR();
TEMP::Temp* NUMERATOR_HIGHER_64();
TEMP::Temp* QUOTIENT();
TEMP::Temp* REMAINDER();

Frame* NewX64Frame(TEMP::Label* name, U::BoolList* formals);

T::Exp* externalCall(std::string s, T::ExpList* args);

T::Stm* F_procEntryExit1(Frame* frame, T::Stm* stm); // P172 P267-269 TODO
AS::InstrList* F_procEntryExit2(AS::InstrList* body); // P215 TODO
AS::Proc* F_procEntryExit3(Frame* frame, AS::InstrList* body); // P267-269 TODO

int defaultRegisterColor(TEMP::Temp* t);
std::string* color2register(int color);

bool checkMoveInstr(std::string instr);


class Frame {
  // Base class, see P138
  public:
    enum Kind { X64 };

    Kind kind;

    Frame(Kind kind) : kind(kind) {}
    virtual std::string GetLabel() const = 0;
    virtual TEMP::Label* GetName() const = 0;
    virtual AccessList* GetFormalList() const = 0;
    virtual AccessList* GetLocalList() const = 0;
    virtual Access* AllocLocal(bool escape) = 0;
    virtual T::Stm* GetPrologue() const = 0;
    virtual int GetSize() const = 0;
};

class Access {
 public:
  enum Kind { INFRAME, INREG };

  Kind kind;

  Access(Kind kind) : kind(kind) {}

  // Hints: You may add interface like
  virtual T::Exp* ToExp(T::Exp* framePtr) const = 0;
};

class AccessList {
 public:
  Access *head;
  AccessList *tail;

  AccessList(Access *head, AccessList *tail) : head(head), tail(tail) {}
};

/*
 * Fragments
 */

class Frag {
 public:
  enum Kind { STRING, PROC };

  Kind kind;

  Frag(Kind kind) : kind(kind) {}
};

class StringFrag : public Frag {
 public:
  TEMP::Label *label;
  std::string str;

  StringFrag(TEMP::Label *label, std::string str)
      : Frag(STRING), label(label), str(str) {}
};

class ProcFrag : public Frag {
 public:
  T::Stm *body;
  Frame *frame;

  ProcFrag(T::Stm *body, Frame *frame) : Frag(PROC), body(body), frame(frame) {}
};

class FragList {
 public:
  Frag *head;
  FragList *tail;

  FragList(Frag *head, FragList *tail) : head(head), tail(tail) {}
};

}  // namespace F

#endif