#ifndef CMP_SEMA_H
#define CMP_SEMA_H

#include "ast_visitor.h"
#include "error.h"
#include "fmt/core.h"
#include "scoped_table.h"
#include <utility>

namespace cmp {

inline void unreachable() { assert(false && "unreachable"); }

struct Context {
  // Current enclosing decls.
  std::vector<FuncDecl *> func_decl_stack;
  std::vector<EnumDecl *> enum_decl_stack;
  // Builtin types.
  // voidType exists to differentiate the type of FuncCallExprs whose
  // function no return values, from expressions that failed to typecheck.
  Type *void_type = nullptr;
  Type *int_type = nullptr;
  Type *char_type = nullptr;
  Type *string_type = nullptr;
};

// Maps a VarDecl to its borrow count in the current scope.
// To be stored inside a ScopedTable.
struct BorrowMap {
  // FIXME: unused.
  const VarDecl *decl = nullptr;

  // Number of occasions that this variable was borrowed.
  int immutable_borrow_count = 0;
  int mutable_borrow_count = 0;
};

struct BasicBlock {
  std::vector<Stmt *> stmts;
  std::vector<BasicBlock *> pred;
  std::vector<BasicBlock *> succ;
  bool walked = false;

  // Indicates whether it is guaranteed that a return statement is seen on
  // every possible control flow that leads to this basic block.
  bool returned_so_far = false;

  // True if this basic block contains a return statement.
  bool returns() const;

  // Walk and enumerate all children nodes and itself in post-order.
  // Used to implement the the reverse post-order traversal.
  void enumerate_postorder(std::vector<BasicBlock *> &walkList);
};

class Parser;

class Lifetime {
public:
  // There are two kinds of lifetimes:
  // 1. Exact lifetimes.
  // 2. Annotated lifetimes.
  // (TODO doc)
  enum { exact, annotated } kind;

  // Declaration that first introduced this Exact lifetime.
  Decl *decl = nullptr;

  // Annotation of the Annotated lifetimes.
  Name *lifetime_annot = nullptr;

  Lifetime(Decl *d) : kind(exact), decl(d) {}
  Lifetime(Name *a) : kind(annotated), lifetime_annot(a) {}
};

// Stores all of the semantic information necessary for semantic analysis
// phase.
struct Sema {
  const Source &source; // source text
  NameTable name_table; // name table

  // Memory pools.  Currently maintains simply a list of malloc()ed pointers for
  // batch freeing.
  std::vector<std::unique_ptr<AstNode>> node_pool;
  std::vector<Type *> type_pool;
  std::vector<Lifetime *> lifetime_pool;
  std::vector<BasicBlock *> basic_block_pool;

  // Declarations visible at the current scope, keyed by their Names.
  ScopedTable<Name *, Decl *> decl_table;

  // XXX: needed?
  ScopedTable<Name *, Type *> type_table;

  // Stores lifetimes that are alive at the current position.
  // Note that this variable is not meant to be used directly; use
  // start_lifetime*() functions to create lifetimes instead.
  ScopedTable<Lifetime *, Lifetime *> lifetime_table;

  // TODO: doc
  ScopedTable<const VarDecl *, BorrowMap> borrow_table;

  // TODO: organize.
  Context context;

  // List of generated errors.
  std::vector<Error> &errors;
  // List of error beacons found in the source text.
  std::vector<Error> &beacons;

  Sema(const Source &s, std::vector<Error> &e, std::vector<Error> &b)
      : source(s), errors(e), beacons(b) {}
  Sema(const Sema &) = delete;
  Sema(Sema &&) = delete;
  ~Sema();

  void scope_open();
  void scope_close();

  template <typename... Args> void error(size_t pos, Args &&... args);

  template <typename T, typename... Args> T *make_node(Args &&... args) {
    node_pool.emplace_back(new T{std::forward<Args>(args)...});
    return static_cast<T *>(node_pool.back().get());
  }
  template <typename T, typename... Args>
  T *make_node_pos(size_t pos, Args &&... args) {
    auto node = make_node<T>(std::forward<Args>(args)...);
    node->pos = pos;
    return node;
  }
  template <typename T, typename... Args>
  T *make_node_range(std::pair<size_t, size_t> range, Args &&... args) {
    auto node = make_node<T>(std::forward<Args>(args)...);
    node->pos = range.first;
    node->endpos = range.second;
    return node;
  }
  template <typename... Args>
  Lifetime *make_lifetime(Args &&... args) {
    lifetime_pool.emplace_back(new Lifetime{std::forward<Args>(args)...});
    return lifetime_pool.back();
  }
  BasicBlock *makeBasicBlock() {
    BasicBlock *bb = new BasicBlock;
    basic_block_pool.push_back(bb);
    return bb;
  }
};

void setup_builtin_types(Sema &s);

// Name binding pass.
// Name binding basically is a pass that simply links each Name to a Decl.
// It handles variable/function/struct declaration, redefinition/undeclared-uses
// checks, number of function arguments checks, etc.
// TODO: doc more.
class NameBinding : public AstVisitor<NameBinding> {
  Sema &sema;

public:
  NameBinding(Sema &s) : sema{s} {}
  bool success() const { return sema.errors.empty(); }

