#ifndef TIGER_ESCAPE_ESCAPE_H_
#define TIGER_ESCAPE_ESCAPE_H_

#include "tiger/absyn/absyn.h"
#include "tiger/symbol/symbol.h"

/* Forward Declarations */
namespace A {
  class Exp;
} // namespace A

namespace ESC {

class EscapeEntry;
void FindEscape(A::Exp* exp);

}  // namespace ESC

#endif