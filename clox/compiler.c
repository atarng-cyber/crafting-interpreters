#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "chunk.h"
#include "table.h"
#include "memory.h"
#include "object.h"
/* for DEBUG_PRINT_CODE disassembly */
#include "debug.h"

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

static Parser parser;
static Chunk* compilingChunk;

static Chunk* currentChunk() {
  return compilingChunk;
}

static void errorAt(Token* token, const char* message) {
  if (parser.panicMode) return;
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* message) {
  errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

static void advance() {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool match(TokenType type) {
  if (parser.current.type != type) return false;
  advance();
  return true;
}

static bool check(TokenType type) {
  return parser.current.type == type;
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitReturn() {
  emitByte(OP_RETURN);
}

static int makeConstant(Value value) {
  return addConstant(currentChunk(), value);
}

static void emitConstant(Value value) {
  /* writeConstant chooses OP_CONSTANT or OP_CONSTANT_LONG as needed. */
  writeConstant(currentChunk(), value, parser.previous.line);
}

/* Helpers for control-flow bytecode emission (backpatching). */
static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  /* Placeholder two-byte operand. */
  emitByte(0xff);
  emitByte(0xff);
  /* Return the offset of the operand's first byte. */
  return currentChunk()->count - 2;
}

static void patchJump(int offset) {
  /* -2 to adjust for the two bytes of the jump operand itself. */
  int jump = currentChunk()->count - offset - 2;
  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }
  currentChunk()->code[offset] = (uint8_t)((jump >> 8) & 0xff);
  currentChunk()->code[offset + 1] = (uint8_t)(jump & 0xff);
}

static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);
  int offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX) error("Loop body too large.");
  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

/* Precedence levels for Pratt parser */
typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  // =
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! -
  PREC_CALL,        // . ()
  PREC_PRIMARY
} Precedence;

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
  ObjString* name; /* interned string pointer */
  int depth;
  int prev; /* previous slot index for shadowing restoration */
  bool isMutable;
  bool isCaptured;
} Local;

/* Forward declare the struct tag so pointers to it can be used inside the
   definition. */
typedef struct Compiler Compiler;

struct Compiler {
  Local* locals;
  int localCount;
  int localsCapacity;
  int scopeDepth;
  Table localsTable; /* map from ObjString* to current slot index */
  Table globalsImmutable; /* set of global names declared immutable */
  /* Loop stack for continue handling: parallel arrays of loop start offsets
     and the scope depth at the loop entry. */
  int* loopStarts;
  int* loopScopeDepths;
  int loopCount;
  int loopCapacity;
  struct Compiler* enclosing; /* for nested function compilation */
  ObjFunction* function;     /* function being compiled, if any */
  /* Upvalue descriptors collected while compiling a nested function. */
  uint8_t* upvalueIsLocal;
  uint8_t* upvalueIndex;
  int upvalueCount;
  int upvalueCapacity;
};

static Compiler* current = NULL;

/* Forward declarations for recursive grammar */
static void expression(void);
static void parsePrecedence(Precedence precedence);
static ParseRule* getRule(TokenType type);
static void statement(void);
static void printStatement(void);
static void expressionStatement(void);
static void synchronize(void);
static void ifStatement(void);
static void whileStatement(void);
static void forStatement(void);
static void and_(bool canAssign);
static void or_(bool canAssign);
static void switchStatement(void);
static void continueStatement(void);
static void call_(bool canAssign);
static void funDeclaration(void);
static ObjFunction* compileFunction(void);
static void endCompiler(void);

/* Loop stack helpers */
static void pushLoopStart(int loopStart);
static void popLoopStart(void);

/* Additional forward declarations for variable helpers defined later. */
static void declareVariable(bool isMutable);
static bool identifiersEqual(Token* a, Token* b);

/* Variable / declaration helpers */
static int parseVariable(const char* errorMessage, bool isMutable);
static int identifierConstant(Token* name);
static void defineVariable(int global);
static void varDeclaration(bool isMutable);
static void namedVariable(Token name, bool canAssign);
static void variable(bool canAssign);

/* Scope helpers (defined later) */
static void beginScope(void);
static void endScope(void);
static void block(void);

