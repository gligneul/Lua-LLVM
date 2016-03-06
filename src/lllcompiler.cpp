/*
** LLL - Lua Low Level
** September, 2015
** Author: Gabriel de Quadros Ligneul
** Copyright Notice for LLL: see lllcore.h
**
** lllcompiler.cpp
*/

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Verifier.h>
#include <llvm/PassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/Scalar.h>

#define LLL_USE_MCJIT

#ifdef LLL_USE_MCJIT
#include <llvm/ExecutionEngine/MCJIT.h>
#else
#include <llvm/ExecutionEngine/JIT.h>
#endif

#include "lllarith.h"
#include "lllcompiler.h"
#include "lllengine.h"
#include "llllogical.h"
#include "lllruntime.h"
#include "llltableget.h"
#include "llltableset.h"
#include "lllvalue.h"
#include "lllvararg.h"

extern "C" {
#include "lprefix.h"
#include "lfunc.h"
#include "lgc.h"
#include "lopcodes.h"
#include "luaconf.h"
#include "lvm.h"
#include "ltable.h"
}

namespace lll {

Compiler::Compiler(lua_State* L, Proto* proto) :
    cs_(L, proto),
    engine_(nullptr) {
    static bool init = true;
    if (init) {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        init = false;
    }
}

bool Compiler::Compile() {
    return CompileInstructions() &&
           VerifyModule() &&
           OptimizeModule() &&
           CreateEngine();
}

const std::string& Compiler::GetErrorMessage() {
    return error_;
}

Engine* Compiler::GetEngine() {
    return engine_.release();
}

bool Compiler::CompileInstructions() {
    for (cs_.curr_ = 0; cs_.curr_ < cs_.proto_->sizecode; ++cs_.curr_) {
        cs_.builder_.SetInsertPoint(cs_.blocks_[cs_.curr_]);
        cs_.instr_ = cs_.proto_->code[cs_.curr_];
        //cs_.DebugPrint(luaP_opnames[GET_OPCODE(cs_.instr_)]);
        switch (GET_OPCODE(cs_.instr_)) {
            case OP_MOVE:     CompileMove(); break;
            case OP_LOADK:    CompileLoadk(false); break;
            case OP_LOADKX:   CompileLoadk(true); break;
            case OP_LOADBOOL: CompileLoadbool(); break;
            case OP_LOADNIL:  CompileLoadnil(); break;
            case OP_GETUPVAL: CompileGetupval(); break;
            case OP_GETTABUP: CompileGettabup(); break;
            case OP_GETTABLE: CompileGettable(); break;
            case OP_SETTABUP: CompileSettabup(); break;
            case OP_SETUPVAL: CompileSetupval(); break;
            case OP_SETTABLE: CompileSettable(); break;
            case OP_NEWTABLE: CompileNewtable(); break;
            case OP_SELF:     CompileSelf(); break;
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_MOD: case OP_POW:  
            case OP_DIV: case OP_IDIV:
                              Arith(cs_).Compile(); break;
            case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR:
                              Logical(cs_).Compile(); break;
            case OP_UNM:      CompileUnop("lll_unm"); break;
            case OP_BNOT:     CompileUnop("lll_bnot"); break;
            case OP_NOT:      CompileUnop("lll_not"); break;
            case OP_LEN:      CompileUnop("luaV_objlen"); break;
            case OP_CONCAT:   CompileConcat(); break;
            case OP_JMP:      CompileJmp(); break;
            case OP_EQ:       CompileCmp("luaV_equalobj"); break;
            case OP_LT:       CompileCmp("luaV_lessthan"); break;
            case OP_LE:       CompileCmp("luaV_lessequal"); break;
            case OP_TEST:     CompileTest(); break;
            case OP_TESTSET:  CompileTestset(); break;
            case OP_CALL:     CompileCall(); break;
            case OP_TAILCALL: CompileTailcall(); break;
            case OP_RETURN:   CompileReturn(); break;
            case OP_FORLOOP:  CompileForloop(); break;
            case OP_FORPREP:  CompileForprep(); break;
            case OP_TFORCALL: CompileTforcall(); break;
            case OP_TFORLOOP: CompileTforloop(); break;
            case OP_SETLIST:  CompileSetlist(); break;
            case OP_CLOSURE:  CompileClosure(); break;
            case OP_VARARG:   Vararg(cs_).Compile(); break;
            case OP_EXTRAARG: /* ignored */ break;
        }
        if (!cs_.blocks_[cs_.curr_]->getTerminator())
            cs_.builder_.CreateBr(cs_.blocks_[cs_.curr_ + 1]);
    }
    return true;
}

bool Compiler::VerifyModule() {
    llvm::raw_string_ostream error_os(error_);
    bool err = llvm::verifyModule(*cs_.module_, &error_os);
    if (err)
        cs_.module_->dump();
    return !err;
}

bool Compiler::OptimizeModule() {
    llvm::FunctionPassManager fpm(cs_.module_.get());
    fpm.add(llvm::createPromoteMemoryToRegisterPass());
    fpm.run(*cs_.function_);
    return true;
}

bool Compiler::CreateEngine() {
    auto module = cs_.module_.get();
    auto engine = llvm::EngineBuilder(cs_.module_.release())
            .setErrorStr(&error_)
            .setOptLevel(OPT_LEVEL)
            .setEngineKind(llvm::EngineKind::JIT)
#ifdef LLL_USE_MCJIT
            .setUseMCJIT(true)
#else
            .setUseMCJIT(false)
#endif
            .create();

    if (engine) {
        engine->finalizeObject();
        engine_.reset(new Engine(engine, module, cs_.function_));
        return true;
    } else {
        return false;
    }
}

void Compiler::CompileMove() {
    Register ra(cs_, GETARG_A(cs_.instr_), "ra");
    Register rb(cs_, GETARG_B(cs_.instr_), "rb");
    ra.Assign(rb);
}

void Compiler::CompileLoadk(bool extraarg) {
    Register ra(cs_, GETARG_A(cs_.instr_), "ra");
    int karg = extraarg ? GETARG_Ax(cs_.proto_->code[cs_.curr_ + 1])
                        : GETARG_Bx(cs_.instr_);
    Constant k(cs_, karg)
    ra.Assign(k);
}

void Compiler::CompileLoadbool() {
    Register ra(cs_, GETARG_A(cs_.instr_), "ra");
    ra.SetBoolean(cs_.MakeInt(GETARG_B(cs_.instr_)));
    if (GETARG_C(cs_.instr_))
        cs_.builder_.CreateBr(cs_.blocks_[cs_.curr_ + 2]);
}

void Compiler::CompileLoadnil() {
    int start = GETARG_A(cs_.instr_);
    int end = start + GETARG_B(cs_.instr_);
    for (int i = start; i <= end; ++i) {
        Register r(cs_, i, "r" + std::to_string(i));
        r.SetTag(LUA_TNIL);
    }
}

void Compiler::CompileGetupval() {
    Register ra(cs_, GETARG_A(cs_.instr_), "ra");
    Upvalue upval(cs_, GETARG_B(cs_.instr_));
    ra.Assign(upval);
}

void Compiler::CompileGettabup() {
    auto table = new Upvalue(cs_, GETARG_B(cs_.instr_));
    auto key = Value::CreateByArg(cs_, GETARG_C(cs_.instr_), "rkc");
    auto dest = new Register(cs_, GETARG_A(cs_.instr_), "ra");
    TableGet(cs_, table, key, dest).Compile();
}

void Compiler::CompileGettable() {
    auto table = new Register(cs_, GETARG_B(cs_.instr_), "rb");
    auto key = Value::CreateByArg(cs_, GETARG_C(cs_.instr_), "rkc");
    auto dest = new Register(cs_, GETARG_A(cs_.instr_), "ra");
    TableGet(cs_, table, key, dest).Compile();
}

void Compiler::CompileSettabup() {
    auto table = new Upval(cs_, GETARG_A(cs_.instr_));
    auto key = Value::CreateByArg(cs_, GETARG_B(cs_.instr_), "rkb");
    auto value = Value::CreateByArg(cs_, GETARG_C(cs_.instr_), "rkc");
    TableSet(cs_, table, key, value).Compile();
}

void Compiler::CompileSetupval() {
    Upvalue upval(cs_, GETARG_B(cs_.instr_));
    Register ra(cs_, GETARG_A(cs_.instr_), "ra");
    upval.Assign(ra);
    cs_.CreateCall("lll_upvalbarrier", {cs_.values_.state, upval.GetTValue()});
}

void Compiler::CompileSettable() {
    auto table = new Register(cs_, GETARG_A(cs_.instr_), "ra");
    auto key = Value::CreateByArg(cs_, GETARG_B(cs_.instr_), "rkb");
    auto value = Value::CreateByArg(cs_, GETARG_C(cs_.instr_), "rkc");
    TableSet(cs_, table, key, value).Compile();
}

void Compiler::CompileNewtable() {
    int a = GETARG_A(cs_.instr_);
    int b = GETARG_B(cs_.instr_);
    int c = GETARG_C(cs_.instr_);
    Register ra(cs_, a, "ra");
    auto args = {cs_.values_.state, ra.GetTValue()};
    auto table = cs_.CreateCall("lll_newtable", args);
    if (b != 0 || c != 0) {
        args = {
            cs_.values_.state,
            table,
            cs_.MakeInt(luaO_fb2int(b)),
            cs_.MakeInt(luaO_fb2int(c))
        };
        cs_.CreateCall("luaH_resize", args);
    }
    Register ra1(cs_, a + 1, "ra1");
    CompileCheckcg(ra1.GetTValue());
}

void Compiler::CompileSelf() {
    auto table = new Register(cs_, GETARG_B(cs_.instr_), "rb");
    auto key = Value::CreateByArg(cs_, GETARG_C(cs_.instr_), "rkc");
    auto methodslot = new Register(cs_, GETARG_A(cs_.instr_), "ra");
    Register selfslot(cs_, GETARG_A(cs_.instr_) + 1, "ra1");
    selfslot.SetValue(table);
    TableGet(cs_, table, key, methodslot).Compile();
}

void Compiler::CompileUnop(const std::string& function) {
    Register ra(cs_, GETARG_A(cs_.instr_), "ra");
    std::unique_ptr<Value> rkb(
            Value::CreateByArg(cs_, GETARG_B(cs_.instr_), "rkb"));
    auto args = {cs_.values_.state, ra.GetTValue(), rkb->GetTValue()};
    cs_.CreateCall(function, args);
    cs_.UpdateStack();
}

void Compiler::CompileConcat() {
    int a = GETARG_A(cs_.instr_);
    int b = GETARG_B(cs_.instr_);
    int c = GETARG_C(cs_.instr_);

    cs_.SetTop(c + 1);
    auto args = {cs_.values_.state, cs_.MakeInt(c - b + 1)};
    cs_.CreateCall("luaV_concat", args);
    cs_.UpdateStack();

    Register ra(cs_, a, "ra");
    Register rb(cs_, b, "rb");
    ra.Assign(rb);

    if (a >= b) {
        Register ra1(cs_, a + 1, "ra1");
        CompileCheckcg(ra1.GetTValue());
    } else {
        CompileCheckcg(rb.GetTValue());
    }

    cs_.ReloadTop();
}

void Compiler::CompileJmp() {
    cs_.builder_.CreateBr(cs_.blocks_[cs_.curr_ + GETARG_sBx(cs_.instr_) + 1]);
}

void Compiler::CompileCmp(const std::string& function) {
    std::unique_ptr<Value> rkb(
            Value::CreateByArg(cs_, GETARG_B(cs_.instr_), "rkb"));
    std::unique_ptr<Value> rkc(
            Value::CreateByArg(cs_, GETARG_C(cs_.instr_), "rkc"));
    auto args = {
        cs_.values_.state,
        rkb.GetTValue(),
        rkc.GetTValue()
    };
    auto result = cs_.CreateCall(function, args, "result");
    cs_.UpdateStack();

    auto a = cs_.MakeInt(GETARG_A(cs_.instr_));
    auto cmp = cs_.builder_.CreateICmpNE(result, a, "cmp");
    auto nextblock = cs_.blocks_[cs_.curr_ + 2];
    auto jmpblock = cs_.blocks_[cs_.curr_ + 1];
    cs_.builder_.CreateCondBr(cmp, nextblock, jmpblock);
}

void Compiler::CompileTest() {
    Register ra(cs_, cs_.GetValueR(GETARG_A(cs_.instr_), "ra"));
    auto args = {
        cs_.MakeInt(GETARG_C(cs_.instr_)),
        
    };
    auto test = cs_.ToBool(cs_.CreateCall("lll_test", args, "test"));
    auto nextblock = cs_.blocks_[cs_.curr_ + 2];
    auto jmpblock = cs_.blocks_[cs_.curr_ + 1];
    cs_.builder_.CreateCondBr(test, nextblock, jmpblock);
}

void Compiler::CompileTestset() {
    auto rb = cs_.GetValueR(GETARG_B(cs_.instr_), "rb");
    auto args = {cs_.MakeInt(GETARG_C(cs_.instr_)), rb};
    auto result = cs_.ToBool(cs_.CreateCall("lll_test", args, "result"));
    auto setblock = cs_.CreateSubBlock("set");
    cs_.builder_.CreateCondBr(result, cs_.blocks_[cs_.curr_ + 2], setblock);
    cs_.builder_.SetInsertPoint(setblock);
    auto ra = cs_.GetValueR(GETARG_A(cs_.instr_), "ra");
    cs_.SetRegister(ra, rb);
    cs_.builder_.CreateBr(cs_.blocks_[cs_.curr_ + 1]);
}

void Compiler::CompileCall() {
    int a = GETARG_A(cs_.instr_);
    int b = GETARG_B(cs_.instr_);
    if (b != 0)
        cs_.SetTop(a + b);
    auto args = {
        cs_.values_.state,
        cs_.GetValueR(a, "ra"),
        cs_.MakeInt(GETARG_C(cs_.instr_) - 1)
    };
    cs_.CreateCall("luaD_callnoyield", args);
    cs_.UpdateStack();
}

void Compiler::CompileTailcall() {
    // Tailcall returns a negative value that signals the call must be performed
    auto base = cs_.GetValueR(0, "base");
    if (cs_.proto_->sizep > 0)
        cs_.CreateCall("luaF_close", {cs_.values_.state, base});
    int a = GETARG_A(cs_.instr_);
    int b = GETARG_B(cs_.instr_);
    if (b != 0)
        cs_.SetTop(a + b);
    auto diff = cs_.TopDiff(a);
    auto ret = cs_.builder_.CreateNeg(diff, "ret");
    cs_.builder_.CreateRet(ret);
}

void Compiler::CompileReturn() {
    auto base = cs_.GetValueR(0, "base");
    if (cs_.proto_->sizep > 0)
        cs_.CreateCall("luaF_close", {cs_.values_.state, base});
    int a = GETARG_A(cs_.instr_);
    int b = GETARG_B(cs_.instr_);
    llvm::Value* nresults = nullptr;
    if (b == 0) {
        nresults = cs_.TopDiff(GETARG_A(cs_.instr_));
    } else if (b == 1) { 
        nresults = cs_.MakeInt(0);
    } else {
        nresults = cs_.MakeInt(b - 1);
        cs_.SetTop(a + b - 1);
    }
    cs_.builder_.CreateRet(nresults);
}

void Compiler::CompileForloop() {
    auto ra = cs_.GetValueR(GETARG_A(cs_.instr_), "ra");
    auto jump = cs_.ToBool(cs_.CreateCall("lll_forloop", {ra}, "jump"));
    auto jumpblock = cs_.blocks_[cs_.curr_ + 1 + GETARG_sBx(cs_.instr_)];
    cs_.builder_.CreateCondBr(jump, jumpblock, cs_.blocks_[cs_.curr_ + 1]);
}

void Compiler::CompileForprep() {
    auto args = {cs_.values_.state, cs_.GetValueR(GETARG_A(cs_.instr_), "ra")};
    cs_.CreateCall("lll_forprep", args);
    cs_.builder_.CreateBr(cs_.blocks_[cs_.curr_ + 1 + GETARG_sBx(cs_.instr_)]);
}

void Compiler::CompileTforcall() {
    int a = GETARG_A(cs_.instr_);
    int cb = a + 3;
    cs_.SetRegister(cs_.GetValueR(cb + 2, "cb2"), cs_.GetValueR(a + 2, "ra2"));
    cs_.SetRegister(cs_.GetValueR(cb + 1, "cb1"), cs_.GetValueR(a + 1, "ra1"));
    cs_.SetRegister(cs_.GetValueR(cb, "cb"), cs_.GetValueR(a, "ra"));
    cs_.SetTop(cb + 3);
    auto args = {
        cs_.values_.state,
        cs_.GetValueR(cb, "cb"),
        cs_.MakeInt(GETARG_C(cs_.instr_))
    };
    cs_.CreateCall("luaD_callnoyield", args);
    cs_.UpdateStack();
    cs_.ReloadTop();
}

void Compiler::CompileTforloop() {
    int a = GETARG_A(cs_.instr_);
    auto ra1 = cs_.GetValueR(a + 1, "ra1");
    auto tag = cs_.LoadField(ra1, cs_.rt_.MakeIntT(sizeof(int)),
            offsetof(TValue, tt_), "tag");
    auto notnil = cs_.builder_.CreateICmpNE(tag, cs_.MakeInt(LUA_TNIL),
            "notnil");
    auto continueblock = cs_.CreateSubBlock("continue");
    auto jmpblock = cs_.blocks_[cs_.curr_ + 1];
    cs_.builder_.CreateCondBr(notnil, continueblock, jmpblock);

    cs_.builder_.SetInsertPoint(continueblock);
    auto ra = cs_.GetValueR(a, "ra");
    cs_.SetRegister(ra, ra1);
    cs_.builder_.CreateBr(cs_.blocks_[cs_.curr_ + 1 + GETARG_sBx(cs_.instr_)]);
}

void Compiler::CompileSetlist() {

    int a = GETARG_A(cs_.instr_);
    int b = GETARG_B(cs_.instr_);
    int c = GETARG_C(cs_.instr_);
    if (c == 0)
        c = GETARG_Ax(cs_.proto_->code[cs_.curr_ + 1]);

    auto n = (b != 0 ? cs_.MakeInt(b) : cs_.TopDiff(a + 1));
    auto fields = cs_.MakeInt((c - 1) * LFIELDS_PER_FLUSH);

    auto args = {cs_.values_.state, cs_.GetValueR(a, "ra"), fields, n};
    cs_.CreateCall("lll_setlist", args);
    cs_.ReloadTop();
}

void Compiler::CompileClosure() {
    auto args = {
        cs_.values_.state,
        cs_.values_.closure,
        cs_.GetValueR(0, "base"),
        cs_.GetValueR(GETARG_A(cs_.instr_), "ra"),
        cs_.MakeInt(GETARG_Bx(cs_.instr_))
    };
    cs_.CreateCall("lll_closure", args);
    CompileCheckcg(cs_.GetValueR(GETARG_A(cs_.instr_) + 1, "ra1"));
}

void Compiler::CompileCheckcg(llvm::Value* reg) {
    auto args = {cs_.values_.state, cs_.values_.ci, reg};
    cs_.CreateCall("lll_checkcg", args);
}

}

