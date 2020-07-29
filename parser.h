#ifndef CMP_PARSER_H
#define CMP_PARSER_H

#include "ast.h"
#include "lexer.h"
#include "sema.h"
#include <variant>

namespace cmp {

// ParserResult wraps the result of a single parse operation, i.e. the
// resulting AST node in the successful case, or an error object in the failing
// case. It also marks the position where the parse operation started (TODO).
// With these information, it enables several helpful features:
//
//   1. it allows the caller to easily recover from the parse failure, using the
//   marked position info;
//
//   2. it enables the parser to proceed and try alternative productions
//   without being interrupted by an error generated in the failed production;
//
//   3. it allows the caller to overwrite the error message with a more
//   descriptive, context-aware one.
template <typename T> class ParserResult {
public:
    // Uninitialized
    ParserResult() {}

    // Successful result
    // U is the more-specific type.
    template <typename U>
    ParserResult(U *ptr) : result(ptr) {}

    // Erroneous result
    ParserResult(const Error &error) : result(error) {}

    // Upcasting
    template <typename U>
    ParserResult(ParserResult<U> &&res) {
        if (res.success()) {
            result = static_cast<T *>(res.ptr());
        } else {
            result = res.error();
        }
    }

    // Returns 'res', provided there were no errors; if there were, report them
    // and cause the compiler to exit.
    T *unwrap();

    // Get the stored node pointer.
    T *ptr() const { return std::get<T *>(result); }

    // Get the stored Error object.
    const Error &error() const { return std::get<Error>(result); }

    // Is this result successful?
    bool success() { return std::holds_alternative<T *>(result); }

private:
    std::variant<T *, Error> result;
};

Name *name_of_derived_type(NameTable &names, TypeExprKind kind,
                           Name *referee_name);

class Parser {
public:
  Lexer &lexer;
  Sema &sema;

  Token tok;                                   // lookahead token
  std::vector<std::unique_ptr<AstNode>> nodes; // node pointer pool
  AstNode *ast = nullptr;                      // resulting AST

  // End position of the last consumed token.
  // Used for tracking the range of the current token.
  size_t last_tok_endpos = 0;

  // Token cache.
  // These are data structures that enable flexible roll-back of the parsing
  // state.
  // Cache of the lookahead tokens.
  std::vector<Token> token_cache;
  // Index of the token to be read in the next next() call.
  size_t next_read_pos = 0;

  struct State {
    Token tok;
    size_t last_tok_endpos;
    size_t next_read_pos;
    size_t error_count;
  };
  State save_state();
  void restore_state(State state);

  Parser(Lexer &lexer, Sema &sema);
  Ast parse();

private:
  // Parse the whole file.
  File *parseFile();

  // Parse a toplevel statement.
  AstNode *parse_toplevel();

  // Statement parsers.
  Stmt *parse_stmt();
  Stmt *parseExprOrAssignStmt();
  Stmt *parse_return_stmt();
  IfStmt *parse_if_stmt();
  DeclStmt *parseDeclStmt();
  CompoundStmt *parseCompoundStmt();
  BuiltinStmt *parseBuiltinStmt();
  bool is_end_of_stmt() const;
  bool is_eos() const;

  // Declaration parsers
  Decl *parseDecl();
  VarDecl *parseVarDecl(VarDeclKind kind);
  template <typename T, typename F>
  std::vector<T> parseCommaSeparatedList(F &&parseFn);
  FuncDecl *parseFuncDecl();
  StructDecl *parseStructDecl();
  EnumVariantDecl *parseEnumVariant();
  std::vector<EnumVariantDecl *> parseEnumVariantDeclList();
  EnumDecl *parseEnumDecl();
  bool isStartOfDecl();

  // Expression parsers
  Expr *parseExpr();
  Expr *parseUnaryExpr();
  Expr *parseLiteralExpr();
  Expr *parseFuncCallOrDeclRefExpr();
  Expr *parseTypeExpr();
  Expr *parseBinaryExprRhs(Expr *lhs, int precedence);
  Expr *parseMemberExprMaybe(Expr *expr);
  std::optional<StructFieldDesignator> parseStructDefField();
  bool lookaheadStructDef();
  Expr *parseStructDefMaybe(Expr *expr);

  // Error handling
  void error(const std::string &msg);
  void error_expected(const std::string &msg);

  // Advance the lookahead token.
  void next();

  // Expect and consume functions.
  bool expect(Tok kind, const std::string &msg);

  // Skip until a specific token(s) show up.
  void skip_until(Tok kind);
  void skip_until_any(std::initializer_list<Tok> &kinds);
  void skip_until_end_of_line();
  void skip_to_next_line();
  void skip_newlines();

  // Figure out the current location (line, col) in the source.
  SourceLoc locate() const { return lexer.source().locate(tok.pos); }

  // Convenience function for make_node_range.
  template <typename T, typename... Args>
  T *makeNodeHere(size_t pos, Args &&... args) {
    return sema.make_node_range<T>({pos, last_tok_endpos},
                                   std::forward<Args>(args)...);
  }
};

} // namespace cmp

#endif
