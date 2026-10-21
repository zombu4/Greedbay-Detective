#pragma once

namespace app::model {

// The model runner (DESIGN 3.6, item 2): the app started with
// "--model-runner" (src/app/main.cpp). One JSON object per line on standard
// input, one answer per line on standard output; nothing else is written to
// standard output (the engine's own messages are dropped).
//
// {"op":"load","path":<file>,"sha256":<hex>,"gpu":true|false,"format":"qwen3"|"chatml"}
//   -> {"ok":true,"ms":<n>} or {"ok":false,"error":<text>}
//   The file must be a regular file (no link) whose SHA-256 is the given
//   value; it is checked before every load.
// {"op":"generate","prompt":[{"text":..,"special":bool},..],"grammar":"identity"|"sort"}
//   -> {"ok":true,"text":<answer>,"ms":<n>} or {"ok":false,"error":<text>}
//   Greedy decoding under the grammar (model/Prompts), at most 160 tokens.
// End of input ends the runner.
int runModelRunner();

} // namespace app::model