  void visitCompoundStmt(CompoundStmt *cs);
  void visitDeclRefExpr(DeclRefExpr *d);
  void visitCallExpr(CallExpr *f);
  void visitTypeExpr(TypeExpr *t);
  void visitVarDecl(VarDecl *v);
  void visitFuncDecl(FuncDecl *f);
  void visitStructDecl(StructDecl *s);
  void visitEnumVariantDecl(EnumVariantDecl *e);
  void visitEnumDecl(EnumDecl *e);
};

// Type checking pass.
class TypeChecker : public AstVisitor<TypeChecker, Type *> {
  Sema &sema;

public:
  TypeChecker(Sema &s) : sema{s} {}
  bool success() const { return sema.errors.empty(); }

  Type *visitAssignStmt(AssignStmt *as);
  Type *visitReturnStmt(ReturnStmt *rs);

  Type *visitIntegerLiteral(IntegerLiteral *i);
  Type *visitStringLiteral(StringLiteral *s);
  Type *visitDeclRefExpr(DeclRefExpr *d);
  Type *visitCallExpr(CallExpr *f);
  Type *visitStructDefExpr(StructDefExpr *s);
  Type *visitCastExpr(CastExpr *c);
  Type *visitMemberExpr(MemberExpr *m);
  Type *visitUnaryExpr(UnaryExpr *u);
  Type *visitParenExpr(ParenExpr *p);
  Type *visitBinaryExpr(BinaryExpr *b);
  Type *visitTypeExpr(TypeExpr *t);

  Type *visitVarDecl(VarDecl *v);
  Type *visitFuncDecl(FuncDecl *f);
  Type *visitStructDecl(StructDecl *s);
  Type *visitEnumVariantDecl(EnumVariantDecl *e);
  Type *visitEnumDecl(EnumDecl *e);
};

class ReturnChecker
    : public AstVisitor<ReturnChecker, BasicBlock *, BasicBlock *> {
  Sema &sema;

public:
  ReturnChecker(Sema &s) : sema{s} {}
  bool success() const { return sema.errors.empty(); }

  BasicBlock *visitStmt(Stmt *s, BasicBlock *bb);
  BasicBlock *visitCompoundStmt(CompoundStmt *cs, BasicBlock *bb);
  BasicBlock *visitIfStmt(IfStmt *is, BasicBlock *bb);

  BasicBlock *visitFuncDecl(FuncDecl *f, BasicBlock *bb);
};

class BorrowChecker : public AstVisitor<BorrowChecker, void> {
  Sema &sema;

  // Shows whether the current expression being visited is inside a return
  // statement, so that the borrow checker knows to check if this expression
  // borrows from a value declared in the local scope.
  bool in_return_stmt = false;

  // TODO
  bool in_annotated_func = false;

public:
  BorrowChecker(Sema &s) : sema{s} {}
  bool success() const { return sema.errors.empty(); }

  void visitCompoundStmt(CompoundStmt *cs);
  void visitAssignStmt(AssignStmt *as);
  void visitReturnStmt(ReturnStmt *rs);

  void visitExpr(Expr *e);
  void visitDeclRefExpr(DeclRefExpr *d);
  void visitCallExpr(CallExpr *f);
  void visitStructDefExpr(StructDefExpr *s);
  void visitUnaryExpr(UnaryExpr *u);

  void visitVarDecl(VarDecl *v);
  void visitFuncDecl(FuncDecl *f);
};

class CodeGenerator : public AstVisitor<CodeGenerator, void> {
  Sema &sema;
  int indent = 0;
  FILE *file;

  template <typename... Args> void emit_indent(Args &&...args) {
    fmt::print(file, "{:{}}", "", indent);
    fmt::print(file, std::forward<Args>(args)...);
  }

  template <typename... Args> void emit(Args &&...args) {
    fmt::print(file, std::forward<Args>(args)...);
  }

  struct IndentBlock {
    CodeGenerator &c;
    IndentBlock(CodeGenerator &c) : c{c} { c.indent += 2; }
    ~IndentBlock() { c.indent -= 2; }
  };

public:
  CodeGenerator(Sema &s, const char *fname) : sema{s} {
    file = fopen(fname, "w");
  }
  ~CodeGenerator() { fclose(file); }
  bool success() const { return sema.errors.empty(); }

  void visitFile(File *f);

  void visitIntegerLiteral(IntegerLiteral *i);
  void visitStringLiteral(StringLiteral *s);
  void visitDeclRefExpr(DeclRefExpr *d);
  void visitCallExpr(CallExpr *f);
  void visitStructDefExpr(StructDefExpr *s);
  void visitCastExpr(CastExpr *c);
  void visitMemberExpr(MemberExpr *m);
  void visitUnaryExpr(UnaryExpr *u);
  void visitParenExpr(ParenExpr *p);
  void visitBinaryExpr(BinaryExpr *b);
  void visitTypeExpr(TypeExpr *t);

  void visitExprStmt(ExprStmt *e);
  void visitAssignStmt(AssignStmt *a);
  void visitReturnStmt(ReturnStmt *r);
  void visitIfStmt(IfStmt *i);
  void visitBuiltinStmt(BuiltinStmt *b);

  void visitVarDecl(VarDecl *v);
  void visitFuncDecl(FuncDecl *f);
  void visitStructDecl(StructDecl *s);
  void visitExternDecl(ExternDecl *e);
};

} // namespace cmp

#endif
