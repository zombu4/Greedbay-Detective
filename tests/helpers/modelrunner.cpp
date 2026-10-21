// The real model runner (model/Runner.cpp, on the engine) as a program of
// its own for tests of its refusals: a missing file, a link, a wrong SHA-256,
// a question before any model. No model file is ever loaded in tests.

#include "model/Runner.h"

int main() { return app::model::runModelRunner(); }
