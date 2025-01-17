#include "codegen.h"
#include "node.h"
#include "parser.hpp"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Value.h"
#include <iostream>
#include <llvm/IR/Instructions.h>
#include <llvm/IRPrinter/IRPrintingPasses.h>

using namespace std;

/* Compile the AST into a module */
void CodeGenContext::generateCode(NBlock &root, std::string bcFile) {
  std::cout << "Generating code...\n";

  /* Create the top level interpreter function to call as entry */
  vector<Type *> argTypes;
  FunctionType *ftype =
      FunctionType::get(Type::getInt32Ty(MyContext), argTypes, false);
  mainFunction =
      Function::Create(ftype, GlobalValue::ExternalLinkage, "main", module);

  std::cout << "name2: " << mainFunction->getName().str() << endl;
  BasicBlock *bblock = BasicBlock::Create(MyContext, "entry", mainFunction, 0);
  Builder->SetInsertPoint(bblock);

  /* Push a new variable/block context */
  pushBlock(bblock);
  root.codeGen(*this); /* emit bytecode for the toplevel block */

  Builder->CreateRet(ConstantInt::get(Type::getInt32Ty(MyContext), 0));
  popBlock();

  std::cout << "Code is generated.\n";
  printIR(module);
}

/* Executes the AST by running the main function */
GenericValue CodeGenContext::runCode() {
  std::cout << "Running code...\n";
  ExecutionEngine *ee = EngineBuilder(unique_ptr<Module>(module)).create();
  ee->finalizeObject();
  vector<GenericValue> noargs;
  GenericValue v = ee->runFunction(mainFunction, noargs);
  std::cout << "Code was run.\n";
  return v;
}

/* Returns an LLVM type based on the identifier */
static Type *typeOf(const NIdentifier &type) {
  if (type.name.compare("int") == 0) {
    return Type::getInt64Ty(MyContext);
  } else if (type.name.compare("double") == 0) {
    return Type::getDoubleTy(MyContext);
  }
  return Type::getVoidTy(MyContext);
}

/* -- Code Generation -- */

Value *NInteger::codeGen(CodeGenContext &context) {
  std::cout << "Creating integer: " << value << endl;
  return ConstantInt::get(Type::getInt64Ty(MyContext), value, true);
}

Value *NDouble::codeGen(CodeGenContext &context) {
  std::cout << "Creating double: " << value << endl;
  return ConstantFP::get(Type::getDoubleTy(MyContext), value);
}

Value *NIdentifier::codeGen(CodeGenContext &context) {
  std::cout << "Creating identifier reference: " << name << endl;
  if (context.locals().find(name) == context.locals().end()) {
    std::cerr << "undeclared variable " << name << endl;
    return NULL;
  }
  return context.Builder->CreateLoad(llvm::Type::getInt64Ty(MyContext),
                                     context.locals()[name], "");
}

Value *NMethodCall::codeGen(CodeGenContext &context) {
  Function *function = context.module->getFunction(id.name.c_str());
  if (function == NULL) { std::cerr << "no such function " << id.name << endl; }
  std::vector<Value *> args;
  ExpressionList::const_iterator it;
  for (it = arguments.begin(); it != arguments.end(); it++) {
    args.push_back((**it).codeGen(context));
  }
  auto call = context.Builder->CreateCall(function, args, "");
  std::cout << "Creating method call: " << id.name << endl;
  return call;
}

Value *NBinaryOperator::codeGen(CodeGenContext &context) {
  std::cout << "Creating binary operation " << op << endl;
  Instruction::BinaryOps instr;
  switch (op) {
    case TPLUS:
      return context.Builder->CreateAdd(lhs.codeGen(context),
                                        rhs.codeGen(context), "addtmp");
    case TMINUS:
      return context.Builder->CreateSub(lhs.codeGen(context),
                                        rhs.codeGen(context), "subtmp");
    case TMUL:
      return context.Builder->CreateMul(lhs.codeGen(context),
                                        rhs.codeGen(context), "multmp");
    case TDIV:
      return context.Builder->CreateSDiv(lhs.codeGen(context),
                                         rhs.codeGen(context), "idivtmp");
      /* TODO comparison */
  }
  return nullptr;
}

