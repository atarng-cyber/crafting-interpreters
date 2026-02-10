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

    // Keep numbers clean (optional nicety)
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

  // Optional quick test
  public static void main(String[] args) {
    // (1 + 2) * (4 - 3)
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
    // Expected: 1 2 + 4 3 - *
  }
}
