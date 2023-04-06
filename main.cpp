#include "codegen.h"
#include "node.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <stdio.h>
#include <memory>
#include <string>

using namespace std;
using namespace llvm;
using namespace llvm::sys;

extern FILE *yyin;
extern int yyparse();
extern NBlock *programBlock;

int main(int argc, char **argv) {
  const char *fname = "test/example.txt";
  const char *foutname = "test/example.bc";
  if (argc == 3) {
    fname = argv[1];
    foutname = argv[2];
  }

  FILE *fp = fopen(fname, "r");
  if (!fp) {
    errs() << "Failed when open file " << fname << '\n';
    exit(-1);
  }
  yyin = fp;
  int parseErr = yyparse();
  if (parseErr != 0) {
    errs() << "Failed when parse\n";
    exit(-1);
  }
  fclose(fp);

  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  CodeGenContext context;
  createCoreFunctions(context);
  context.generateCode(*programBlock, foutname);

  auto TargetTriple = sys::getDefaultTargetTriple();
  auto TheModule = context.module;

  // Create a new pass manager attached to it.
  std::shared_ptr<legacy::FunctionPassManager> FPM =
    std::make_shared<legacy::FunctionPassManager>(TheModule);

  // Do simple "peephole" optimizations and bit-twiddling optzns.
  FPM->add(createInstructionCombiningPass());
  // Reassociate expressions.
  FPM->add(createReassociatePass());
  // Eliminate Common SubExpressions.
  FPM->add(createGVNPass());
  // Simplify the control flow graph (deleting unreachable blocks, etc).
  FPM->add(createCFGSimplificationPass());

  FPM->doInitialization();

  // Optimize with function passes
  for (auto &Func: *TheModule) FPM->run(Func);

  printIR(TheModule);

  TheModule->setTargetTriple(TargetTriple);

  std::string Error;
  auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);

  // Print an error and exit if we couldn't find the requested target.
  // This generally occurs if we've forgotten to initialise the
  // TargetRegistry or we have a bogus target triple.
  if (!Target) {
    errs() << Error;
    return 1;
  }

  auto CPU = "generic";
  auto Features = "";


  TargetOptions opt;
  auto RM = std::optional<Reloc::Model>();
  auto TheTargetMachine =
      Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);

  TheModule->setDataLayout(TheTargetMachine->createDataLayout());

  auto Filename = "test/output.o";
  if (argc == 3) Filename = argv[2];
  std::error_code EC;
  raw_fd_ostream dest(Filename, EC, sys::fs::OF_None);

  if (EC) {
    errs() << "Could not open file: " << EC.message();
    return 1;
  }

  legacy::PassManager pass;
  auto FileType = CGFT_ObjectFile;

  if (TheTargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    errs() << "TheTargetMachine can't emit a file of this type";
    return 1;
  }

  pass.run(*TheModule);
  dest.flush();

  outs() << "Wrote " << Filename << "\n";

  return 0;
}
