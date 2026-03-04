package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Stack;

class Resolver implements Expr.Visitor<Void>, Stmt.Visitor<Void> {
  private final Interpreter interpreter;
private static class VarInfo {
  final Token name;
  final int index;
  boolean defined;
  boolean used;

  VarInfo(Token name, int index) {
    this.name = name;
    this.index = index;
    this.defined = false;
    this.used = false;
  }
}

private final Stack<Map<String, VarInfo>> scopes = new Stack<>();
private final Stack<Integer> nextIndex = new Stack<>();

// Track what node "owns" this scope so we can store its slot count
private final Stack<Object> scopeOwner = new Stack<>();

private enum FunctionType {
    NONE,
    FUNCTION
  }

  private enum LoopType {
  NONE,
  WHILE
}

private LoopType currentLoop = LoopType.NONE;

  private FunctionType currentFunction = FunctionType.NONE;

  Resolver(Interpreter interpreter) {
    this.interpreter = interpreter;
  }

  void resolve(List<Stmt> statements) {
    for (Stmt statement : statements) {
      resolve(statement);
    }
  }

  private void resolve(Stmt stmt) {
    if (stmt != null) stmt.accept(this);
  }

  private void resolve(Expr expr) {
    if (expr != null) expr.accept(this);
  }

private void beginScope(Object owner) {
  scopes.push(new HashMap<String, VarInfo>());
  nextIndex.push(0);
  scopeOwner.push(owner);
}

private void endScope() {
  Map<String, VarInfo> scope = scopes.pop();
  int slotCount = nextIndex.pop();
  Object owner = scopeOwner.pop();

  // unused local error (optional: treat params too)
  for (VarInfo info : scope.values()) {
    if (info.defined && !info.used) {
      Lox.error(info.name, "Local variable '" + info.name.lexeme + "' is never used.");
    }
  }

  if (owner instanceof Stmt.Block) {
    interpreter.registerBlockSlots((Stmt.Block) owner, slotCount);
  } else if (owner instanceof Stmt.Function) {
    interpreter.registerFunctionSlots((Stmt.Function) owner, slotCount);
  }
}

  private void declare(Token name) {
  if (scopes.isEmpty()) return;

  Map<String, VarInfo> scope = scopes.peek();
  if (scope.containsKey(name.lexeme)) {
    Lox.error(name, "Already a variable with this name in this scope.");
  }

  int index = nextIndex.pop();
  nextIndex.push(index + 1);

  scope.put(name.lexeme, new VarInfo(name, index));
}

private void define(Token name) {
  if (scopes.isEmpty()) return;
  VarInfo info = scopes.peek().get(name.lexeme);
  if (info != null) info.defined = true;
}

  private void resolveLocal(Expr expr, Token name) {
  for (int i = scopes.size() - 1; i >= 0; i--) {
    Map<String, VarInfo> scope = scopes.get(i);
    if (scope.containsKey(name.lexeme)) {
      VarInfo info = scope.get(name.lexeme);
      int depth = scopes.size() - 1 - i;
      interpreter.resolve(expr, depth, info.index);
      return;
    }
  }
}

private void resolveFunction(Stmt.Function function, FunctionType type) {
  FunctionType enclosingFunction = currentFunction;
  currentFunction = type;

  beginScope(function);

  for (Token param : function.params) {
    declare(param);
    define(param);
  }

  resolve(function.body);
  endScope();

  currentFunction = enclosingFunction;
}

  // ----------------
  // Statement visitors
  // ----------------

@Override
public Void visitBlockStmt(Stmt.Block stmt) {
  beginScope(stmt);
  resolve(stmt.statements);
  endScope();
  return null;
}

@Override
public Void visitVarStmt(Stmt.Var stmt) {
  declare(stmt.name);

  // If this is a local (not global), record the slot index for runtime initialization
  if (!scopes.isEmpty()) {
    VarInfo info = scopes.peek().get(stmt.name.lexeme);
    if (info != null) {
      interpreter.registerVarSlot(stmt, info.index);
    }
  }

  if (stmt.initializer != null) {
    resolve(stmt.initializer);
  }

  define(stmt.name);
  return null;
}

