#pragma once
#include <istream>
#include <ostream>

// Reads UCI commands from `in` until "quit" or EOF, answering on `out`.
void uci_loop(std::istream& in, std::ostream& out);
