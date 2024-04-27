// aarch64 Instruction

#pragma once

#include <stdint.h>  // int64_t

typedef struct Expr Expr;

enum Opcode {
  NOOP,
  MOV,
  LDP, STP,
  BL, BLR,
  RET,
};

enum RegSize {
  REG32,
  REG64,
};

typedef struct {
  char size;  // RegSize
  char no;  // 0~31
} Reg;

enum OperandType {
  NOOPERAND,
  REG,        // reg
  IMMEDIATE,  // 1234
  DIRECT,     // foobar + 345
  INDIRECT,   // indirect:   [reg,#12]
              // pre-index:  [reg,#34]!
              // post-index: [reg],#34
};

typedef struct {
  enum OperandType type;
  union {
    Reg reg;
    int64_t immediate;
    struct {
      Expr *expr;
    } direct;
    struct {
      Expr *offset;
      Reg reg;
      int prepost;  // 0=none, 1=pre, 2=post
    } indirect;
  };
} Operand;

typedef struct Inst {
  enum Opcode op;
  Operand opr[4];
} Inst;