/* Declaration / statement parsing */
static void declaration(void) {
  if (match(TOKEN_VAR)) {
    varDeclaration(true);
  } else if (match(TOKEN_VAL)) {
    varDeclaration(false);
  } else if (match(TOKEN_FUN)) {
    funDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode) synchronize();
}

static void initCompiler(Compiler* compiler) {
  compiler->locals = NULL;
  compiler->localCount = 0;
  compiler->localsCapacity = 0;
  compiler->scopeDepth = 0;
  initTable(&compiler->localsTable);
  initTable(&compiler->globalsImmutable);
  compiler->loopStarts = NULL;
  compiler->loopScopeDepths = NULL;
  compiler->loopCount = 0;
  compiler->loopCapacity = 0;
  compiler->enclosing = NULL;
  compiler->function = NULL;
  compiler->upvalueIsLocal = NULL;
  compiler->upvalueIndex = NULL;
  compiler->upvalueCount = 0;
  compiler->upvalueCapacity = 0;
  current = compiler;
}

static void statement(void) {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_FOR)) {
    /* for (...) { ... } */
    forStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_SWITCH)) {
    switchStatement();
  } else if (match(TOKEN_CONTINUE)) {
    continueStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

static int parseVariable(const char* errorMessage, bool isMutable) {
  consume(TOKEN_IDENTIFIER, errorMessage);
  declareVariable(isMutable);
  if (current->scopeDepth > 0) return 0;

  return identifierConstant(&parser.previous);
}

static int identifierConstant(Token* name) {
  /* Intern the identifier string first so identical lexemes have the same
     ObjString* pointer. Then reuse an existing constant in the current
     chunk if one already holds that ObjString to avoid duplicate
     constant entries. */
  ObjString* interned = copyString(name->start, name->length);
  Chunk* chunk = currentChunk();
  for (int i = 0; i < chunk->constants.count; i++) {
    Value v = chunk->constants.values[i];
    if (IS_OBJ(v) && IS_STRING(v) && AS_STRING(v) == interned) {
      return i;
    }
  }
  return makeConstant(OBJ_VAL(interned));
}

static void ensureLocalCapacity(Compiler* compiler) {
  if (compiler->localCount + 1 > compiler->localsCapacity) {
    int old = compiler->localsCapacity;
    int newCap = GROW_CAPACITY(old);
    compiler->locals = GROW_ARRAY(Local, compiler->locals, old, newCap);
    compiler->localsCapacity = newCap;
  }
}

static void ensureLoopCapacity(Compiler* compiler) {
  if (compiler->loopCount + 1 > compiler->loopCapacity) {
    int old = compiler->loopCapacity;
    int newCap = GROW_CAPACITY(old);
    compiler->loopStarts = GROW_ARRAY(int, compiler->loopStarts, old, newCap);
    compiler->loopScopeDepths = GROW_ARRAY(int, compiler->loopScopeDepths, old, newCap);
    compiler->loopCapacity = newCap;
  }
}

static void pushLoopStart(int loopStart) {
  ensureLoopCapacity(current);
  current->loopStarts[current->loopCount] = loopStart;
  current->loopScopeDepths[current->loopCount] = current->scopeDepth;
  current->loopCount++;
}

static void popLoopStart(void) {
  if (current->loopCount > 0) current->loopCount--;
}

/* Emit either the short (1-byte index) or long (3-byte index) local op. */
static void emitLocal(uint8_t shortOp, uint8_t longOp, uint32_t index) {
  if (index <= UINT8_MAX) {
    emitBytes(shortOp, (uint8_t)index);
  } else {
    emitByte(longOp);
    emitByte((uint8_t)(index & 0xFF));
    emitByte((uint8_t)((index >> 8) & 0xFF));
    emitByte((uint8_t)((index >> 16) & 0xFF));
  }
}

static void addLocal(Token name) {
  ensureLocalCapacity(current);

  ObjString* interned = copyString(name.start, name.length);

  int slot = current->localCount;
  Local* local = &current->locals[slot];
  local->name = interned;
  local->depth = -1;
  local->prev = -1;
  local->isMutable = true; /* default mutable; var/val will adjust */

  Value prevVal;
  if (tableGet(&current->localsTable, OBJ_VAL(interned), &prevVal)) {
    local->prev = (int)AS_NUMBER(prevVal);
  }

  tableSet(&current->localsTable, OBJ_VAL(interned), NUMBER_VAL(slot));
  current->localCount++;
}

static void declareVariable(bool isMutable) {
  if (current->scopeDepth == 0) return;

  Token* nameToken = &parser.previous;
  ObjString* interned = copyString(nameToken->start, nameToken->length);

  for (int i = current->localCount - 1; i >= 0; i--) {
    Local* local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (local->name == interned) {
      error("Already a variable with this name in this scope.");
    }
  }

  addLocal(*nameToken);
  /* Set mutability on the newly added local. */
  current->locals[current->localCount - 1].isMutable = isMutable;
}

/* identifiers are interned into ObjString*; pointer equality is sufficient. */
static bool identifiersEqual(Token* a, Token* b) {
  (void)a; (void)b; /* not used under new scheme */
  return false;
}

static int resolveLocal(Compiler* compiler, Token* name) {
  ObjString* interned = copyString(name->start, name->length);
  Value val;
  if (tableGet(&compiler->localsTable, OBJ_VAL(interned), &val)) {
    int slot = (int)AS_NUMBER(val);
    if (compiler->locals[slot].depth == -1) {
      error("Can't read local variable in its own initializer.");
    }
    return slot;
  }
  return -1;
}

/* Ensure the compiler's upvalue descriptor arrays have space for one more. */
static void ensureUpvalueCapacity(Compiler* compiler) {
  if (compiler->upvalueCapacity < compiler->upvalueCount + 1) {
    int oldCap = compiler->upvalueCapacity;
    int newCap = oldCap < 8 ? 8 : oldCap * 2;
    compiler->upvalueIsLocal = realloc(compiler->upvalueIsLocal, sizeof(uint8_t) * newCap);
    compiler->upvalueIndex = realloc(compiler->upvalueIndex, sizeof(uint8_t) * newCap);
    compiler->upvalueCapacity = newCap;
  }
}

/* Add an upvalue descriptor to the current compiler, returning its index. */
static int addUpvalue(Compiler* compiler, uint8_t isLocal, uint8_t index) {
  for (int i = 0; i < compiler->upvalueCount; i++) {
    if (compiler->upvalueIsLocal[i] == isLocal && compiler->upvalueIndex[i] == index) {
      return i;
    }
  }
  ensureUpvalueCapacity(compiler);
  int result = compiler->upvalueCount;
  compiler->upvalueIsLocal[result] = isLocal;
  compiler->upvalueIndex[result] = index;
  compiler->upvalueCount++;
  return result;
}

/* Try to resolve an upvalue by walking enclosing compilers. Returns index or -1. */
static int resolveUpvalue(Compiler* compiler, Token* name) {
  if (!compiler->enclosing) return -1;

  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, 1, (uint8_t)local);
  }

  int up = resolveUpvalue(compiler->enclosing, name);
  if (up != -1) {
    return addUpvalue(compiler, 0, (uint8_t)up);
  }

  return -1;
}
static void markInitialized() {
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(int global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  /* Emit DEFINE_GLOBAL or DEFINE_GLOBAL_LONG depending on constant index. */
  if (global <= UINT8_MAX) {
    emitBytes(OP_DEFINE_GLOBAL, global);
  } else {
    emitByte(OP_DEFINE_GLOBAL_LONG);
    emitByte((uint8_t)(global & 0xFF));
    emitByte((uint8_t)((global >> 8) & 0xFF));
    emitByte((uint8_t)((global >> 16) & 0xFF));
  }
}

static void varDeclaration(bool isMutable) {
  /* Save the name token now because parsing the initializer will advance
    the parser and change parser.previous. */
  int global = parseVariable("Expect variable name.", isMutable);
  Token nameToken = parser.previous;

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(OP_NIL);
  }
  consume(TOKEN_SEMICOLON,
          "Expect ';' after variable declaration.");

  defineVariable(global);
  /* If this is a top-level immutable (val) declaration, record it. */
  if (!isMutable && current->scopeDepth == 0) {
    int constIndex = identifierConstant(&nameToken);
    ObjString* name = AS_STRING(currentChunk()->constants.values[constIndex]);
    tableSet(&current->globalsImmutable, OBJ_VAL(name), BOOL_VAL(true));
  }
}

