#include "Lexer.hh"
#include <cstring>
#include <fstream>
#include <iostream>

extern void use_read(std::ifstream& in);

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "No filename specified\n";
    return 1;
  }

  auto src = source_from_file(argv[1]);
  Lexer lexer{src};

  while (true) {
    auto token = lexer.lex();

    if (std::holds_alternative<Eos>(token))
      break;
  }

  return 0;
}
