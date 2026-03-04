package com.craftinginterpreters.lox;

import java.util.List;

class LoxFunction implements LoxCallable {
  private final Stmt.Function declaration;
  private final Environment closure;

  LoxFunction(Stmt.Function declaration, Environment closure) {
    this.declaration = declaration;
    this.closure = closure;
  }

  @Override
  public int arity() {
    return declaration.params.size();
  }

  @Override
public Object call(Interpreter interpreter, List<Object> arguments) {
  int slotCount = interpreter.getFunctionSlotCount(declaration);
  Environment environment = new Environment(closure, slotCount);

  for (int i = 0; i < declaration.params.size(); i++) {
    environment.defineAt(i, arguments.get(i));
  }

  try {
    interpreter.executeBlock(declaration.body, environment);
  } catch (Return returnValue) {
    return returnValue.value;
  }
  return null;
}

  @Override
public String toString() {
  if (declaration.name.lexeme.equals("<anonymous>")) return "<fn>";
  return "<fn " + declaration.name.lexeme + ">";
}

  static LoxFunction anonymous(Expr.Function expr, Environment closure) {
  // Create a fake name token just for toString() / debugging.
  Token fakeName = new Token(TokenType.IDENTIFIER, "<anonymous>", null, -1);
  Stmt.Function decl = new Stmt.Function(fakeName, expr.params, expr.body);
  return new LoxFunction(decl, closure);
}
}