static void namedVariable(Token name, bool canAssign) {
  int arg = resolveLocal(current, &name);
  if (arg != -1) {
    /* local */
    if (canAssign && match(TOKEN_EQUAL)) {
      if (!current->locals[arg].isMutable) {
        error("Cannot assign to immutable local variable.");
      }
      expression();
      emitLocal(OP_SET_LOCAL, OP_SET_LOCAL_LONG, (uint32_t)arg);
    } else {
      emitLocal(OP_GET_LOCAL, OP_GET_LOCAL_LONG, (uint32_t)arg);
    }
    return;
  }

  /* Not a local: try resolve as an upvalue in enclosing functions. */
  int up = resolveUpvalue(current, &name);
  if (up != -1) {
    if (canAssign && match(TOKEN_EQUAL)) {
      expression();
      if (up > UINT8_MAX) {
        error("Too many upvalues in function (limit 255).");
      }
      emitBytes(OP_SET_UPVALUE, (uint8_t)up);
    } else {
      if (up > UINT8_MAX) {
        error("Too many upvalues in function (limit 255).");
      }
      emitBytes(OP_GET_UPVALUE, (uint8_t)up);
    }
    return;
  }

  /* global */
  arg = identifierConstant(&name);
  if (canAssign && match(TOKEN_EQUAL)) {
    /* If this global is immutable (val), compile-time error. */
    ObjString* nameObj = AS_STRING(currentChunk()->constants.values[arg]);
    Value imm;
    if (tableGet(&current->globalsImmutable, OBJ_VAL(nameObj), &imm)) {
      error("Cannot assign to immutable global variable.");
    }
    expression();
    if (arg <= UINT8_MAX) {
      emitBytes(OP_SET_GLOBAL, (uint8_t)arg);
    } else {
      emitByte(OP_SET_GLOBAL_LONG);
      emitByte((uint8_t)(arg & 0xFF));
      emitByte((uint8_t)((arg >> 8) & 0xFF));
      emitByte((uint8_t)((arg >> 16) & 0xFF));
    }
  } else {
    if (arg <= UINT8_MAX) {
      emitBytes(OP_GET_GLOBAL, (uint8_t)arg);
    } else {
      emitByte(OP_GET_GLOBAL_LONG);
      emitByte((uint8_t)(arg & 0xFF));
      emitByte((uint8_t)((arg >> 8) & 0xFF));
      emitByte((uint8_t)((arg >> 16) & 0xFF));
    }
  }
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

static void beginScope() {
  current->scopeDepth++;
}

static void endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth >
            current->scopeDepth) {
    Local* local = &current->locals[current->localCount - 1];
    ObjString* name = local->name;
    int prev = local->prev;
    if (prev == -1) {
      tableDelete(&current->localsTable, OBJ_VAL(name));
    } else {
      tableSet(&current->localsTable, OBJ_VAL(name), NUMBER_VAL(prev));
    }
    if (local->isCaptured) {
      emitByte(OP_CLOSE_UPVALUE);
    } else {
      emitByte(OP_POP);
    }
    current->localCount--;
  }
}

