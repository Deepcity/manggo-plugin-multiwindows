import { cpSync, mkdirSync } from "node:fs";
import { resolve } from "node:path";

const root = resolve(import.meta.dirname, "..");
const source = resolve(root, "packages/companion/renderer");
const destination = resolve(root, "packages/companion/dist/renderer");
mkdirSync(destination, { recursive: true });
cpSync(source, destination, { recursive: true, force: true });
