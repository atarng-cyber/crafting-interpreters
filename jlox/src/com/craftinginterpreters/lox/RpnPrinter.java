package com.craftinginterpreters.lox;

class RpnPrinter implements Expr.Visitor<String> {

  String print(Expr expr) {
    return expr.accept(this);
  }

  @Override
  public String visitBinaryExpr(Expr.Binary expr) {
    String left = expr.left.accept(this);
    String right = expr.right.accept(this);
    return left + " " + right + " " + expr.operator.lexeme;
  }

  @Override
  public String visitGroupingExpr(Expr.Grouping expr) {
    return expr.expression.accept(this);
  }

  @Override
  public String visitLiteralExpr(Expr.Literal expr) {
    if (expr.value == null) return "nil";

    // Optional nicety: print integers without .0
    if (expr.value instanceof Double) {
      double d = (Double) expr.value;
      if (d == (long) d) return Long.toString((long) d);
      return Double.toString(d);
    }

    return expr.value.toString();
  }

  @Override
  public String visitUnaryExpr(Expr.Unary expr) {
    String right = expr.right.accept(this);
    return right + " " + expr.operator.lexeme;
  }

  @Override
  public String visitVariableExpr(Expr.Variable expr) {
    return expr.name.lexeme;
}

  @Override
  public String visitAssignExpr(Expr.Assign expr) {
  // In RPN: <value> <name> =
    String value = expr.value.accept(this);
    return value + " " + expr.name.lexeme + " =";
}

  @Override
public String visitLogicalExpr(Expr.Logical expr) {
  String left = expr.left.accept(this);
  String right = expr.right.accept(this);
  return left + " " + right + " " + expr.operator.lexeme;
}

  // Optional quick test
  public static void main(String[] args) {
    // (1 + 2) * (4 - 3)  ->  1 2 + 4 3 - *
    Expr expression = new Expr.Binary(
        new Expr.Grouping(
            new Expr.Binary(
                new Expr.Literal(1.0),
                new Token(TokenType.PLUS, "+", null, 1),
                new Expr.Literal(2.0))),
        new Token(TokenType.STAR, "*", null, 1),
        new Expr.Grouping(
            new Expr.Binary(
                new Expr.Literal(4.0),
                new Token(TokenType.MINUS, "-", null, 1),
                new Expr.Literal(3.0))));

    System.out.println(new RpnPrinter().print(expression));
  }


@Override
public String visitFunctionExpr(Expr.Function expr) {
  // RPN for a function literal isn't super meaningful; just show a marker.
  // If you want: include param count.
  return "fun";
}

@Override
public String visitCallExpr(Expr.Call expr) {
  StringBuilder b = new StringBuilder();
  b.append(expr.callee.accept(this));
  for (Expr arg : expr.arguments) {
    b.append(" ").append(arg.accept(this));
  }
  b.append(" call");
  return b.toString();
}

@Override
public String visitGetExpr(Expr.Get expr) {
  return expr.object.accept(this) + " " + expr.name.lexeme + " get";
}

@Override
public String visitSetExpr(Expr.Set expr) {
  return expr.object.accept(this) + " " + expr.value.accept(this) + " " + expr.name.lexeme + " set";
}

@Override
public String visitThisExpr(Expr.This expr) {
  return "this";
}

@Override
public String visitSuperExpr(Expr.Super expr) {
  return "super " + expr.method.lexeme;
}

@Override
public String visitInnerExpr(Expr.Inner expr) {
  return "inner";
}
}
