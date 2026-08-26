#include <Cue/Foundation/NumberParsing.h>

static_assert(cue::parse_unsigned_decimal<unsigned int>(std::string_view("42")).value() == 42);
