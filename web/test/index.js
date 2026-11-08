// SPDX-License-Identifier: GPL-3.0-only
// `node --test web/test/` runs this file: Node resolves a directory argument to its index.js rather than walking it.
// It loads every *.test.mjs file in this folder. (To run them one by one instead: node --test web/test/*.test.mjs)
const { readdirSync } = require("node:fs");
for (const f of readdirSync(__dirname).filter((n) => n.endsWith(".test.mjs")).sort()) import(`./${f}`);
