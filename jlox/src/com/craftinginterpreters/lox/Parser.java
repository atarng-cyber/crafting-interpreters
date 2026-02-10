package com.craftinginterpreters.lox;

import java.util.List;

import static com.craftinginterpreters.lox.TokenType.*;

class Parser {
  private static class ParseError extends RuntimeException {}

  private final List<Token> tokens;
  private int current = 0;

  Parser(List<Token> tokens) {
    this.tokens = tokens;
  }

  Expr parse() {
    try {
      return expression();
    } catch (ParseError error) {
      return null;
    }
  }

  // expression → comma ;
  private Expr expression() {
    return comma();
  }

  // comma → conditional ( "," conditional )* ;
  // Lowest precedence, left-associative (like C).
  private Expr comma() {
    Expr expr = conditional();

    while (match(COMMA)) {
      Token operator = previous();
      Expr right = conditional();
      expr = new Expr.Binary(expr, operator, right);
    }

    return expr;
  }

  // conditional → equality ( "?" expression ":" conditional )? ;
  // Right-associative (like C).
  private Expr conditional() {
    Expr expr = equality();

    if (match(QUESTION)) {
      Expr thenBranch = expression(); // in C grammar, middle is "expression" (comma allowed)
      consume(COLON, "Expect ':' after then-branch of conditional expression.");
      Expr elseBranch = conditional(); // recursion on the right makes it right-associative
      expr = new Expr.Conditional(expr, thenBranch, elseBranch);
    }

    return expr;
  }

  // equality → comparison ( ( "!=" | "==" ) comparison )* ;
  private Expr equality() {
    Expr expr = comparison();

    while (match(BANG_EQUAL, EQUAL_EQUAL)) {
      Token operator = previous();
      Expr right = comparison();
      expr = new Expr.Binary(expr, operator, right);
    }

    return expr;
  }

  // comparison → term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
  private Expr comparison() {
    Expr expr = term();

    while (match(GREATER, GREATER_EQUAL, LESS, LESS_EQUAL)) {
      Token operator = previous();
      Expr right = term();
      expr = new Expr.Binary(expr, operator, right);
    }

    return expr;
  }

  // term → factor ( ( "-" | "+" ) factor )* ;
  private Expr term() {
    Expr expr = factor();

    while (match(MINUS, PLUS)) {
      Token operator = previous();
      Expr right = factor();
      expr = new Expr.Binary(expr, operator, right);
    }

    return expr;
  }

  // factor → unary ( ( "/" | "*" ) unary )* ;
  private Expr factor() {
    Expr expr = unary();

    while (match(SLASH, STAR)) {
      Token operator = previous();
      Expr right = unary();
      expr = new Expr.Binary(expr, operator, right);
    }

    return expr;
  }

  // unary → ( "!" | "-" ) unary | primary ;
  private Expr unary() {
    if (match(BANG, MINUS)) {
      Token operator = previous();
      Expr right = unary();
      return new Expr.Unary(operator, right);
    }

    return primary();
  }

  // primary → literals | "(" expression ")" ;
  // PLUS: error productions for missing left operand for binary ops.
  private Expr primary() {
    if (match(FALSE)) return new Expr.Literal(false);
    if (match(TRUE)) return new Expr.Literal(true);
    if (match(NIL)) return new Expr.Literal(null);

    if (match(NUMBER, STRING)) {
      return new Expr.Literal(previous().literal);
    }

    if (match(LEFT_PAREN)) {
      Expr expr = expression();
      consume(RIGHT_PAREN, "Expect ')' after expression.");
      return new Expr.Grouping(expr);
    }

    // --- Error productions: binary operator without left-hand operand ---
    // Detect binary operator at beginning of an expression and recover by
    // parsing/discarding a right operand with the correct precedence.
    TokenType t = peek().type;
    switch (t) {
      case PLUS: { // unary + not allowed in Lox
        error(peek(), "Expect left-hand operand before '+'.");
        advance(); // consume '+'
        Expr rhs = factor(); // '+' sits at term level: rhs should be factor
        return rhs;
      }
      case STAR: {
        error(peek(), "Expect left-hand operand before '*'.");
        advance();
        Expr rhs = unary(); // '*' sits at factor level: rhs should be unary
        return rhs;
      }
      case SLASH: {
        error(peek(), "Expect left-hand operand before '/'.");
        advance();
        Expr rhs = unary(); // '/' sits at factor level
        return rhs;
      }
      case GREATER:
      case GREATER_EQUAL:
      case LESS:
      case LESS_EQUAL: {
        error(peek(), "Expect left-hand operand before comparison operator.");
        advance();
        Expr rhs = term(); // comparisons operate on term operands
        return rhs;
      }
      case EQUAL_EQUAL:
      case BANG_EQUAL: {
        error(peek(), "Expect left-hand operand before equality operator.");
        advance();
        Expr rhs = comparison(); // equality operates on comparison operands
        return rhs;
      }
      case COMMA: {
        error(peek(), "Expect left-hand operand before ','.");
        advance();
        Expr rhs = conditional(); // comma operates on conditional operands
        return rhs;
      }
      default:
        break;
    }

    throw error(peek(), "Expect expression.");
  }

  /* ----- Helper parsing primitives ----- */

  private boolean match(TokenType... types) {
    for (TokenType type : types) {
      if (check(type)) {
        advance();
        return true;
      }
    }
    return false;
  }

  private Token consume(TokenType type, String message) {
    if (check(type)) return advance();
    throw error(peek(), message);
  }

  private boolean check(TokenType type) {
    if (isAtEnd()) return false;
    return peek().type == type;
  }

  private Token advance() {
    if (!isAtEnd()) current++;
    return previous();
  }

  private boolean isAtEnd() {
    return peek().type == EOF;
  }

  private Token peek() {
    return tokens.get(current);
  }

  private Token previous() {
    return tokens.get(current - 1);
  }

  private ParseError error(Token token, String message) {
    Lox.error(token, message);
    return new ParseError();
  }
}
