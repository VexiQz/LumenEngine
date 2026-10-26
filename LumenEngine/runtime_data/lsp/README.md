# Language server

`lumen-lsp` is a standalone Lua language server (diagnostics, typed completion, hover, go to definition, references, rename,
signature help, document and workspace symbols). It reads the annotations in `lua/api` and every `.lua` file of the project.

* stdio: `lumen-lsp --api lumen_runtime/lua/api --module lumen_runtime/lua`
* TCP (started by the editor): `lumen-lsp --tcp <port>`; the editor writes its port to `lsp/port.txt`.

VS Code: use any generic LSP client extension with the command above, or copy `lumen.luarc.json` to your project as `.luarc.json`
to let sumneko LuaLS use the same definitions.