static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

/* (previously declared above) */

/* Parsing functions */
static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
  /* Trim the surrounding quotes. */
  emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
                                  parser.previous.length - 2)));
}

static void literal(bool canAssign) {
  switch (parser.previous.type) {
    case TOKEN_FALSE: emitByte(OP_FALSE); break;
    case TOKEN_NIL: emitByte(OP_NIL); break;
    case TOKEN_TRUE: emitByte(OP_TRUE); break;
    default: return; /* Unreachable. */
  }
}

static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  /* Compile the operand. */
  parsePrecedence(PREC_UNARY);

  /* Emit the operator instruction. */
  switch (operatorType) {
    case TOKEN_MINUS: emitByte(OP_NEGATE); break;
    default: return; /* Unreachable. */
  }
}

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_PLUS:  emitByte(OP_ADD); break;
    case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
    case TOKEN_STAR:  emitByte(OP_MULTIPLY); break;
    case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
    case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
    case TOKEN_GREATER:       emitByte(OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
    case TOKEN_LESS:          emitByte(OP_LESS); break;
    case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
    default: return; /* Unreachable. */
  }
}

static void ternary(bool canAssign) {
  parsePrecedence(PREC_ASSIGNMENT);
  consume(TOKEN_COLON, "Expect ':' after expression.");
  parsePrecedence(PREC_ASSIGNMENT);
}

