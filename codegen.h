#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Pass.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <stack>
#include <typeinfo>

using namespace llvm;


class NBlock;

static LLVMContext MyContext;

inline void printIR(Module *module) {
  /* Print the bytecode in a human-readable format to see if our program compiled properly */
  auto pass = createPrintModulePass(outs());
  pass->runOnModule(*module);
}

class CodeGenBlock {
  public:
  BasicBlock *block;
  Value *returnValue;
  std::map<std::string, Value *> locals;
};

class CodeGenContext {
  std::stack<CodeGenBlock *> blocks;
  Function *mainFunction;

  public:
  std::unique_ptr<IRBuilder<>> Builder;
  Module *module;
  CodeGenContext() {
    module = new Module("main", MyContext);
    Builder = std::make_unique<IRBuilder<>>(MyContext);
  }

  void generateCode(NBlock &root, std::string bcFile);
  GenericValue runCode();
  std::map<std::string, Value *> &locals() { return blocks.top()->locals; }
  BasicBlock *currentBlock() { return blocks.top()->block; }
  void pushBlock(BasicBlock *block) {
    blocks.push(new CodeGenBlock());
    blocks.top()->returnValue = NULL;
    blocks.top()->block = block;
  }
  void popBlock() {
    CodeGenBlock *top = blocks.top();
    blocks.pop();
    delete top;
  }
  void setCurrentReturnValue(Value *value) {
    blocks.top()->returnValue = value;
  }
  Value *getCurrentReturnValue() { return blocks.top()->returnValue; }
};

void createCoreFunctions(CodeGenContext &context);
