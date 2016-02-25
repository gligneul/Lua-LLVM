/*
** LLL - Lua Low Level
** September, 2015
** Author: Gabriel de Quadros Ligneul
** Copyright Notice for LLL: see lllcore.h
**
** lllarith.h
** Compiles the arithmetics opcodes
*/

#ifndef LLLARITH_H
#define LLLARITH_H

#include <map>

#include "lllopcode.h"

namespace lll {

class CompilerState;

class Arith : public Opcode<Arith> {
public:
    // Constructor
    Arith(CompilerState& cs);

    // Returns the list of steps
    std::vector<CompilationStep> GetSteps();

private:
    // Compilation steps
    llvm::BasicBlock* ComputeInteger(llvm::BasicBlock* entry);
    llvm::BasicBlock* ComputeFloat(llvm::BasicBlock* entry);
    llvm::BasicBlock* ComputeTaggedMethod(llvm::BasicBlock* entry);

    // Returns whether the opcode can perform an integer/float operation
    bool HasIntegerOp();
    bool HasFloatOp();

    // Returns whether the constant can perform an integer/float operation
    bool CanPerformIntegerOp(int v);
    bool CanPerformFloatOp(int v);

    // Returns whether the value is integer
    llvm::Value* IsInteger(int v);

    // Obtains the integer value
    llvm::Value* LoadInteger(int v);

    // Returns wheter the value is float and, if it is, returns it
    std::pair<llvm::Value*, llvm::Value*> ConvertToFloat(int v);

    // Performs the integer/float binary operation
    llvm::Value* PerformIntOp(llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* PerformFloatOp(llvm::Value* lhs, llvm::Value* rhs);
    
    // Obtains the corresponding tag for the opcode
    int GetMethodTag();

    CompilerState& cs_;
};

}

#endif