static void and_(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  parsePrecedence(PREC_AND);
  patchJump(endJump);
}

static void or_(bool canAssign) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

/* Rule table */
static void ternary(bool canAssign);
static void and_(bool canAssign);
static void or_(bool canAssign);
static void ifStatement(void);
static void whileStatement(void);
static void forStatement(void);
ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, call_,   PREC_CALL},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_QUESTION]     = {NULL,     ternary, PREC_ASSIGNMENT},
  [TOKEN_COLON]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     or_,    PREC_OR},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

static void parsePrecedence(Precedence precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

static void expression(void) {
  parsePrecedence(PREC_ASSIGNMENT);
}

/* call parsing: (callable args...) */
static void call_(bool canAssign) {
  /* Parse argument list and emit OP_CALL with arg count. */
  int argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      argCount++;
      if (argCount > 255) error("Can't have more than 255 arguments.");
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  emitBytes(OP_CALL, (uint8_t)argCount);
}

/* Compile a function declared with 'fun' into an ObjFunction constant and
   emit an OP_CLOSURE that captures it. This simplified version creates a
   new ObjFunction and copies the current chunk into it. Full upvalue
   descriptor wiring is left for the next step. */
static ObjFunction* compileFunction(void) {
  /* Create a new ObjFunction and compile a nested function into it. */
  ObjFunction* function = newFunction();

  /* Save the current compiler and chunk, create a new compiler for the
     nested function, and set it as current. */
  Compiler fnCompiler;
  initCompiler(&fnCompiler);
  fnCompiler.enclosing = current;
  fnCompiler.function = function;

  /* Start a fresh chunk for the function. */
  Chunk* previousChunk = compilingChunk;
  Chunk functionChunk;
  initChunk(&functionChunk);
  compilingChunk = &functionChunk;

  /* Begin function scope and add an implicit local slot for the function
     receiver (not used here but simplifies indexing). */
  beginScope();
  /* Parse parameter list. Expect '(' already consumed by caller. */
  int arity = 0;
  if (!match(TOKEN_RIGHT_PAREN)) {
    do {
      consume(TOKEN_IDENTIFIER, "Expect parameter name.");
      ObjString* paramName = copyString(parser.previous.start, parser.previous.length);
      addLocal(parser.previous);
      markInitialized();
      arity++;
      if (arity > 255) error("Can't have more than 255 parameters.");
    } while (match(TOKEN_COMMA));
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
  }
  function->arity = arity;

  /* Parse function body. Expect '{' has been consumed by caller (funDeclaration). */
  consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block();
  /* Copy upvalue descriptor arrays so we can emit them after finishing the
     nested compiler — endCompiler will free the compiler's arrays. */
  int collectedUpvalues = fnCompiler.upvalueCount;
  uint8_t* upIsLocal = NULL;
  uint8_t* upIndex = NULL;
  if (collectedUpvalues > 0) {
    upIsLocal = (uint8_t*)malloc(collectedUpvalues);
    upIndex = (uint8_t*)malloc(collectedUpvalues);
    for (int i = 0; i < collectedUpvalues; i++) {
      upIsLocal[i] = fnCompiler.upvalueIsLocal[i];
      upIndex[i] = fnCompiler.upvalueIndex[i];
    }
  }

  /* Let endCompiler emit the return and free compiler resources for fnCompiler. */
  endCompiler();

  /* Move compiled chunk into function and restore previous chunk/compiler. */
  function->chunk = *compilingChunk;
  compilingChunk = previousChunk;
  function->upvalueCount = collectedUpvalues;

  /* Emit the function as a constant into the enclosing chunk, then emit the
     OP_CLOSURE and the copied upvalue descriptors. */
  emitConstant(OBJ_VAL(function));
  emitByte(OP_CLOSURE);
  for (int i = 0; i < collectedUpvalues; i++) {
    emitByte(upIsLocal[i]);
    emitByte(upIndex[i]);
  }

  if (upIsLocal) free(upIsLocal);
  if (upIndex) free(upIndex);

  /* Restore previous compiler as current (fnCompiler.enclosing). */
  current = fnCompiler.enclosing;

  return function;
}

static void funDeclaration(void) {
  /* Expected: 'fun' identifier '(' params ')' '{' body '}' */
  consume(TOKEN_IDENTIFIER, "Expect function name.");
  Token name = parser.previous;
  int global = identifierConstant(&name);

  /* Compile the function body into a function object. We'll temporarily
     create a new chunk and swap it into currentChunk via compilingChunk. */
  beginScope();
  ObjFunction* function = compileFunction();

  /* Emit the function as a constant and an OP_CLOSURE. */
  emitConstant(OBJ_VAL(function));
  emitByte(OP_CLOSURE);
  /* Emit upvalue descriptors recorded in the compiler: (isLocal, index) pairs. */
  for (int i = 0; i < current->upvalueCount; i++) {
    emitByte(current->upvalueIsLocal[i]);
    emitByte(current->upvalueIndex[i]);
  }

  /* Define the variable with the function value. */
  if (global <= UINT8_MAX) {
    emitBytes(OP_DEFINE_GLOBAL, (uint8_t)global);
  } else {
    emitByte(OP_DEFINE_GLOBAL_LONG);
    emitByte((uint8_t)(global & 0xFF));
    emitByte((uint8_t)((global >> 8) & 0xFF));
    emitByte((uint8_t)((global >> 16) & 0xFF));
  }

  endScope();
}

/* Statement parsing */
static void printStatement(void) {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}

static void expressionStatement(void) {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_POP);
}