  @Override
  public Void visitFunctionStmt(Stmt.Function stmt) {
    declare(stmt.name);
    define(stmt.name);
    resolveFunction(stmt, FunctionType.FUNCTION);
    return null;
  }

  @Override
  public Void visitExpressionStmt(Stmt.Expression stmt) {
    resolve(stmt.expression);
    return null;
  }

  @Override
  public Void visitIfStmt(Stmt.If stmt) {
    resolve(stmt.condition);
    resolve(stmt.thenBranch);
    if (stmt.elseBranch != null) resolve(stmt.elseBranch);
    return null;
  }

  @Override
  public Void visitPrintStmt(Stmt.Print stmt) {
    resolve(stmt.expression);
    return null;
  }

  @Override
  public Void visitReturnStmt(Stmt.Return stmt) {
    if (currentFunction == FunctionType.NONE) {
      Lox.error(stmt.keyword, "Can't return from top-level code.");
    }
    if (stmt.value != null) resolve(stmt.value);
    return null;
  }

@Override
public Void visitWhileStmt(Stmt.While stmt) {
  LoopType enclosingLoop = currentLoop;
  currentLoop = LoopType.WHILE;

  resolve(stmt.condition);
  resolve(stmt.body);

  currentLoop = enclosingLoop;
  return null;
}

  // ----------------
  // Expression visitors
  // ----------------

  @Override
  public Void visitAssignExpr(Expr.Assign expr) {
    resolve(expr.value);
    resolveLocal(expr, expr.name);
    return null;
  }

  @Override
public Void visitVariableExpr(Expr.Variable expr) {
  if (!scopes.isEmpty()) {
    VarInfo info = scopes.peek().get(expr.name.lexeme);
    if (info != null && !info.defined) {
      Lox.error(expr.name, "Can't read local variable in its own initializer.");
    }
  }

  resolveLocal(expr, expr.name);
  markUsed(expr.name);
  return null;
}

  @Override
  public Void visitBinaryExpr(Expr.Binary expr) {
    resolve(expr.left);
    resolve(expr.right);
    return null;
  }


  @Override
  public Void visitCallExpr(Expr.Call expr) {
    resolve(expr.callee);
    for (Expr argument : expr.arguments) {
      resolve(argument);
    }
    return null;
  }

  @Override
  public Void visitGroupingExpr(Expr.Grouping expr) {
    resolve(expr.expression);
    return null;
  }

  @Override
  public Void visitLiteralExpr(Expr.Literal expr) {
    return null;
  }

  @Override
  public Void visitLogicalExpr(Expr.Logical expr) {
    resolve(expr.left);
    resolve(expr.right);
    return null;
  }

  @Override
  public Void visitUnaryExpr(Expr.Unary expr) {
    resolve(expr.right);
    return null;
  }

  @Override
public Void visitBreakStmt(Stmt.Break stmt) {
  if (currentLoop == LoopType.NONE) {
    Lox.error(stmt.keyword, "Can't use 'break' outside of a loop.");
  }
  return null;
}

  // ------------------------------
  // If you implemented anonymous functions:
  // Expr.Function(params, body)
  // ------------------------------
  @Override
  public Void visitFunctionExpr(Expr.Function expr) {
    FunctionType enclosing = currentFunction;
    currentFunction = FunctionType.FUNCTION;

    beginScope(expr);
    for (Token param : expr.params) {
      declare(param);
      define(param);
    }
    resolve(expr.body);
    endScope();

    currentFunction = enclosing;
    return null;
  }

  private void markUsed(Token name) {
  for (int i = scopes.size() - 1; i >= 0; i--) {
    VarInfo info = scopes.get(i).get(name.lexeme);
    if (info != null) {
      info.used = true;
      return;
    }
  }
}
}