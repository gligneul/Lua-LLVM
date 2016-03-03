/*
** LLL - Lua Low Level
** September, 2015
** Author: Gabriel de Quadros Ligneul
** Copyright Notice for LLL: see lllcore.h
**
** lllvararg.h
** Compiles the vararg opcode
*/

#ifndef LLLVARARG_H
#define LLLVARARG_H

#include "lllopcode.h"

namespace lll {

class Vararg : public Opcode<Vararg> {
public:
    // Constructor
    Vararg(CompilerState& cs);

    // Returns the list of steps
    std::vector<CompilationStep> GetSteps();

private:
    // Compilation steps
    void ComputeAvailableArgs();
    void ComputeRequiredArgs();
    void ComputeNMoves();
    void MoveAvailable();
    void FillRequired();

    // Retuns the register at ra + offset
    llvm::Value* GetRegisterFromA(llvm::Value* offset);

    llvm::Value* available_;
    llvm::Value* required_;
    llvm::Value* nmoves_;
    llvm::BasicBlock* movecheck_;
    llvm::BasicBlock* move_;
    llvm::BasicBlock* fillcheck_;
    llvm::BasicBlock* fill_;
};

}

#endif

