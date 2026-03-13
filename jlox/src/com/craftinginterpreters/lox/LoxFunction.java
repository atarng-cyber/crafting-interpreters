package com.craftinginterpreters.lox;

import java.util.List;

class LoxFunction implements LoxCallable {
  private final Stmt.Function declaration;
  private final Environment closure;

  private final boolean isInitializer;
  private final boolean isGetter;

  // Used only for anonymous functions created from Expr.Function
  private final Expr.Function anon;

  // ---------- Constructors for named functions/methods ----------
  LoxFunction(Stmt.Function declaration, Environment closure,
              boolean isInitializer, boolean isGetter) {
    this.declaration = declaration;
    this.closure = closure;
    this.isInitializer = isInitializer;
    this.isGetter = isGetter;
    this.anon = null;
  }

  // Convenience overloads
  LoxFunction(Stmt.Function declaration, Environment closure, boolean isInitializer) {
    this(declaration, closure, isInitializer, false);
  }

  LoxFunction(Stmt.Function declaration, Environment closure) {
    this(declaration, closure, false, false);
  }

  // ---------- Private constructor for anonymous functions ----------
  private LoxFunction(Expr.Function anon, Environment closure) {
    this.declaration = null;
    this.anon = anon;
    this.closure = closure;
    this.isInitializer = false;
    this.isGetter = false;
  }

  // Factory used by Interpreter.java line 293
  static LoxFunction anonymous(Expr.Function expr, Environment closure) {
    return new LoxFunction(expr, closure);
  }

  // Called by LoxInstance.get() in your getter implementation
  public boolean isGetter() {
    return isGetter;
  }

  // ---------- LoxCallable ----------
  @Override
  public int arity() {
    if (anon != null) return anon.params.size();
    return declaration.params.size();
  }

  @Override
  public Object call(Interpreter interpreter, List<Object> arguments) {
    Environment environment = new Environment(closure);

    if (anon != null) {
      for (int i = 0; i < anon.params.size(); i++) {
        environment.define(anon.params.get(i).lexeme, arguments.get(i));
      }
      try {
        interpreter.executeBlock(anon.body, environment);
      } catch (Return returnValue) {
        return returnValue.value;
      }
      return null;
    }

    // Named function/method
    for (int i = 0; i < declaration.params.size(); i++) {
      environment.define(declaration.params.get(i).lexeme, arguments.get(i));
    }

    try {
      interpreter.executeBlock(declaration.body, environment);
    } catch (Return returnValue) {
      if (isInitializer) return closure.getAt(0, "this");
      return returnValue.value;
    }

    if (isInitializer) return closure.getAt(0, "this");
    return null;
  }

  LoxFunction bind(LoxInstance instance) {
    Environment environment = new Environment(closure);
    environment.define("this", instance);
    // Store the method name in the environment so "inner" can find it at runtime.
    if (declaration != null) {
      environment.define("__method_name__", declaration.name.lexeme);
    }
    return new LoxFunction(declaration, environment, isInitializer, isGetter);
  }

  @Override
  public String toString() {
    if (anon != null) return "<fn>";
    return "<fn " + declaration.name.lexeme + ">";
  }
}