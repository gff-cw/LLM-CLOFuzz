/*
  Copyright 2015 Google LLC All rights reserved.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at:

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

/*
   american fuzzy lop - LLVM-mode instrumentation pass
   ---------------------------------------------------

   Written by Laszlo Szekeres <lszekeres@google.com> and
              Michal Zalewski <lcamtuf@google.com>

   LLVM integration design comes from Laszlo Szekeres. C bits copied-and-pasted
   from afl-as.c are Michal's fault.

   This library is plugged into LLVM when invoking clang through afl-clang-fast.
   It tells the compiler to add code roughly equivalent to the bits discussed
   in ../afl-as.h.
*/

#define AFL_LLVM_PASS

#include "../config.h"
#include "../debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include <map>
#include <optional>
#include <limits>

#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/IR/DataLayout.h"

#include <cxxabi.h>

#include "state-tracer.h"

using namespace llvm;

namespace {

/* A custom entry keeps the original name[:argument] syntax. The optional third
   field supplies a byte size when opaque IR genuinely carries no struct type. */
struct CustomTrace {
  int argument = -1;
  std::optional<uint64_t> size;
};

static void parseCustom(const char *text, std::map<std::string, CustomTrace> &out) {
  if (!text) return;
  SmallVector<StringRef, 8> entries;
  StringRef(text).split(entries, ';', -1, false);
  for (StringRef entry : entries) {
    SmallVector<StringRef, 3> fields;
    entry.split(fields, ':');
    CustomTrace config;
    if (fields.size() > 3 || fields[0].empty())
      FATAL("Invalid custom trace entry '%s'", entry.str().c_str());
    if (fields.size() >= 2 && !fields[1].empty()) {
      int position;
      if (fields[1].getAsInteger(10, position) || position < -1)
        FATAL("Invalid custom trace argument in '%s'", entry.str().c_str());
      config.argument = position;
    }
    if (fields.size() == 3) {
      uint64_t size;
      if (config.argument < 0 || fields[2].getAsInteger(10, size) || !size)
        FATAL("Invalid explicit custom trace size in '%s'", entry.str().c_str());
      config.size = size;
    }
    out.insert({fields[0].str(), config});
    OKF("INSTRUMENTING CUSTOM FUNCTION '%s'", entry.str().c_str());
  }
}

/* Recover only evidence of a struct object, never sizeof(pointer). At -O0 a
   formal pointer is often spilled and reloaded before its typed GEP use. */
static void collectStructTypes(Value *value, SmallPtrSetImpl<Value *> &seen,
                               SmallPtrSetImpl<StructType *> &types,
                               bool followUses) {
  if (!seen.insert(value).second) return;
  if (auto *alloc = dyn_cast<AllocaInst>(value)) {
    if (auto *type = dyn_cast<StructType>(alloc->getAllocatedType()))
      types.insert(type);
  } else if (auto *global = dyn_cast<GlobalVariable>(value)) {
    if (auto *type = dyn_cast<StructType>(global->getValueType()))
      types.insert(type);
  } else if (auto *gep = dyn_cast<GEPOperator>(value)) {
    if (auto *type = dyn_cast<StructType>(gep->getResultElementType()))
      types.insert(type);
  } else if (auto *load = dyn_cast<LoadInst>(value)) {
    Value *slot = load->getPointerOperand()->stripPointerCasts();
    for (User *user : slot->users())
      if (auto *store = dyn_cast<StoreInst>(user))
        if (store->getPointerOperand()->stripPointerCasts() == slot)
          collectStructTypes(store->getValueOperand(), seen, types, followUses);
  } else if (auto *phi = dyn_cast<PHINode>(value)) {
    for (Value *input : phi->incoming_values())
      collectStructTypes(input, seen, types, followUses);
  } else if (auto *select = dyn_cast<SelectInst>(value)) {
    collectStructTypes(select->getTrueValue(), seen, types, followUses);
    collectStructTypes(select->getFalseValue(), seen, types, followUses);
  } else if (auto *cast = dyn_cast<CastInst>(value)) {
    collectStructTypes(cast->getOperand(0), seen, types, followUses);
  }
  if (!followUses) return;
  for (User *user : value->users()) {
    if (auto *gep = dyn_cast<GetElementPtrInst>(user)) {
      if (gep->getPointerOperand() == value)
        if (auto *type = dyn_cast<StructType>(gep->getSourceElementType()))
          types.insert(type);
    } else if (auto *store = dyn_cast<StoreInst>(user)) {
      if (store->getValueOperand() == value)
        for (User *slotUser : store->getPointerOperand()->users())
          if (auto *load = dyn_cast<LoadInst>(slotUser))
            if (load->getType()->isPointerTy())
              collectStructTypes(load, seen, types, true);
    } else if (isa<CastInst>(user) || isa<PHINode>(user) || isa<SelectInst>(user)) {
      collectStructTypes(cast<Value>(user), seen, types, true);
    }
  }
}

static std::optional<uint64_t> debugTypeSize(DIType *type) {
  /* Strip qualifiers and at most one pointer/reference. A pointer-to-pointer
     must not be mistaken for a pointer to a struct in debug-only recovery. */
  unsigned indirections = 0;
  while (auto *derived = dyn_cast_or_null<DIDerivedType>(type)) {
    unsigned tag = derived->getTag();
    if (tag == dwarf::DW_TAG_pointer_type || tag == dwarf::DW_TAG_reference_type ||
        tag == dwarf::DW_TAG_rvalue_reference_type)
      if (++indirections > 1) return std::nullopt;
    type = derived->getBaseType();
  }
  auto *structure = dyn_cast_or_null<DICompositeType>(type);
  if (!structure || (structure->getTag() != dwarf::DW_TAG_structure_type &&
                     structure->getTag() != dwarf::DW_TAG_class_type &&
                     structure->getTag() != dwarf::DW_TAG_union_type) ||
      !structure->getSizeInBits() || structure->getSizeInBits() % 8)
    return std::nullopt;
  return structure->getSizeInBits() / 8;
}

/* index 0 is the return type; formal argument n has debug type index n+1. */
static std::optional<uint64_t> debugFunctionSize(Function &function, unsigned index) {
  auto *subprogram = function.getSubprogram();
  if (!subprogram || !subprogram->getType()) return std::nullopt;
  auto parameters = subprogram->getType()->getTypeArray();
  if (index >= parameters.size()) return std::nullopt;
  return debugTypeSize(dyn_cast_or_null<DIType>(parameters[index]));
}

static std::optional<uint64_t> debugValueSize(Value *value,
                                             SmallPtrSetImpl<Value *> &seen) {
  if (!seen.insert(value).second) return std::nullopt;
  SmallVector<DbgVariableIntrinsic *, 4> debugUsers;
  findDbgUsers(debugUsers, value);
  for (DbgVariableIntrinsic *debug : debugUsers)
    if (auto size = debugTypeSize(debug->getVariable()->getType())) return size;
  if (auto *argument = dyn_cast<Argument>(value))
    return debugFunctionSize(*argument->getParent(), argument->getArgNo() + 1);
  if (auto *load = dyn_cast<LoadInst>(value))
    return debugValueSize(load->getPointerOperand(), seen);
  if (auto *cast = dyn_cast<CastInst>(value))
    return debugValueSize(cast->getOperand(0), seen);
  if (auto *call = dyn_cast<CallBase>(value))
    if (Function *callee = call->getCalledFunction())
      return debugFunctionSize(*callee, 0);
  return std::nullopt;
}

static std::optional<uint64_t> customStructSize(Module &module, CallBase &call,
                                               Function &callee, unsigned arg) {
  /* The original syntax describes the callee's formal struct parameter. Debug
     signatures preserve that information even when a caller explicitly casts a
     different backing object. They are stronger evidence than caller storage. */
  if (auto size = debugFunctionSize(callee, arg + 1)) return size;
  SmallPtrSet<Value *, 32> seen;
  SmallPtrSet<StructType *, 4> types;
  collectStructTypes(call.getArgOperand(arg), seen, types, true);
  if (arg < callee.arg_size()) {
    collectStructTypes(callee.getArg(arg), seen, types, true);
    if (auto *type = dyn_cast_or_null<StructType>(callee.getParamByValType(arg)))
      types.insert(type);
    if (auto *type = dyn_cast_or_null<StructType>(callee.getParamStructRetType(arg)))
      types.insert(type);
  }
  std::optional<uint64_t> result;
  for (StructType *type : types) {
    if (!type->isSized()) continue;
    TypeSize size = module.getDataLayout().getTypeAllocSize(type);
    if (size.isScalable())
      FATAL("Scalable custom struct size for '%s'", callee.getName().str().c_str());
    uint64_t bytes = size.getFixedValue();
    if (result && *result != bytes)
      FATAL("Ambiguous custom struct type for '%s'; supply name:argument:bytes",
            callee.getName().str().c_str());
    result = bytes;
  }
  if (!result) {
    SmallPtrSet<Value *, 16> debugSeen;
    result = debugValueSize(call.getArgOperand(arg), debugSeen);
  }
  return result;
}

static FunctionCallee traceFunction(Module &module, StringRef name,
                                    FunctionType *type) {
  if (Function *existing = module.getFunction(name))
    if (existing->getFunctionType() != type)
      FATAL("Incompatible runtime declaration '%s'", name.str().c_str());
  return module.getOrInsertFunction(name, type);
}

static void setAfterCall(IRBuilder<> &builder, CallBase *call) {
  if (auto *invoke = dyn_cast<InvokeInst>(call)) {
    BasicBlock *edge = SplitEdge(invoke->getParent(), invoke->getNormalDest());
    if (!edge) FATAL("Unable to split successful allocation invoke edge");
    builder.SetInsertPoint(&*edge->getFirstInsertionPt());
  } else {
    auto *normal = cast<CallInst>(call);
    if (normal->isMustTailCall())
      FATAL("Cannot append allocation tracing to a musttail call");
    builder.SetInsertPoint(normal->getNextNode());
  }
}

static bool instrumentModule(Module &M) {
  /* Also protects explicit opt pipelines and recompilation of instrumented IR. */
  if (M.getNamedMetadata("stateafl.instrumented")) return false;
  M.getOrInsertNamedMetadata("stateafl.instrumented")
      ->addOperand(MDNode::get(M.getContext(), {}));

  LLVMContext &C = M.getContext();

  IntegerType *Int8Ty  = IntegerType::getInt8Ty(C);
  IntegerType *Int32Ty = IntegerType::getInt32Ty(C);
  IntegerType *SizeTy = M.getDataLayout().getIntPtrType(C);

  /* Show a banner */

  char be_quiet = 0;

  if (isatty(2) && !getenv("AFL_QUIET")) {

    SAYF(cCYA "afl-llvm-pass " cBRI VERSION cRST " by <lszekeres@google.com>\n");

  } else be_quiet = 1;

  /* Decide instrumentation ratio */

  char* inst_ratio_str = getenv("AFL_INST_RATIO");
  unsigned int inst_ratio = 100;

  if (inst_ratio_str) {

    if (sscanf(inst_ratio_str, "%u", &inst_ratio) != 1 || !inst_ratio ||
        inst_ratio > 100)
      FATAL("Bad value of AFL_INST_RATIO (must be between 1 and 100)");

  }

  /* Get globals for the SHM region and the previous location. Note that
     __afl_prev_loc is thread-local. */

  GlobalVariable *AFLMapPtr =
      new GlobalVariable(M, PointerType::get(C, 0), false,
                         GlobalValue::ExternalLinkage, 0, "__afl_area_ptr");

  GlobalVariable *AFLPrevLoc = new GlobalVariable(
      M, Int32Ty, false, GlobalValue::ExternalLinkage, 0, "__afl_prev_loc",
      0, GlobalVariable::GeneralDynamicTLSModel, 0, false);

  /* Get list of user-defined primitives for sending/receiving data */

  std::map<std::string, CustomTrace> map_custom_receive;
  std::map<std::string, CustomTrace> map_custom_send;
  parseCustom(getenv("TRACE_CUSTOM_RECEIVE"), map_custom_receive);
  parseCustom(getenv("TRACE_CUSTOM_SEND"), map_custom_send);

  /* Instrument all the things! */

  int inst_blocks = 0;

  for (auto &F : M) {

    SmallVector<CallBase*, 16> stack_lookups;
    SmallVector<CallBase*, 32> original_calls;
  
    for (auto &BB : F) {

      BasicBlock::iterator IP = BB.getFirstInsertionPt();
      if (IP != BB.end() && AFL_R(100) < inst_ratio) {
        IRBuilder<> IRB(&(*IP));

        /* Make up cur_loc */

        unsigned int cur_loc = AFL_R(MAP_SIZE);

        ConstantInt *CurLoc = ConstantInt::get(Int32Ty, cur_loc);

        /* Load prev_loc */

        LoadInst *PrevLoc = IRB.CreateLoad(Int32Ty, AFLPrevLoc);
        PrevLoc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, {}));
        Value *PrevLocCasted = IRB.CreateZExt(PrevLoc, IRB.getInt32Ty());

        /* Load SHM pointer */

        LoadInst *MapPtr = IRB.CreateLoad(PointerType::get(C, 0), AFLMapPtr);
        MapPtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, {}));
        Value *MapPtrIdx =
            IRB.CreateGEP(Int8Ty, MapPtr, IRB.CreateXor(PrevLocCasted, CurLoc));

