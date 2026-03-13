package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

class Environment {
  // Sentinel for "declared but not initialized".
  private static final Object UNINITIALIZED = new Object();

  static Object uninitialized() {
    return UNINITIALIZED;
  }

  final Environment enclosing;

  // Globals (and any scope you still store by name).
  private final Map<String, Object> values = new HashMap<>();

  // Locals stored by slot index (Challenge 4).
  private final Object[] slots;

  // Global scope constructor.
  Environment() {
    enclosing = null;
    slots = null;
  }

  // Non-slot scope (still allowed).
  Environment(Environment enclosing) {
    this(enclosing, 0);
  }

  // Slot-based scope constructor (Challenge 4).
  Environment(Environment enclosing, int slotCount) {
    this.enclosing = enclosing;
    this.slots = new Object[slotCount];
    for (int i = 0; i < slotCount; i++) {
      this.slots[i] = UNINITIALIZED;
    }
  }

  // ===== Map-based operations (globals / name lookup) =====

  void define(String name, Object value) {
    values.put(name, value);
  }

  Object get(Token name) {
    if (values.containsKey(name.lexeme)) {
      Object value = values.get(name.lexeme);
      if (value == UNINITIALIZED) {
        throw new RuntimeError(name,
            "Variable '" + name.lexeme + "' is not initialized.");
      }
      return value;
    }

    if (enclosing != null) return enclosing.get(name);

    throw new RuntimeError(name,
        "Undefined variable '" + name.lexeme + "'.");
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

  // ===== Distance-based helpers (Chapter 11) =====

  Environment ancestor(int distance) {
    Environment environment = this;
    for (int i = 0; i < distance; i++) {
      environment = environment.enclosing;
    }
    return environment;
  }

  // ===== Slot-based operations (Challenge 4 fast locals) =====

  void defineAt(int index, Object value) {
    if (slots == null) {
      throw new IllegalStateException("defineAt() called on non-slot Environment.");
    }
    slots[index] = value;
  }

  Object getAt(int distance, int index) {
    Environment env = ancestor(distance);
    if (env.slots == null) {
      throw new IllegalStateException("getAt() reached non-slot Environment.");
    }

    Object value = env.slots[index];
    if (value == UNINITIALIZED) {
      // No Token here, so we make a dummy one for the RuntimeError.
      throw new RuntimeError(
          new Token(TokenType.IDENTIFIER, "<local>", null, -1),
          "Variable is not initialized.");
    }
    return value;
  }

  void assignAt(int distance, int index, Object value) {
    Environment env = ancestor(distance);
    if (env.slots == null) {
      throw new IllegalStateException("assignAt() reached non-slot Environment.");
    }
    env.slots[index] = value;
  }

    /**
   * Compatibility helper: return the value of a variable by name at a given
   * distance in the environment chain. This keeps older call sites (which use
   * name-based lookups like closure.getAt(0, "this")) working.
   */
  Object getAt(int distance, String name) {
    return ancestor(distance).values.get(name);
  }
}