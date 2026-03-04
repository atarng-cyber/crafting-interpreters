package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

class Environment {
  final Environment enclosing;
  private static final Object UNINITIALIZED = new Object();
  private final Map<String, Object> values = new HashMap<>();

  Environment() {
    enclosing = null;
  }

  Environment(Environment enclosing) {
    this.enclosing = enclosing;
  }

  void define(String name, Object value) {
    values.put(name, value);
  }

  Object get(Token name) {
    Object value = values.get(name.lexeme);
if (value == UNINITIALIZED) {
  throw new RuntimeError(name,
      "Variable '" + name.lexeme + "' is declared but not initialized.");
}
return value;
  }

  void assign(Token name, Object value) {
    if (values.containsKey(name.lexeme)) {
      values.put(name.lexeme, value);
      return;
    }

    if (enclosing != null) {
      enclosing.assign(name, value);
      return;
    }

    throw new RuntimeError(name,
        "Undefined variable '" + name.lexeme + "'.");
  }

  static Object uninitialized() { return UNINITIALIZED; }

  Environment ancestor(int distance) {
  Environment environment = this;
  for (int i = 0; i < distance; i++) {
    environment = environment.enclosing;
  }
  return environment;
}

Object getAt(int distance, String name) {
  return ancestor(distance).values.get(name);
}

void assignAt(int distance, Token name, Object value) {
  ancestor(distance).values.put(name.lexeme, value);
}
}