Value *NAssignment::codeGen(CodeGenContext &context) {
  std::cout << "Creating assignment for " << lhs.name << endl;
  if (context.locals().find(lhs.name) == context.locals().end()) {
    std::cerr << "undeclared variable " << lhs.name << endl;
    return NULL;
  }
  return context.Builder->CreateStore(rhs.codeGen(context),
                                      context.locals()[lhs.name]);
}

Value *NBlock::codeGen(CodeGenContext &context) {
  StatementList::const_iterator it;
  Value *last = NULL;
  for (it = statements.begin(); it != statements.end(); it++) {
    auto &statement = **it;
    std::cout << "Generating code for " << typeid(statement).name() << endl;
    last = (statement).codeGen(context);
  }
  std::cout << "Creating block" << endl;
  return last;
}

Value *NExpressionStatement::codeGen(CodeGenContext &context) {
  std::cout << "Generating code for " << typeid(expression).name() << endl;
  return expression.codeGen(context);
}

Value *NReturnStatement::codeGen(CodeGenContext &context) {
  std::cout << "Generating return code for " << typeid(expression).name()
            << endl;
  Value *returnValue = expression.codeGen(context);
  context.setCurrentReturnValue(returnValue);
  return returnValue;
}

Value *NVariableDeclaration::codeGen(CodeGenContext &context) {
  std::cout << "Creating variable declaration " << type.name << " " << id.name
            << endl;
  auto alloc =
      context.Builder->CreateAlloca(typeOf(type), nullptr, id.name.c_str());
  context.locals()[id.name] = alloc;
  if (assignmentExpr != NULL) {
    NAssignment assn(id, *assignmentExpr);
    assn.codeGen(context);
  }
  return alloc;
}

Value *NExternDeclaration::codeGen(CodeGenContext &context) {
  vector<Type *> argTypes;
  VariableList::const_iterator it;
  for (it = arguments.begin(); it != arguments.end(); it++) {
    argTypes.push_back(typeOf((**it).type));
  }
  FunctionType *ftype = FunctionType::get(typeOf(type), argTypes, false);
  Function *function = Function::Create(ftype, GlobalValue::ExternalLinkage,
                                        id.name.c_str(), context.module);
  return function;
}

Value *NFunctionDeclaration::codeGen(CodeGenContext &context) {
  vector<Type *> argTypes;
  VariableList::const_iterator it;
  for (it = arguments.begin(); it != arguments.end(); it++)
    argTypes.push_back(typeOf((**it).type));

  FunctionType *ftype = FunctionType::get(typeOf(type), argTypes, false);
  Function *function = Function::Create(ftype, GlobalValue::InternalLinkage,
                                        id.name.c_str(), context.module);
  std::cout << "name: " << id.name << endl;
  BasicBlock *bblock = BasicBlock::Create(MyContext, "entry", function, 0);
  auto PreInsertBB = context.Builder->GetInsertBlock();
  context.Builder->SetInsertPoint(bblock);

  context.pushBlock(bblock);

  Function::arg_iterator argsValues = function->arg_begin();
  Value *argumentValue;

  for (it = arguments.begin(); it != arguments.end(); it++) {
    (**it).codeGen(context);

    argumentValue = &*argsValues++;
    argumentValue->setName((*it)->id.name.c_str());
    StoreInst *inst = context.Builder->CreateStore(
        argumentValue, context.locals()[(*it)->id.name]);
  }

  block.codeGen(context);

  context.Builder->CreateRet(context.getCurrentReturnValue());

  context.popBlock();
  std::cout << "Creating function: " << id.name << endl;

  context.Builder->SetInsertPoint(PreInsertBB);

  return function;
}

void NBranchStatement::setIFBlocks(IFBlockList &ifBlocks) {
  IFBlocks = ifBlocks;
}

void NBranchStatement::setElseBlock(NBlock *block) {
  ElseBlock = block;
}