        /* Update bitmap */

        LoadInst *Counter = IRB.CreateLoad(Int8Ty, MapPtrIdx);
        Counter->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, {}));
        Value *Incr = IRB.CreateAdd(Counter, ConstantInt::get(Int8Ty, 1));
        IRB.CreateStore(Incr, MapPtrIdx)
            ->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, {}));

        /* Set prev_loc to cur_loc >> 1 */

        StoreInst *Store =
            IRB.CreateStore(ConstantInt::get(Int32Ty, cur_loc >> 1), AFLPrevLoc);
        Store->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, {}));

        inst_blocks++;
      }

      for (Instruction &instruction : BB)
        if (auto *call = dyn_cast<CallBase>(&instruction))
          original_calls.push_back(call);
    }

    /* Walk only original calls: inserted helpers must never instrument themselves,
       and invoke edge splitting must not invalidate the basic-block traversal. */
    for (CallBase *call_instr : original_calls) {
        auto called_func = dyn_cast<Function>(call_instr->getCalledOperand()->stripPointerCasts());
        if (called_func == nullptr)
          continue;


        /* Clang may ABI-coerce an oversized scalar into several IR arguments.
           Reject that layout rather than accidentally tracing only its low half. */
        int expectedArguments = StringSwitch<int>(called_func->getName())
            .Cases("malloc", "free", "close", "fclose", "_exit", 1)
            .Cases("calloc", "realloc", 2)
            .Cases("read", "write", "recvmsg", "sendmsg", "fgets", 3)
            .Cases("recv", "send", "fread", "fwrite", 4)
            .Cases("recvfrom", "sendto", 6).Default(-1);
        if (expectedArguments >= 0 && call_instr->arg_size() != unsigned(expectedArguments))
          FATAL("Unsupported argument layout for '%s'; cannot represent its byte count in the runtime ABI",
                called_func->getName().str().c_str());

        /* Older supplied fixtures use int-sized recv/send stubs. Widen their
           nonnegative byte counts explicitly; never hide an oversized count by
           truncating it to the target runtime ABI. */
        auto byteCount = [&](unsigned position) -> Value * {
          Value *value = call_instr->getArgOperand(position);
          auto *integer = dyn_cast<IntegerType>(value->getType());
          if (!integer || integer->getBitWidth() > SizeTy->getBitWidth())
            FATAL("Byte count for '%s' cannot be represented by target size_t",
                  called_func->getName().str().c_str());
          IRBuilder<> builder(call_instr);
          return builder.CreateZExt(value, SizeTy);
        };

        std::string demangled;
        int demangled_status;
        char * demangled_char = abi::__cxa_demangle(called_func->getName().data(), nullptr, nullptr, &demangled_status);

        if(demangled_status == 0) {
          demangled = demangled_char;
        }

        free(demangled_char);

        if ( called_func->getName().starts_with("llvm.lifetime") ) {
          OKF("FUNCTION LLVM: %s", called_func->getName().data() );

          stack_lookups.push_back(call_instr);
        }
        else
        if ( called_func->getName() == "malloc" || demangled.find("operator new") != std::string::npos ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          if(demangled_status==0)
            OKF("DEMANGLED FUNCTION: %s\n", demangled.c_str());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0);

          auto * helperTy = FunctionType::get(voidTy, {int8PtrTy, Type::getInt64Ty(C)}, false);
          auto helper_malloc = traceFunction(M, "new_heap_alloc_record", helperTy);

          IRBuilder<> builder(call_instr);
          setAfterCall(builder, call_instr);
          builder.CreateCall(helper_malloc, {call_instr, builder.CreateZExt(byteCount(0), Type::getInt64Ty(C))});

        }
        if ( called_func->getName() == "calloc" ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0);

          auto * helperTy = FunctionType::get(voidTy, {int8PtrTy, SizeTy, SizeTy}, false);
          auto helper_malloc = traceFunction(M, "trace_calloc", helperTy);

          IRBuilder<> builder(call_instr);
          setAfterCall(builder, call_instr);
          builder.CreateCall(helper_malloc, {call_instr, byteCount(1), byteCount(0)});

        }
        if ( called_func->getName() == "realloc" ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0);

          auto * helperTy = FunctionType::get(voidTy, {int8PtrTy, SizeTy, int8PtrTy}, false);
          auto helper_malloc = traceFunction(M, "trace_realloc", helperTy);

          IRBuilder<> builder(call_instr);
          setAfterCall(builder, call_instr);
          builder.CreateCall(helper_malloc, {call_instr, byteCount(1), call_instr->getArgOperand(0)});

        }
        else if ( called_func->getName() == "free" || demangled.find("operator delete") != std::string::npos ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          if(demangled_status==0)
            OKF("DEMANGLED FUNCTION: %s\n", demangled.c_str());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0);

          auto * helperTy = FunctionType::get(voidTy, {int8PtrTy, Type::getInt64Ty(C)}, false);
          auto helper_free = traceFunction(M, "free_heap_alloc_record", helperTy);

          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper_free, {call_instr->getArgOperand(0), ConstantInt::get(Type::getInt64Ty(C), 0)});

        }
        else if(  called_func->getName() == "recv"
               || called_func->getName() == "recvfrom" )
        {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0); /* pass buffer address */

          auto * helperTy = FunctionType::get(voidTy, {int8PtrTy, SizeTy}, false);
          auto helper = traceFunction(M, "trace_receive", helperTy);

          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper, {call_instr->getArgOperand(1), byteCount(2)});

        }
        else if (called_func->getName() == "recvmsg") {
          auto *helperTy = FunctionType::get(Type::getVoidTy(C), {PointerType::get(C, 0)}, false);
          auto helper = traceFunction(M, "trace_recvmsg", helperTy);
          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper, {call_instr->getArgOperand(1)});
        }
        else if(  called_func->getName() == "send" 
               || called_func->getName() == "sendto" )
        {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0); /* pass buffer address */

          auto * helperTy = FunctionType::get(voidTy, {int8PtrTy, SizeTy}, false);
          auto helper = traceFunction(M, "trace_send", helperTy);

          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper, {call_instr->getArgOperand(1), byteCount(2)});

        }
        else if (called_func->getName() == "sendmsg") {
          auto *helperTy = FunctionType::get(Type::getVoidTy(C), {PointerType::get(C, 0)}, false);
          auto helper = traceFunction(M, "trace_sendmsg", helperTy);
          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper, {call_instr->getArgOperand(1)});
        }
        else if (map_custom_receive.count(called_func->getName().str()) ||
                 map_custom_send.count(called_func->getName().str())) {
          bool receive = map_custom_receive.count(called_func->getName().str());
          const CustomTrace &config = receive
              ? map_custom_receive.at(called_func->getName().str())
              : map_custom_send.at(called_func->getName().str());
          auto *pointerTy = PointerType::get(C, 0);
          Value *buffer = ConstantPointerNull::get(pointerTy);
          uint64_t size = 0;
          if (config.argument >= 0) {
            unsigned position = config.argument;
            if (position >= call_instr->arg_size())
              FATAL("Custom function '%s' argument %u is out of range",
                    called_func->getName().str().c_str(), position);
            buffer = call_instr->getArgOperand(position);
            if (!buffer->getType()->isPointerTy())
              FATAL("Custom function '%s' argument %u is not a pointer",
                    called_func->getName().str().c_str(), position);
            auto inferred = config.size ? config.size
                : customStructSize(M, *call_instr, *called_func, position);
            if (!inferred)
              FATAL("Cannot recover struct type for custom function '%s' argument %u; "
                    "use name:argument:bytes or retain debug/type-bearing IR",
                    called_func->getName().str().c_str(), position);
            size = *inferred;
            if (SizeTy->getBitWidth() < 64 && size > UINT32_MAX)
              FATAL("Custom buffer size exceeds target size_t for '%s'",
                    called_func->getName().str().c_str());
          } else {
            WARNF("NO BUFFER POINTER PROVIDED FOR CUSTOM FUNCTION %s (PASSING NULL, size = 0)",
                  called_func->getName().str().c_str());
          }
          auto helper = traceFunction(M, receive ? "trace_receive" : "trace_send",
              FunctionType::get(Type::getVoidTy(C), {pointerTy, SizeTy}, false));
          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper, {buffer, ConstantInt::get(SizeTy, size)});
        }
        else if( called_func->getName() == "read" ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int32Ty = Type::getInt32Ty(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0); /* pass buffer address */

          auto * helperTy = FunctionType::get(voidTy, {int32Ty, int8PtrTy, SizeTy}, false);
          auto helper_read = traceFunction(M, "trace_read", helperTy);

          /* Preserve the target size_t width, including 32-bit IR. */
          std::vector<Value *> call_args = {call_instr->getArgOperand(0), call_instr->getArgOperand(1), byteCount(2)};
          
          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper_read, call_args);

        }
        else if( called_func->getName() == "write" ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int32Ty = Type::getInt32Ty(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0); /* pass buffer address */

          auto * helperTy = FunctionType::get(voidTy, {int32Ty, int8PtrTy, SizeTy}, false);
          auto helper_write = traceFunction(M, "trace_write", helperTy);

          std::vector<Value *> call_args = {call_instr->getArgOperand(0), call_instr->getArgOperand(1), byteCount(2)};

          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper_write, call_args);

        }
        else if( called_func->getName() == "fprintf" ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0);
          auto * int32Ty = Type::getInt32Ty(M.getContext()); /* pass buffer size */

          auto * helperTy = FunctionType::get(voidTy, { int8PtrTy, int8PtrTy, int32Ty }, false);
          auto helper = traceFunction(M, "trace_fprintf", helperTy);

          std::vector<Value *> call_args = {call_instr->getArgOperand(0), call_instr->getArgOperand(1), call_instr};

          IRBuilder<> builder(call_instr);
          setAfterCall(builder, call_instr);
          builder.CreateCall(helper, call_args);

        }
        else if( called_func->getName() == "fgets" ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0);
          auto * int32Ty = Type::getInt32Ty(M.getContext()); /* pass buffer size */

          auto * helperTy = FunctionType::get(voidTy, { int8PtrTy, int8PtrTy, int32Ty }, false);
          auto helper = traceFunction(M, "trace_fgets", helperTy);

          std::vector<Value *> call_args = {call_instr->getArgOperand(2), call_instr->getArgOperand(0), call_instr->getArgOperand(1)};

          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper, call_args);

        }
        else if( called_func->getName() == "fwrite" ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0);

          auto * helperTy = FunctionType::get(voidTy, { int8PtrTy, int8PtrTy, SizeTy, SizeTy }, false);
          auto helper = traceFunction(M, "trace_fwrite", helperTy);

          std::vector<Value *> call_args = {call_instr->getArgOperand(3), call_instr->getArgOperand(0), byteCount(1), byteCount(2)};

          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper, call_args);

        }
        else if( called_func->getName() == "fread" ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0);

          auto * helperTy = FunctionType::get(voidTy, { int8PtrTy, int8PtrTy, SizeTy, SizeTy }, false);
          auto helper = traceFunction(M, "trace_fread", helperTy);

          std::vector<Value *> call_args = {call_instr->getArgOperand(3), call_instr->getArgOperand(0), byteCount(1), byteCount(2)};

          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper, call_args);

        }
        else if( called_func->getName() == "_exit" ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());

          auto * helperTy = FunctionType::get(voidTy, false);
          auto helper = traceFunction(M, "end_state_tracer", helperTy);

          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper, {});

        }
        else if( called_func->getName() == "close" ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int32Ty = Type::getInt32Ty(M.getContext());

          auto * helperTy = FunctionType::get(voidTy, {int32Ty}, false);
          auto helper_read = traceFunction(M, "trace_close", helperTy);

          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper_read, {call_instr->getArgOperand(0)});

        }
        else if( called_func->getName() == "fclose" ) {
          OKF("Found function call: %s\n", called_func->getName().data());

          auto * voidTy = Type::getVoidTy(M.getContext());
          auto * int8PtrTy = PointerType::get(C, 0);

          auto * helperTy = FunctionType::get(voidTy, {int8PtrTy}, false);
          auto helper = traceFunction(M, "trace_fclose", helperTy);

          IRBuilder<> builder(call_instr);
          builder.CreateCall(helper, {call_instr->getArgOperand(0)});

        }


    }

    for (SmallVectorImpl<CallBase*>::iterator it=stack_lookups.begin(),
          e=stack_lookups.end() ; e!=it ; it++) {

      CallBase * call_instr = *it;
      auto called_func = dyn_cast<Function>(call_instr->getCalledOperand()->stripPointerCasts());

      auto * voidTy = Type::getVoidTy(M.getContext());
      auto * int8PtrTy = PointerType::get(C, 0);
      auto * int64Ty = Type::getInt64Ty(M.getContext());


      auto * helperTy = FunctionType::get(voidTy, {int8PtrTy, int64Ty}, false);

      IRBuilder<> builder(call_instr);

      // test if allocated stack size is greater or equal than threshold
      auto stack_size_cmp = builder.CreateICmpUGE(call_instr->getArgOperand(0), ConstantInt::get(int64Ty, MIN_STACK_ALLOC_SIZE));
      auto split_before = SplitBlockAndInsertIfThen(stack_size_cmp, call_instr, false);
      builder.SetInsertPoint(split_before);

      if( called_func->getName().find("start") != StringRef::npos ) {

        auto helper_malloc = traceFunction(M, "new_stack_alloc_record", helperTy);
        builder.CreateCall(helper_malloc, {call_instr->getArgOperand(1), call_instr->getArgOperand(0)});

      } else {

        auto helper_malloc = traceFunction(M, "free_stack_alloc_record", helperTy);
        builder.CreateCall(helper_malloc, {call_instr->getArgOperand(1), call_instr->getArgOperand(0)});
      }

    }
  }

  /* Say something nice. */

  if (!be_quiet) {

    if (!inst_blocks) WARNF("No instrumentation targets found.");
    else OKF("Instrumented %u locations (%s mode, ratio %u%%).",
             inst_blocks, getenv("AFL_HARDEN") ? "hardened" :
             ((getenv("AFL_USE_ASAN") || getenv("AFL_USE_MSAN")) ?
              "ASAN/MSAN" : "non-hardened"), inst_ratio);

  }

  return true;

}


class AFLCoverage : public PassInfoMixin<AFLCoverage> {
 public:
  PreservedAnalyses run(Module &module, ModuleAnalysisManager &) {
    return instrumentModule(module) ? PreservedAnalyses::none()
                                    : PreservedAnalyses::all();
  }
  static bool isRequired() { return true; }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "StateAFL", LLVM_VERSION_STRING,
          [](PassBuilder &builder) {
            /* Earlier than SROA/inlining: retain allocations and lifetime events
               at O0 as well as O1-O3. One callback is sufficient at every level. */
            builder.registerPipelineStartEPCallback(
                [](ModulePassManager &manager, OptimizationLevel) {
                  manager.addPass(AFLCoverage());
                });
            builder.registerPipelineParsingCallback(
                [](StringRef name, ModulePassManager &manager,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (name != "stateafl") return false;
                  manager.addPass(AFLCoverage());
                  return true;
                });
          }};
}
