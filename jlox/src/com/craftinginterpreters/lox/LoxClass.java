package com.craftinginterpreters.lox;

import java.util.List;
import java.util.Map;

class LoxClass extends LoxInstance implements LoxCallable {
  final String name;
  final LoxClass superclass;
  private final Map<String, LoxFunction> methods;

  LoxClass(String name, LoxClass superclass,
           Map<String, LoxFunction> methods,
           Map<String, LoxFunction> staticMethods) {
    super(null); // class itself doesn't have a class
    this.superclass = superclass;
    this.name = name;
    this.methods = methods;

    // Attach static methods as fields on the class object
    for (Map.Entry<String, LoxFunction> entry : staticMethods.entrySet()) {
      set(entry.getKey(), entry.getValue());
    }
  }

  LoxFunction findMethod(String name) {
    // BETA semantics: prefer the highest method on the inheritance chain.
    if (superclass != null) {
      LoxFunction m = superclass.findMethod(name);
      if (m != null) return m;
    }
    if (methods.containsKey(name)) return methods.get(name);
    return null;
  }

  // Helper: find a method defined directly on this class (no recursion).
  LoxFunction findOwnMethod(String name) {
    return methods.get(name);
  }

  @Override
  public Object call(Interpreter interpreter,
                     List<Object> arguments) {
    LoxInstance instance = new LoxInstance(this);

    LoxFunction initializer = findMethod("init");
    if (initializer != null) {
      initializer.bind(instance).call(interpreter, arguments);
    }

    return instance;
  }

  @Override
  public int arity() {
    LoxFunction initializer = findMethod("init");
    if (initializer == null) return 0;
    return initializer.arity();
  }

  @Override
  public String toString() {
    return name;
  }
}