static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  int elseJump = -1;
  if (match(TOKEN_ELSE)) {
    elseJump = emitJump(OP_JUMP);
  }

  patchJump(thenJump);
  emitByte(OP_POP);

  if (elseJump != -1) {
    patchJump(elseJump);
  }
}

static void whileStatement() {
  int loopStart = currentChunk()->count;
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  emitLoop(loopStart);
  patchJump(exitJump);
  emitByte(OP_POP);
}

static void forStatement() {
  beginScope();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

  if (match(TOKEN_SEMICOLON)) {
    // no initializer
  } else if (match(TOKEN_VAR)) {
    varDeclaration(true);
  } else if (match(TOKEN_VAL)) {
    varDeclaration(false);
  } else {
    expressionStatement();
  }

  int loopStart = currentChunk()->count;

  int exitJump = -1;
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
  }

  if (!match(TOKEN_RIGHT_PAREN)) {
    int bodyJump = emitJump(OP_JUMP);
    int incrementStart = currentChunk()->count;
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJump);
  }

  statement();

  emitLoop(loopStart);

  if (exitJump != -1) {
    patchJump(exitJump);
    emitByte(OP_POP);
  }

  endScope();
}

static void continueStatement(void) {
  consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
  if (current->loopCount == 0) {
    error("'continue' must be inside a loop.");
    return;
  }

  int loopStart = current->loopStarts[current->loopCount - 1];
  int loopScope = current->loopScopeDepths[current->loopCount - 1];

  /* At runtime we need to pop any locals that were declared inside nested
     scopes within the loop body before jumping back to the top of the loop.
     Emit the appropriate number of OP_POPs now (do not modify compile-time
     local table). */
  int popCount = 0;
  for (int i = current->localCount - 1; i >= 0 && current->locals[i].depth > loopScope; i--) {
    popCount++;
  }
  for (int i = 0; i < popCount; i++) emitByte(OP_POP);

  /* Jump back to the loop start. */
  emitLoop(loopStart);
}

