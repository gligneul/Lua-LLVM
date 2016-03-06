/*
** LLL - Lua Low Level
** September, 2015
** Author: Gabriel de Quadros Ligneul
** Copyright Notice for LLL: see lllcore.h
**
** lllcompilerstate.cpp
*/

#include <sstream>

#include <llvm/IR/Constant.h>
#include <llvm/Support/Host.h>

#include "lllcompilerstate.h"

extern "C" {
#include "lprefix.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lstate.h"
}

namespace lll {

CompilerState::CompilerState(lua_State* L, Proto* proto) :
    L_(L),
    proto_(proto),
    context_(llvm::getGlobalContext()),
    rt_(*Runtime::Instance()),
    module_(new llvm::Module("lll_module", context_)),
    function_(CreateMainFunction()),
    blocks_(proto_->sizecode, nullptr),
    builder_(context_),
    curr_(0) {
    module_->setTargetTriple(llvm::sys::getDefaultTargetTriple());
    CreateBlocks();
}

llvm::Function* CompilerState::CreateMainFunction() {
    auto ret = rt_.MakeIntT(sizeof(int));
    auto params = {rt_.GetType("lua_State"), rt_.GetType("LClosure")};
    auto type = llvm::FunctionType::get(ret, params, false);
    std::stringstream name;
    name << "lll" << static_cast<void*>(proto_);
    return llvm::Function::Create(type, llvm::Function::ExternalLinkage,
            name.str(), module_.get());
}

void CompilerState::CreateBlocks() {
    auto& args = function_->getArgumentList();
    values_.state = &args.front();
    values_.state->setName("state");
    values_.closure = &args.back();
    values_.closure->setName("closure");

    auto entry = llvm::BasicBlock::Create(context_, "entry", function_);
    builder_.SetInsertPoint(entry);

    auto tci = rt_.GetType("CallInfo");
    values_.ci = LoadField(values_.state, tci, offsetof(lua_State, ci), "ci");

    auto tluanumber = rt_.GetType("lua_Number");
    values_.bnumber = builder_.CreateAlloca(tluanumber, nullptr, "bnumber");
    values_.cnumber = builder_.CreateAlloca(tluanumber, nullptr, "cnumber");

    auto ttvalue = rt_.GetType("TValue");
    values_.base = builder_.CreateAlloca(ttvalue, nullptr, "base");
    UpdateStack();

    for (size_t i = 0; i < blocks_.size(); ++i) {
        auto instruction = luaP_opnames[GET_OPCODE(proto_->code[i])];
        std::stringstream name;
        name << "block." << i << "." << instruction;
        blocks_[i] = llvm::BasicBlock::Create(context_, name.str(), function_);
    }

    builder_.CreateBr(blocks_[0]);
}

llvm::Value* CompilerState::MakeInt(int64_t value, llvm::Type* type) {
    if (!type)
        type = rt_.MakeIntT(sizeof(int));
    return llvm::ConstantInt::get(type, value);
}

llvm::Value* CompilerState::ToBool(llvm::Value* value) {
    return builder_.CreateICmpNE(value, MakeInt(0, value->getType()),
            value->getName() + ".bool");
}

llvm::Value* CompilerState::InjectPointer(llvm::Type* type, void* ptr) {
    auto intptrt = rt_.MakeIntT(sizeof(void*));
    auto intptr = llvm::ConstantInt::get(intptrt, (uintptr_t)ptr);
    return builder_.CreateIntToPtr(intptr, type);
}

llvm::Value* CompilerState::GetFieldPtr(llvm::Value* strukt,
        llvm::Type* fieldtype, size_t offset, const std::string& name) {
    auto memt = llvm::PointerType::get(rt_.MakeIntT(1), 0);
    auto mem = builder_.CreateBitCast(strukt, memt, strukt->getName() + "_mem");
    auto element = builder_.CreateGEP(mem, MakeInt(offset), name + "_mem");
    auto ptrtype = llvm::PointerType::get(fieldtype, 0);
    return builder_.CreateBitCast(element, ptrtype, name + "_ptr");
}

llvm::Value* CompilerState::LoadField(llvm::Value* strukt,
        llvm::Type* fieldtype, size_t offset, const std::string& name) {
    auto ptr = GetFieldPtr(strukt, fieldtype, offset, name);
    return builder_.CreateLoad(ptr, name);
}

void CompilerState::SetField(llvm::Value* strukt, llvm::Value* fieldvalue,
        size_t offset, const std::string& fieldname) {
    auto ptr = GetFieldPtr(strukt, fieldvalue->getType(), offset,fieldname);
    builder_.CreateStore(fieldvalue, ptr);
}

llvm::Value* CompilerState::CreateCall(const std::string& name,
        std::initializer_list<llvm::Value*> args, const std::string& retname) {
    auto f = rt_.GetFunction(module_.get(), name);
    return builder_.CreateCall(f, args, retname);
}

llvm::Value* CompilerState::GetBase() {
    return builder_.CreateLoad(values_.base);
}

void CompilerState::UpdateStack() {
    auto base = LoadField(values_.ci, rt_.GetType("TValue"),
            offsetof(CallInfo, u.l.base), "u.l.base");
    builder_.CreateStore(base, values_.base);
}

void CompilerState::ReloadTop() {
    auto top = LoadField(values_.ci, rt_.GetType("TValue"),
            offsetof(CallInfo, top), "top");
    SetField(values_.state, top, offsetof(lua_State, top), "top");
}

void CompilerState::SetTop(int reg) {
    auto top = GetValueR(reg, "top");
    SetField(values_.state, top, offsetof(lua_State, top), "top");
}

llvm::Value* CompilerState::TopDiff(int n) {
    auto top = LoadField(values_.state, rt_.GetType("TValue"),
            offsetof(lua_State, top), "top");
    auto r = GetValueR(n, "r");
    auto diff = builder_.CreatePtrDiff(top, r, "diff");
    auto inttype = rt_.MakeIntT(sizeof(int));
    return builder_.CreateIntCast(diff, inttype, false, "idiff");
}

llvm::BasicBlock* CompilerState::CreateSubBlock(const std::string& suffix,
            llvm::BasicBlock* preview) {
    if (!preview)
        preview = blocks_[curr_];
    auto name = blocks_[curr_]->getName() + "." +  suffix;
    auto block = llvm::BasicBlock::Create(context_, name, function_, preview);
    block->moveAfter(preview);
    return block;
}

}