Value *NBranchStatement::codeGen(CodeGenContext &context) {
  std::cout << "Creating branch" << endl;
  IFBlockList::const_iterator it;
  Value *CondV;
  BasicBlock *PreInsertBB = context.Builder->GetInsertBlock();
  Function *TheFunction = PreInsertBB->getParent();

  // Create block labels
  std::vector<BasicBlock *> IfBBs;
  std::vector<BasicBlock *> ThenBBs;
  auto Parent = TheFunction;
  for (auto &IFBlock : IFBlocks) {
    IfBBs.push_back(BasicBlock::Create(MyContext, "if"));
    ThenBBs.push_back(BasicBlock::Create(MyContext, "then"));
  }

  BasicBlock *ElseBB = BasicBlock::Create(MyContext, "else");
  BasicBlock *MergeBB = BasicBlock::Create(MyContext, "merge");

  for (int i = 0; i < IFBlocks.size(); i++) {
    NIFBlock *IFBlock = dynamic_cast<NIFBlock *>(IFBlocks[i]);
    NExpression &ConditionExpr = IFBlock->CondExpr;
    NBlock &ThenBlock = *IFBlocks[i];
    BasicBlock *IfBB = IfBBs[i];
    BasicBlock *ThenBB = ThenBBs[i];
    BasicBlock *ElseIfBB = nullptr;


    if (i + 1 < IFBlocks.size())
      ElseIfBB = IfBBs[i + 1]; // Goto next if some else-if blocks exist
    else if (ElseBlock)
      ElseIfBB = ElseBB; // Goto else block when no else-if blocks exist
    else
      ElseIfBB = MergeBB; // Goto merge block when no else block exists

    if (i) {
      TheFunction->insert(TheFunction->end(), IfBB);
      context.Builder->SetInsertPoint(IfBB);
    }

    CondV = ConditionExpr.codeGen(context);
    if (!CondV) return nullptr;


    CondV = context.Builder->CreateICmpEQ(
        CondV, ConstantInt::get(Type::getInt64Ty(MyContext), 0), "ifcond");

    context.Builder->CreateCondBr(CondV, ElseIfBB, ThenBB);

    TheFunction->insert(TheFunction->end(), ThenBB);
    context.Builder->SetInsertPoint(ThenBB);

    Value *ThenV = ThenBlock.codeGen(context);
    if (!ThenV) return nullptr;

    // Goto MergeBB when finish ThenBB
    context.Builder->CreateBr(MergeBB);
  }

  if (ElseBlock) {
    TheFunction->insert(TheFunction->end(), ElseBB);
    context.Builder->SetInsertPoint(ElseBB);

    Value *ElseV = ElseBlock->codeGen(context);
    if (!ElseV) return nullptr;

    // Goto MergeBB when finish ElseBB
    context.Builder->CreateBr(MergeBB);
  }

  // Emit merge block
  TheFunction->insert(TheFunction->end(), MergeBB);
  context.Builder->SetInsertPoint(MergeBB);

  std::cout << "Created branch" << endl;

  return nullptr;
}


llvm::Value *NWhileStatement::codeGen(CodeGenContext &context) {
  std::cout << "Creating while" << endl;

  Function *TheFunction = context.Builder->GetInsertBlock()->getParent();
  BasicBlock *CondBB = BasicBlock::Create(MyContext, "whilecond");
  BasicBlock *ThenBB = BasicBlock::Create(MyContext, "then");
  BasicBlock *MergeBB = BasicBlock::Create(MyContext, "merge");

  context.Builder->CreateBr(CondBB);
  TheFunction->insert(TheFunction->end(), CondBB);
  context.Builder->SetInsertPoint(CondBB);

  auto CondV = CondExpr.codeGen(context);
  if (!CondV) return nullptr;

  CondV = context.Builder->CreateICmpEQ(
      CondV, ConstantInt::get(Type::getInt64Ty(MyContext), 0), "whilecond");

  context.Builder->CreateCondBr(CondV, MergeBB, ThenBB);

  TheFunction->insert(TheFunction->end(), ThenBB);
  context.Builder->SetInsertPoint(ThenBB);

  Value *ThenV = ThenBlock.codeGen(context);
  if (!ThenV) return nullptr;

  // Back to CondBB
  context.Builder->CreateBr(CondBB);

  TheFunction->insert(TheFunction->end(), MergeBB);

  // Insert extra instructions to where from MergeBB
  context.Builder->SetInsertPoint(MergeBB);
  
  std::cout << "Created while" << endl;

  return nullptr;
}