static void switchStatement(void) {
  /* switch (expr) { case ... default ... } */
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after switch value.");

  consume(TOKEN_LEFT_BRACE, "Expect '{' before switch cases.");

  beginScope();
  /* Allocate a local slot to store the switch value so we can compare it
     against each case expression repeatedly. */
  Token dummyName;
  static const char* switchName = "__switch";
  dummyName.start = switchName;
  dummyName.length = (int)strlen(switchName);
  addLocal(dummyName);
  markInitialized();
  int switchLocal = current->localCount - 1;

  /* Store the evaluated switch value into the local. */
  emitLocal(OP_SET_LOCAL, OP_SET_LOCAL_LONG, (uint32_t)switchLocal);

  /* We'll collect jumps to the end of the switch for each case body so we can
     patch them after we've emitted the whole switch. */
  int* exitJumps = NULL;
  int exitCount = 0, exitCap = 0;

  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    if (match(TOKEN_CASE)) {
      /* case expression: */
      /* load switch value */
      emitLocal(OP_GET_LOCAL, OP_GET_LOCAL_LONG, (uint32_t)switchLocal);
      expression();
      consume(TOKEN_COLON, "Expect ':' after case expression.");
      emitByte(OP_EQUAL);

      /* If not equal, skip this case's body. */
      int skipCase = emitJump(OP_JUMP_IF_FALSE);
      emitByte(OP_POP); /* pop the comparison true value for the taken branch */

      /* compile zero or more statements for this case until next case/default/} */
      while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) && !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
      }

      /* After finishing this case's body, jump to end of switch to avoid
         falling through. */
      int endJump = emitJump(OP_JUMP);
      /* record endJump */
      if (exitCount + 1 > exitCap) {
        int old = exitCap;
        int newCap = old == 0 ? 8 : old * 2;
        exitJumps = GROW_ARRAY(int, exitJumps, old, newCap);
        exitCap = newCap;
      }
      exitJumps[exitCount++] = endJump;

      /* patch skipCase and pop the comparison value for the false branch */
      patchJump(skipCase);
      emitByte(OP_POP);

    } else if (match(TOKEN_DEFAULT)) {
      consume(TOKEN_COLON, "Expect ':' after 'default'.");
      /* compile statements until end or next case (though default should be last)
         We'll execute default if no previous case matched. */
      while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) && !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
      }
    } else {
      /* Synchronize/skip unexpected tokens inside switch body. */
      declaration();
    }
  }

  /* Patch all exits to jump here (after switch). */
  int exitPos = currentChunk()->count;
  for (int i = 0; i < exitCount; i++) {
    patchJump(exitJumps[i]);
  }

  if (exitJumps != NULL) FREE_ARRAY(int, exitJumps, exitCap);

  /* End the scope: this will pop the switch local. */
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after switch.");
  endScope();
}

static void synchronize() {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;
    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUN:
      case TOKEN_VAR:
      case TOKEN_VAL:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_PRINT:
      case TOKEN_RETURN:
        return;

      default:
        ; /* Do nothing. */
    }

    advance();
  }
}

static void endCompiler() {
  emitReturn();
#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), "code");
  }
#endif
  /* Free resources allocated for locals table/array. */
  freeTable(&current->localsTable);
  freeTable(&current->globalsImmutable);
  if (current->locals != NULL) {
    FREE_ARRAY(Local, current->locals, current->localsCapacity);
    current->locals = NULL;
    current->localsCapacity = 0;
  }
  if (current->loopStarts != NULL) {
    FREE_ARRAY(int, current->loopStarts, current->loopCapacity);
    current->loopStarts = NULL;
    current->loopCapacity = 0;
  }
  if (current->loopScopeDepths != NULL) {
    FREE_ARRAY(int, current->loopScopeDepths, current->loopCapacity);
    current->loopScopeDepths = NULL;
  }
}

bool compile(const char* source, Chunk* chunk) {
  initScanner(source);
  Compiler compiler;
  initCompiler(&compiler);
  compilingChunk = chunk;

  parser.hadError = false;
  parser.panicMode = false;

  advance();
  /* Compile top-level declarations until EOF. */
  while (!match(TOKEN_EOF)) {
    declaration();
  }

  endCompiler();
  return !parser.hadError;
}
