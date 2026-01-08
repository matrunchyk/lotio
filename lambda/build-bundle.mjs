#!/usr/bin/env node
/**
 * esbuild bundler script for lotio-lambda (ESM)
 * - Produces handler.js (ESM) in /build
 * - Avoids "Dynamic require of 'timers' is not supported"
 */

import { build } from "esbuild";
import { resolve } from "path";

const projectRoot = process.cwd();
const entryPoint = resolve(projectRoot, "src/lambdas/lotio-lambda/handler.ts");
const outfile = resolve(projectRoot, "handler.js");

/**
 * Keep bundle ESM, but ensure any remaining CJS-style require() can work in ESM.
 * This is a safety net for stubborn dependencies.
 */
const esmRequireBanner = `
import { createRequire } from "node:module";
const require = createRequire(import.meta.url);
`;

try {
  await build({
    entryPoints: [entryPoint],
    outfile,

    bundle: true,
    platform: "node",
    target: "node22",
    format: "esm",

    // IMPORTANT:
    // Do NOT externalize Node built-ins like "timers".
    // Let esbuild handle them correctly for Node ESM.
    external: [
      // Only keep externals you truly want to load at runtime.
      // If you remove these, they will be bundled (often easiest).
      "aws-lambda",
      // Consider removing @aws-sdk/* unless you are sure it's available at runtime.
      "@aws-sdk/*",
    ],

    mainFields: ["module", "main"],
    resolveExtensions: [".ts", ".js", ".mjs"],

    // If your dependencies include conditional exports, these help:
    conditions: ["node", "import"],
    // Optional: keep it compact; enable sourcemap while debugging
    sourcemap: false,
    minify: true,
    treeShaking: true,
    logLevel: "info",

    // Keep this if you rely on workspace packages resolution
    packages: "bundle",
    nodePaths: [
      resolve(projectRoot, "node_modules"),
      resolve(projectRoot, "packages"),
    ],

    // Safety net to allow require() inside ESM when a dependency insists
    banner: { js: esmRequireBanner },
  });

  console.log("✅ Bundle created successfully:", outfile);
} catch (error) {
  console.error("❌ Bundle failed:", error);
  process.exit(1);
}
