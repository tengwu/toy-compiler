#include "codegen.h"
#include "node.h"
#include "parser.hpp"
#include "llvm/IRPrinter/IRPrintingPasses.h"

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
      //      instr = Instruction::Add;
      //      goto math;
      return context.Builder->CreateAdd(lhs.codeGen(context),
                                        rhs.codeGen(context), "addtmp");
    case TMINUS:
      //      instr = Instruction::Sub;
      //      goto math;
      return context.Builder->CreateSub(lhs.codeGen(context),
                                        rhs.codeGen(context), "subtmp");
    case TMUL:
      //      instr = Instruction::Mul;
      //      goto math;
      return context.Builder->CreateMul(lhs.codeGen(context),
                                        rhs.codeGen(context), "multmp");
    case TDIV:
      //      instr = Instruction::SDiv;
      //      goto math;
      return context.Builder->CreateSDiv(lhs.codeGen(context),
                                         rhs.codeGen(context), "idivtmp");
      /* TODO comparison */
  }
  return nullptr;
  //math:
  //  return BinaryOperator::Create(instr, lhs.codeGen(context),
  //                                rhs.codeGen(context), "",
  //                                context.currentBlock());
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

NBranchStatement::NBranchStatement(NExpression &conditionExpr,
                                   NBlock &thenBlock, NBlock &elseBlock)
    : ConditionExpr(conditionExpr), ThenBlock(thenBlock), ElseBlock(elseBlock) {
}
Value *NBranchStatement::codeGen(CodeGenContext &context) {
  //  auto &TheContext = context.module->getContext();
  //  Value *CondV = ConditionExpr.codeGen(context);
  //  if (!CondV) return nullptr;
  //
  //  CondV =
  //      ICm ::Create(TheContext, CondV,
  //                   ConstantInt::get(Type::getInt64Ty(TheContext), 0), "ifcond");

  return nullptr;
}
