#pragma once
#include <istream>
#include <ostream>
#include <string>

// Reads UCI commands from `in` until "quit" or EOF, answering on `out`.
void uci_loop(std::istream& in, std::ostream& out);

// Writes one whole line to `out` and flushes. The UCI loop and the search thread both
// print, so every engine output goes through here under a lock to keep lines intact.
void say(std::ostream& out, const std::string& line);
