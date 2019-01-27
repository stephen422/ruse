#include "ast.h"
#include <sstream>

namespace comp {

int AstNode::depth = 0;

void DeclStmt::print() const {
    out() << "[DeclStmt]\n";

    PrintScope start;

    decl->print();
}

void VarDecl::print() const {
    out() << "[VarDecl]\n";

    PrintScope start;

    out() << "[Id] " << id.text << "\n";
    out() << "[Mutable:" << (mut ? "Y" : "N") << "]\n";
    if (rhs != nullptr) {
        out() << "[RHS]\n";
        PrintScope start;
        rhs->print();
    }
}

void BinaryExpr::print() const {
    out() << "[BinaryExpr]\n";

    PrintScope start;

    lhs->print();
    out() << "[Op] '" << op.text << "'\n";
    rhs->print();
}

std::string BinaryExpr::flatten() const {
    return "(" + lhs->flatten() + std::string(op.text) + rhs->flatten() + ")";
}

void LiteralExpr::print() const {
    out() << "[LiteralExpr] " << lit.text << std::endl;
}

std::string LiteralExpr::flatten() const {
    return lit.text;
}

} // namespace comp
