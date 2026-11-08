// SPDX-License-Identifier: GPL-3.0-only
// TRIGDECK's controller logic for the web version (a port of Sources/DDJMapping/, without the SLOOP cartridge's
// GrooveboxMapper and the accessibility ScreenReader).
export { DDJ400Decoder } from "./decoder.js";
export { DDJPadFunction, FM1Keyboard } from "./padfunction.js";
export { DDJ400LEDWriter } from "./ledwriter.js";
export { TrigMapper, padFunctionOf, PAD_FUNCTION_NAME } from "./trigmapper.js";
