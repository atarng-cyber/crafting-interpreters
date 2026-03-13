package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

class LoxInstance {
  private final LoxClass klass;
  private final Map<String, Object> fields = new HashMap<>();

  LoxInstance(LoxClass klass) {
    this.klass = klass;
  }

  LoxClass getKlass() { return klass; }

  Object get(Token name) {
  if (fields.containsKey(name.lexeme)) {
    return fields.get(name.lexeme);
  }

  LoxFunction method = klass.findMethod(name.lexeme);
  if (method != null) {
    if (method.isGetter()) {
      return method.bind(this).call(null, List.of());
    }
    return method.bind(this);
  }

  throw new RuntimeError(name,
      "Undefined property '" + name.lexeme + "'.");
}

  void set(Token name, Object value) {
    fields.put(name.lexeme, value);
  }

  void set(String name, Object value) {
  fields.put(name, value);
}

  @Override
  public String toString() {
    return klass.name + " instance";
  }
}