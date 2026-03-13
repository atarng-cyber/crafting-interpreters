package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;
import java.util.ArrayList;
import java.util.List;

class Interpreter implements Expr.Visitor<Object>,
                             Stmt.Visitor<Void> {

final Environment globals = new Environment();
private Environment environment = globals;
private final Map<Expr, Local> locals = new HashMap<>();
private final Map<Stmt.Block, Integer> blockSlotCounts = new HashMap<>();
private final Map<Stmt.Function, Integer> functionSlotCounts = new HashMap<>();
private final Map<Stmt.Var, Integer> varDeclSlots = new HashMap<>();

private static class Local {
  final int depth;
  final int index;
  Local(int depth, int index) {
    this.depth = depth;
    this.index = index;
  }
}

Interpreter() {
  globals.define("clock", new LoxCallable() {
    @Override public int arity() { return 0; }

    @Override
    public Object call(Interpreter interpreter, List<Object> arguments) {
      return (double) System.currentTimeMillis() / 1000.0;
    }

    @Override
    public String toString() { return "<native fn>"; }
  });
}

  void interpret(List<Stmt> statements) {
    try {
      for (Stmt statement : statements) {
        execute(statement);
      }
    } catch (RuntimeError error) {
      Lox.runtimeError(error);
    }
  }

  // ---------------- Statements ----------------

  private void execute(Stmt stmt) {
    stmt.accept(this);
  }

  void executeBlock(List<Stmt> statements, Environment environment) {
    Environment previous = this.environment;
    try {
      this.environment = environment;

      for (Stmt statement : statements) {
        execute(statement);
      }
    } finally {
      this.environment = previous;
    }
  }

  @Override
  public Void visitExpressionStmt(Stmt.Expression stmt) {
    evaluate(stmt.expression);
    return null;
  }

  @Override
  public Void visitPrintStmt(Stmt.Print stmt) {
    Object value = evaluate(stmt.expression);
    System.out.println(stringify(value));
    return null;
  }

  @Override
public Void visitVarStmt(Stmt.Var stmt) {
  Object value = null;
  if (stmt.initializer != null) {
    value = evaluate(stmt.initializer);
  }

  Integer slot = varDeclSlots.get(stmt);
  if (slot != null) {
    // local variable: store in current env slots
    environment.defineAt(slot, value);
  } else {
    // global variable
    environment.define(stmt.name.lexeme, value);
  }
  return null;
}

  @Override
public Void visitBlockStmt(Stmt.Block stmt) {
  int slots = blockSlotCounts.getOrDefault(stmt, 0);
  executeBlock(stmt.statements, new Environment(environment, slots));
  return null;
}

  // ---------------- Expressions ----------------

  @Override
  public Object visitLiteralExpr(Expr.Literal expr) {
    return expr.value;
  }

  @Override
  public Object visitGroupingExpr(Expr.Grouping expr) {
    return evaluate(expr.expression);
  }

  @Override
  public Object visitUnaryExpr(Expr.Unary expr) {
    Object right = evaluate(expr.right);

    switch (expr.operator.type) {
      case BANG:
        return !isTruthy(right);
      case MINUS:
        checkNumberOperand(expr.operator, right);
        return -(double) right;
      default:
        return null;
    }
  }

  @Override
  public Object visitBinaryExpr(Expr.Binary expr) {
    Object left = evaluate(expr.left);
    Object right = evaluate(expr.right);

    switch (expr.operator.type) {
      case GREATER:
        checkNumberOperands(expr.operator, left, right);
        return (double) left > (double) right;
      case GREATER_EQUAL:
        checkNumberOperands(expr.operator, left, right);
        return (double) left >= (double) right;
      case LESS:
        checkNumberOperands(expr.operator, left, right);
        return (double) left < (double) right;
      case LESS_EQUAL:
        checkNumberOperands(expr.operator, left, right);
        return (double) left <= (double) right;

      case BANG_EQUAL:
        return !isEqual(left, right);
      case EQUAL_EQUAL:
        return isEqual(left, right);

      case MINUS:
        checkNumberOperands(expr.operator, left, right);
        return (double) left - (double) right;

      case SLASH:
        checkNumberOperands(expr.operator, left, right);
        double divisor = (double) right;
        if (divisor == 0.0) {
          throw new RuntimeError(expr.operator, "Division by zero.");
        }
        return (double) left / divisor;

      case STAR:
        checkNumberOperands(expr.operator, left, right);
        return (double) left * (double) right;

      case PLUS:
        if (left instanceof Double && right instanceof Double) {
          return (double) left + (double) right;
        }
        if (left instanceof String && right instanceof String) {
          return (String) left + (String) right;
        }
        if (left instanceof String) {
          return (String) left + stringify(right);
        }
        if (right instanceof String) {
          return stringify(left) + (String) right;
        }

        throw new RuntimeError(expr.operator,
            "Operands must be two numbers or at least one string.");

      default:
        return null;
    }
  }

  @Override
public Object visitVariableExpr(Expr.Variable expr) {
  return lookUpVariable(expr.name, expr);
}

@Override
public Object visitAssignExpr(Expr.Assign expr) {
  Object value = evaluate(expr.value);

  Local local = locals.get(expr);
  if (local != null) {
    environment.assignAt(local.depth, local.index, value);
  } else {
    globals.assign(expr.name, value);
  }

  return value;
}

  @Override
public Void visitIfStmt(Stmt.If stmt) {
  if (isTruthy(evaluate(stmt.condition))) {
    execute(stmt.thenBranch);
  } else if (stmt.elseBranch != null) {
    execute(stmt.elseBranch);
  }
  return null;
}


@Override
public Void visitWhileStmt(Stmt.While stmt) {
  while (isTruthy(evaluate(stmt.condition))) {
    try {
      execute(stmt.body);
    } catch (BreakSignal b) {
      break;
    }
  }
  return null;
}

  @Override
public Object visitLogicalExpr(Expr.Logical expr) {
  Object left = evaluate(expr.left);

  if (expr.operator.type == TokenType.OR) {
    if (isTruthy(left)) return left;
  } else { // AND
    if (!isTruthy(left)) return left;
  }

  return evaluate(expr.right);
}

@Override
public Void visitBreakStmt(Stmt.Break stmt) {
  throw new BreakSignal();
}

@Override
public Object visitCallExpr(Expr.Call expr) {
  Object callee = evaluate(expr.callee);

  List<Object> arguments = new ArrayList<>();
  for (Expr argument : expr.arguments) {
    arguments.add(evaluate(argument));
  }

  // Special-case the "inner" callable: it performs its own lookup and
  // invocation (may choose to do nothing if no subclass method exists).
  if (callee instanceof InnerCallable) {
    InnerCallable ic = (InnerCallable) callee;
    return ic.call(this, arguments);
  }

  if (!(callee instanceof LoxCallable)) {
    throw new RuntimeError(expr.paren, "Can only call functions and classes.");
  }

  LoxCallable function = (LoxCallable) callee;

  if (arguments.size() != function.arity()) {
    throw new RuntimeError(expr.paren,
        "Expected " + function.arity() + " arguments but got " +
        arguments.size() + ".");
  }

  return function.call(this, arguments);
}

@Override
public Void visitFunctionStmt(Stmt.Function stmt) {
  LoxFunction function = new LoxFunction(stmt, environment, false);
  environment.define(stmt.name.lexeme, function);
  return null;
}

@Override
public Object visitFunctionExpr(Expr.Function expr) {
  // Wrap it into a "declaration-like" object the runtime can execute.
  // Easiest: add a second constructor to LoxFunction that accepts params/body directly,
  // OR synthesize a Stmt.Function with a fake name.
  LoxFunction function = LoxFunction.anonymous(expr, environment);
  return function;
}

@Override
public Void visitReturnStmt(Stmt.Return stmt) {
  Object value = null;
  if (stmt.value != null) value = evaluate(stmt.value);
  throw new Return(value);
}

  private Object evaluate(Expr expr) {
    return expr.accept(this);
  }

  // ---------------- Helpers ----------------

  private boolean isTruthy(Object object) {
    if (object == null) return false;
    if (object instanceof Boolean) return (boolean) object;
    return true;
  }

  private boolean isEqual(Object a, Object b) {
    if (a == null && b == null) return true;
    if (a == null) return false;
    return a.equals(b);
  }

  private void checkNumberOperand(Token operator, Object operand) {
    if (operand instanceof Double) return;
    throw new RuntimeError(operator, "Operand must be a number.");
  }

  private void checkNumberOperands(Token operator, Object left, Object right) {
    if (left instanceof Double && right instanceof Double) return;
    throw new RuntimeError(operator, "Operands must be numbers.");
  }

  private String stringify(Object object) {
    if (object == null) return "nil";

    if (object instanceof Double) {
      String text = object.toString();
      if (text.endsWith(".0")) {
        text = text.substring(0, text.length() - 2);
      }
      return text;
    }

    return object.toString();
  }

  void interpretExpression(Expr expr) {
  try {
    Object value = evaluate(expr);
    System.out.println(stringify(value));
  } catch (RuntimeError error) {
    Lox.runtimeError(error);
  }
}

private static class BreakSignal extends RuntimeException {
  BreakSignal() {
    super(null, null, false, false); // no stack trace
  }
}

private class InnerCallable implements LoxCallable {
  final int distance;
  final Expr.Inner expr;

  InnerCallable(int distance, Expr.Inner expr) {
    this.distance = distance;
    this.expr = expr;
  }

  @Override public int arity() { return 0; }

  @Override
  public Object call(Interpreter interpreter, List<Object> arguments) {
    // Lookup the owner (the class where this method was defined) and the current instance.
    LoxClass owner = (LoxClass) environment.getAt(distance, "inner");
    LoxInstance object = (LoxInstance) environment.getAt(distance - 1, "this");

    Object maybeName = environment.getAt(distance - 1, "__method_name__");
    if (!(maybeName instanceof String)) return null;
    String methodName = (String) maybeName;

    // Search from the runtime class up toward the owner, but pick the nearest
    // subclass to the owner that defines the method (BETA semantics for inner).
    LoxClass curr = object.getKlass();
    LoxFunction found = null;
    while (curr != null && curr != owner) {
      LoxFunction m = curr.findOwnMethod(methodName);
      if (m != null) found = m; // overwrite to prefer the one nearest the owner
      curr = curr.superclass;
    }

    if (found == null) return null; // do nothing if no inner method found
    return found.bind(object).call(interpreter, arguments);
  }

  @Override
  public String toString() { return "<inner fn>"; }
}

void resolve(Expr expr, int depth, int index) {
  locals.put(expr, new Local(depth, index));
}

void registerBlockSlots(Stmt.Block block, int slotCount) {
  blockSlotCounts.put(block, slotCount);
}

void registerFunctionSlots(Stmt.Function function, int slotCount) {
  functionSlotCounts.put(function, slotCount);
}

void registerVarSlot(Stmt.Var stmt, int index) {
  varDeclSlots.put(stmt, index);
}

int getFunctionSlotCount(Stmt.Function function) {
  Integer n = functionSlotCounts.get(function);
  return (n == null) ? 0 : n;
}

private Object lookUpVariable(Token name, Expr expr) {
  Local local = locals.get(expr);
  if (local != null) {
    return environment.getAt(local.depth, local.index);
  }
  return globals.get(name);
}

@Override
public Void visitClassStmt(Stmt.Class stmt) {
  Object superclass = null;
  if (stmt.superclass != null) {
    superclass = evaluate(stmt.superclass);
    if (!(superclass instanceof LoxClass)) {
      throw new RuntimeError(stmt.superclass.name,
          "Superclass must be a class.");
    }
  }

  environment.define(stmt.name.lexeme, null);

  // If there's a superclass, make a temporary environment to bind it so
  // methods can close over 'super'. That environment will also be the
  // parent environment for methods, so define 'inner' there as well.
  if (stmt.superclass != null) {
    environment = new Environment(environment);
    environment.define("super", superclass);
  }

  Map<String, LoxFunction> methods = new HashMap<>();
  Map<String, LoxFunction> staticMethods = new HashMap<>();

  // Create the LoxClass early pointing at the (currently empty) method maps
  // so that we can bind the class itself into the methods' closure as
  // "inner" before creating the LoxFunction closures.
  LoxClass klass = new LoxClass(stmt.name.lexeme, (LoxClass) superclass, methods, staticMethods);
  environment.define("inner", klass);

  for (Stmt.Function method : stmt.methods) {
    boolean isInit = method.name.lexeme.equals("init");
    LoxFunction function = new LoxFunction(method, environment, isInit);
    methods.put(method.name.lexeme, function);
  }

  for (Stmt.Function method : stmt.staticMethods) {
    LoxFunction function = new LoxFunction(method, environment, false);
    staticMethods.put(method.name.lexeme, function);
  }

  if (superclass != null) {
    environment = environment.enclosing;
  }

  // Now assign the finalized class object into the defining environment.
  environment.assign(stmt.name, klass);
  return null;
}

@Override
public Object visitSuperExpr(Expr.Super expr) {
  Local local = locals.get(expr);
  if (local == null) {
    throw new RuntimeError(expr.keyword, "Undefined super access.");
  }

  int distance = local.depth;
  LoxClass superclass = (LoxClass) environment.getAt(distance, "super");
  LoxInstance object = (LoxInstance) environment.getAt(distance - 1, "this");

  LoxFunction method = superclass.findMethod(expr.method.lexeme);
  if (method == null) {
    throw new RuntimeError(expr.method,
        "Undefined property '" + expr.method.lexeme + "'.");
  }

  return method.bind(object);
}

@Override
public Object visitInnerExpr(Expr.Inner expr) {
  Local local = locals.get(expr);
  if (local == null) {
    throw new RuntimeError(expr.keyword, "Undefined inner access.");
  }
  return new InnerCallable(local.depth, expr);
}

@Override
public Object visitGetExpr(Expr.Get expr) {
  Object object = evaluate(expr.object);
  if (object instanceof LoxInstance) {
    return ((LoxInstance) object).get(expr.name);
  }

  throw new RuntimeError(expr.name, "Only instances have properties.");
}

@Override
public Object visitSetExpr(Expr.Set expr) {
  Object object = evaluate(expr.object);

  if (!(object instanceof LoxInstance)) {
    throw new RuntimeError(expr.name, "Only instances have fields.");
  }

  Object value = evaluate(expr.value);
  ((LoxInstance) object).set(expr.name, value);
  return value;
}

@Override
public Object visitThisExpr(Expr.This expr) {
  return environment.get(expr.keyword);
}

}