#ifndef TIGER_LEX_SCANNER_H_
#define TIGER_LEX_SCANNER_H_

#include <algorithm>
#include <string>
#include <stack>

#include "scannerbase.h"
#include "tiger/errormsg/errormsg.h"
#include "tiger/parse/parserbase.h"

extern EM::ErrorMsg errormsg;

class Scanner : public ScannerBase {
 public:
  explicit Scanner(std::istream &in = std::cin, std::ostream &out = std::cout);

  Scanner(std::string const &infile, std::string const &outfile);

  int lex();

 private:
  int lex__();
  int executeAction__(size_t ruleNr);

  void print();
  void preCode();
  void postCode(PostEnum__ type);
  void adjust();
  void adjustStr();
  void pushCommentCondition();
  void popCommentCondition();
  char to_char(const std::string &s);
  char control_character_to_char(const std::string &s);
  void clearStringBuf();
  void handle_line_feed_in_string(const std::string &s);

  int commentLevel_;
  std::string stringBuf_;
  int charPos_;
  std::stack<StartCondition__> d_commentConditionStack;
};

inline Scanner::Scanner(std::istream &in, std::ostream &out)
    : ScannerBase(in, out), charPos_(1) {}

inline Scanner::Scanner(std::string const &infile, std::string const &outfile)
    : ScannerBase(infile, outfile), charPos_(1) {}

inline int Scanner::lex() { return lex__(); }

inline void Scanner::preCode() {
  // optionally replace by your own code
}

inline void Scanner::postCode(PostEnum__ type) {
  // optionally replace by your own code
}

inline void Scanner::print() { print__(); }

inline void Scanner::adjust() {
  errormsg.tokPos = charPos_;
  charPos_ += length();
}

inline void Scanner::adjustStr() { charPos_ += length(); }

inline void Scanner::pushCommentCondition() {
  d_commentConditionStack.push(startCondition());
  begin(StartCondition__::COMMENT);
}

inline void Scanner::popCommentCondition() {
  begin(d_commentConditionStack.top());
  d_commentConditionStack.pop();
}

inline char Scanner::to_char(const std::string &s) {
  int result = atoi(s.c_str() + 1);
  return (char) result;
}

inline char Scanner::control_character_to_char(const std::string &s) {
  return (s[2] - 64);
}

inline void Scanner::clearStringBuf() {
  stringBuf_.clear();
}

inline void Scanner::handle_line_feed_in_string(const std::string &s) {
  size_t len = s.size();
  for (size_t i = 0; i < len; ++i) {
    if (s[i] == '\n') {
      errormsg.Newline();
    }
  }
}

#endif  // TIGER_LEX_SCANNER_H_

