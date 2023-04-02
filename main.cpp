#include <iostream>
#include "codegen.h"
#include "node.h"

using namespace std;

extern FILE *yyin;
extern int yyparse();
extern NBlock *programBlock;

void createCoreFunctions(CodeGenContext &context);

int main(int argc, char **argv)
{
    const char *fname = "test/example.txt";
    const char *foutname = "test/example.bc";
    if (argc == 3) {
        fname = argv[1];
        foutname = argv[2];
    }

    FILE *fp = fopen(fname, "r");
    if (!fp)
    {
        printf("couldn't open file for reading\n");
        exit(-1);
    }
    yyin = fp;
    int parseErr = yyparse();
    if (parseErr != 0) {
        printf("couldn't complete lex parse\n");
        exit(-1);
    }
    fclose(fp);
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();
    CodeGenContext context;
    createCoreFunctions(context);
    context.generateCode(*programBlock, foutname);
    return 0;
}
