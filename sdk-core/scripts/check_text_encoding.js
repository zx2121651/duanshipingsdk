#!/usr/bin/env node
/*
 * Checks source-like files for invalid UTF-8 and common mojibake sequences.
 * Intended as a lightweight CI guard for comments and string literals.
 */

const fs = require("fs");
const path = require("path");

const root = path.resolve(process.argv[2] || path.join(__dirname, "..", ".."));

const sourceExtensions = new Set([
  ".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".m", ".mm",
  ".java", ".kt", ".gradle", ".cmake",
  ".glsl", ".frag", ".vert",
  ".md", ".txt", ".json", ".xml", ".properties", ".sh", ".ps1", ".bat",
]);

const sourceNames = new Set([
  "CMakeLists.txt",
]);

const excludedDirNames = new Set([
  ".git",
  ".gradle",
  ".idea",
  ".vs",
  "build",
  "build-codex",
  "node_modules",
]);

const excludedPathParts = [
  `${path.sep}build${path.sep}`,
  `${path.sep}build-`,
  `${path.sep}build_`,
];

const mojibakePatterns = [
  "\uFFFD", // replacement character
  "\u9225", // common UTF-8-as-GBK mojibake: "鈥"
  "\u951B", // "锛"
  "\u9286", // "銆"
  "\u920B", // "鈋/鈫" neighborhood
  "\u9211", // "鈑/鈮" neighborhood
  "\u923B", // "鈻"
  "\u923C", // "鈼"
  "\u923D", // "鈽"
  "\u923E", // "鈾"
  "\u812B", // "脫"
  "\u8133", // "脳"
  "\u5749", // "坉"
  "\u5755", // "坕"
  "\u5719", // "圙"
  "\u5734", // "圴"
];

const utf8Decoder = new TextDecoder("utf-8", { fatal: true });

function shouldSkipDir(absPath) {
  const name = path.basename(absPath);
  if (excludedDirNames.has(name)) return true;
  return excludedPathParts.some((part) => absPath.includes(part));
}

function shouldScanFile(absPath) {
  const name = path.basename(absPath);
  if (sourceNames.has(name)) return true;
  return sourceExtensions.has(path.extname(name));
}

function walk(dir, out) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const abs = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      if (!shouldSkipDir(abs)) walk(abs, out);
    } else if (entry.isFile() && shouldScanFile(abs)) {
      out.push(abs);
    }
  }
}

function hasReplacementBytes(bytes) {
  for (let i = 0; i + 2 < bytes.length; i += 1) {
    if (bytes[i] === 0xef && bytes[i + 1] === 0xbf && bytes[i + 2] === 0xbd) {
      return true;
    }
  }
  return false;
}

function scanFile(absPath) {
  const rel = path.relative(root, absPath);
  const bytes = fs.readFileSync(absPath);
  const issues = [];

  if (hasReplacementBytes(bytes)) {
    issues.push({ file: rel, line: 0, reason: "contains UTF-8 replacement character bytes" });
  }

  let text;
  try {
    text = utf8Decoder.decode(bytes);
  } catch (error) {
    issues.push({ file: rel, line: 0, reason: `invalid UTF-8: ${error.message}` });
    return issues;
  }

  const lines = text.split(/\r?\n/);
  for (let i = 0; i < lines.length; i += 1) {
    const line = lines[i];
    const pattern = mojibakePatterns.find((p) => line.includes(p));
    if (pattern) {
      issues.push({
        file: rel,
        line: i + 1,
        reason: `possible mojibake sequence U+${pattern.codePointAt(0).toString(16).toUpperCase()}`,
        text: line.trim().slice(0, 180),
      });
    }
  }

  return issues;
}

const files = [];
walk(root, files);

const issues = [];
for (const file of files) {
  issues.push(...scanFile(file));
}

if (issues.length > 0) {
  console.error(`Encoding guard failed: ${issues.length} issue(s) in ${files.length} scanned file(s).`);
  for (const issue of issues.slice(0, 200)) {
    const loc = issue.line > 0 ? `${issue.file}:${issue.line}` : issue.file;
    console.error(`- ${loc}: ${issue.reason}`);
    if (issue.text) console.error(`  ${issue.text}`);
  }
  if (issues.length > 200) {
    console.error(`... ${issues.length - 200} more issue(s) omitted.`);
  }
  process.exit(1);
}

console.log(`Encoding guard passed: ${files.length} source-like file(s) are strict UTF-8 with no common mojibake.`);
