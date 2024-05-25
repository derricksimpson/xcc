// aarch64 Instruction

#pragma once

#include <stdint.h>  // int64_t

typedef struct Expr Expr;

enum Opcode {
  NOOP,
  MOV, MOVK,
  ADD_R, ADD_I, SUB_R, SUB_I,
  MUL, SDIV, UDIV,
  MADD, MSUB,
  AND, ORR, EOR, EON,
  CMP_R, CMP_I, CMN_R, CMN_I,
  LSL_R, LSL_I,
  LSR_R, LSR_I,
  ASR_R, ASR_I,
  SXTB, SXTH, SXTW,
  UXTB, UXTH, UXTW,
  LDRB, LDRH, LDR, LDRSB, LDRSH, LDRSW,
  STRB, STRH, STR,
  LDP, STP,
  ADRP,
  CSET,
  B, BR,
  BEQ, BNE, BHS, BLO, BMI, BPL, BVS, BVC,
  BHI, BLS, BGE, BLT, BGT, BLE, BAL, BNV,
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

enum CondType {
  NOCOND = -1,
  EQ, NE, HS, LO, MI, PL, VS, VC,
  HI, LS, GE, LT, GT, LE, AL, NV,
};

enum OperandType {
  NOOPERAND,
  REG,        // reg
  IMMEDIATE,  // 1234
  DIRECT,     // foobar + 345
  INDIRECT,   // indirect:   [reg,#12]
              // pre-index:  [reg,#34]!
              // post-index: [reg],#34
  COND,
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
    enum CondType cond;
  };
} Operand;

typedef struct Inst {
  enum Opcode op;
  Operand opr[4];
} Inst;
