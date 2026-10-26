---@meta
-- Lua 5.4 standard library, annotated for the Lumen language server.

_G = {}
---Lua version string.
---@type string
_VERSION = "Lua 5.4"

---Prints values to the Lumen console (tab separated).
---@param ... any
function print(...) end
---Converts any value to a string, honouring `__tostring`.
---@param v any
---@return string
function tostring(v) end
---Converts a string (in an optional base) or number to a number.
---@param v any
---@param base? integer
---@return number?
function tonumber(v, base) end
---Returns the type name of a value.
---@param v any
---@return string
function type(v) end
---Raises an error. Level 1 (default) points at the caller of `error`.
---@param message any
---@param level? integer
function error(message, level) end
---Raises an error when `v` is false or nil, otherwise returns all its arguments.
---@generic T
---@param v T
---@param message? any
---@return T
function assert(v, message, ...) end
---Calls `f` in protected mode.
---@param f function
---@param ... any
---@return boolean ok, any result
function pcall(f, ...) end
---Like pcall with a message handler.
---@param f function
---@param msgh function
---@param ... any
---@return boolean ok, any result
function xpcall(f, msgh, ...) end
---Iterates the array part of a table: 1, 2, 3 ... until the first nil.
---@param t table
---@return function iterator, table t, integer start
function ipairs(t) end
---Iterates every key of a table (order is not defined).
---@param t table
---@return function iterator, table t, any start
function pairs(t) end
---Returns the next key/value of a table after `index`.
---@param t table
---@param index? any
---@return any key, any value
function next(t, index) end
---Raw get without metamethods.
---@param t table
---@param k any
---@return any
function rawget(t, k) end
---Raw set without metamethods.
---@param t table
---@param k any
---@param v any
---@return table
function rawset(t, k, v) end
---Raw equality without metamethods.
---@param a any
---@param b any
---@return boolean
function rawequal(a, b) end
---Raw length without metamethods.
---@param v table|string
---@return integer
function rawlen(v) end
---Sets the metatable of a table.
---@generic T
---@param t T
---@param mt table?
---@return T
function setmetatable(t, mt) end
---Returns the metatable of a value.
---@param v any
---@return table?
function getmetatable(v) end
---Returns the arguments after index `n`, or the argument count with "#".
---@param n integer|string
---@param ... any
---@return any
function select(n, ...) end
---Collects garbage / controls the collector.
---@param opt? string
---@param arg? number
---@return any
function collectgarbage(opt, arg) end
---Compiles a chunk of Lua source.
---@param chunk string|function
---@param chunkname? string
---@param mode? string
---@param env? table
---@return function? fn, string? err
function load(chunk, chunkname, mode, env) end
---Loads and runs a Lua file.
---@param filename? string
---@return any
function dofile(filename) end
---Loads a Lua module (searched in the project and the Lumen runtime).
---@param name string
---@return any module
function require(name) end
---Emits a warning.
---@param msg string
---@param ... string
function warn(msg, ...) end

package = {}
---@type string
package.path = ""
---@type string
package.cpath = ""
---@type table<string, any>
package.loaded = {}
---@type table<string, function>
package.preload = {}
---@type table
package.searchers = {}

-- string ------------------------------------------------------------------------------------------------
string = {}
---@param s string
---@param i? integer
---@param j? integer
---@return integer ...
function string.byte(s, i, j) end
---@param ... integer
---@return string
function string.char(...) end
---@param s string
---@param pattern string
---@param init? integer
---@param plain? boolean
---@return integer? start, integer? stop, string ... captures
function string.find(s, pattern, init, plain) end
---C-style formatting.
---@param fmt string
---@param ... any
---@return string
function string.format(fmt, ...) end
---@param s string
---@param pattern string
---@return function iterator
function string.gmatch(s, pattern, init) end
---@param s string
---@param pattern string
---@param repl string|table|function
---@param n? integer
---@return string result, integer count
function string.gsub(s, pattern, repl, n) end
---@param s string
---@return integer
function string.len(s) end
---@param s string
---@return string
function string.lower(s) end
---@param s string
---@param pattern string
---@param init? integer
---@return string ...
function string.match(s, pattern, init) end
---@param s string
---@param n integer
---@param sep? string
---@return string
function string.rep(s, n, sep) end
---@param s string
---@return string
function string.reverse(s) end
---@param s string
---@param i integer
---@param j? integer
---@return string
function string.sub(s, i, j) end
---@param s string
---@return string
function string.upper(s) end
---@param fmt string
---@param ... any
---@return string
function string.pack(fmt, ...) end
---@param fmt string
---@param s string
---@param pos? integer
---@return any ...
function string.unpack(fmt, s, pos) end
---@param fmt string
---@return integer
function string.packsize(fmt) end

-- table -------------------------------------------------------------------------------------------------
table = {}
---@param list table
---@param sep? string
---@param i? integer
---@param j? integer
---@return string
function table.concat(list, sep, i, j) end
---@param list table
---@param pos integer|any
---@param value? any
function table.insert(list, pos, value) end
---@param list table
---@param pos? integer
---@return any
function table.remove(list, pos) end
---Sorts a list in place (not stable).
---@param list table
---@param comp? fun(a: any, b: any): boolean
function table.sort(list, comp) end
---@param ... any
---@return table
function table.pack(...) end
---@param list table
---@param i? integer
---@param j? integer
---@return any ...
function table.unpack(list, i, j) end
---@param a1 table
---@param f integer
---@param e integer
---@param t integer
---@param a2? table
---@return table
function table.move(a1, f, e, t, a2) end

-- math --------------------------------------------------------------------------------------------------
math = {}
---@type number
math.pi = 3.14159265358979
---@type number
math.huge = 1 / 0
---@type integer
math.maxinteger = 0
---@type integer
math.mininteger = 0
---@param x number
---@return number
function math.abs(x) end
---@param x number
---@return number
function math.ceil(x) end
---@param x number
---@return number
function math.floor(x) end
---@param x number
---@return number
function math.sqrt(x) end
---@param x number
---@return number
function math.sin(x) end
---@param x number
---@return number
function math.cos(x) end
---@param x number
---@return number
function math.tan(x) end
---@param x number
---@return number
function math.asin(x) end
---@param x number
---@return number
function math.acos(x) end
---@param y number
---@param x? number
---@return number
function math.atan(y, x) end
---@param x number
---@return number
function math.exp(x) end
---@param x number
---@param base? number
---@return number
function math.log(x, base) end
---@param x number
---@param ... number
---@return number
function math.max(x, ...) end
---@param x number
---@param ... number
---@return number
function math.min(x, ...) end
---@param x number
---@return number integral, number fractional
function math.modf(x) end
---@param x number
---@param y number
---@return number
function math.fmod(x, y) end
---@param x number
---@return number|nil
function math.tointeger(x) end
---@param x any
---@return string|nil
function math.type(x) end
---@param x integer
---@param y integer
---@return boolean
function math.ult(x, y) end
---Random number: float in [0,1), integer in [1,m] or [m,n].
---@param m? integer
---@param n? integer
---@return number
function math.random(m, n) end
---@param seed? integer
function math.randomseed(seed) end

-- os ----------------------------------------------------------------------------------------------------
os = {}
---CPU time used by the program, in seconds.
---@return number
function os.clock() end
---@param format? string
---@param time? integer
---@return string|table
function os.date(format, time) end
---@param t1 integer
---@param t2 integer
---@return number
function os.difftime(t1, t2) end
---@param name string
---@return string?
function os.getenv(name) end
---@param t? table
---@return integer
function os.time(t) end
---@return string
function os.tmpname() end
---@param code? integer|boolean
function os.exit(code) end

-- io (limited to the project folders by the sandbox policy) -------------------------------------------
io = {}
---@param filename string
---@param mode? string
---@return file? f, string? err
function io.open(filename, mode) end
---@param ... any
function io.write(...) end
---@param fmt? string|integer
---@return string?
function io.read(fmt) end
---@param filename? string
---@return function iterator
function io.lines(filename, ...) end

---@class file
---@field read fun(self: file, fmt?: string|integer): string?
---@field write fun(self: file, ...: any): file
---@field lines fun(self: file, ...: any): function
---@field close fun(self: file)
---@field seek fun(self: file, whence?: string, offset?: integer): integer?
---@field flush fun(self: file)
local file = {}

-- coroutine ---------------------------------------------------------------------------------------------
coroutine = {}
---@param f function
---@return thread
function coroutine.create(f) end
---@param co thread
---@param ... any
---@return boolean ok, any ...
function coroutine.resume(co, ...) end
---@param ... any
---@return any ...
function coroutine.yield(...) end
---@param co thread
---@return string
function coroutine.status(co) end
---@param f function
---@return function
function coroutine.wrap(f) end
---@return thread co, boolean ismain
function coroutine.running() end
---@return boolean
function coroutine.isyieldable() end
---@param co thread
---@return boolean ok, any err
function coroutine.close(co) end

-- utf8 --------------------------------------------------------------------------------------------------
utf8 = {}
---@type string
utf8.charpattern = ""
---@param ... integer
---@return string
function utf8.char(...) end
---@param s string
---@param i? integer
---@param j? integer
---@return integer? length
function utf8.len(s, i, j) end
---@param s string
---@param i? integer
---@param j? integer
---@return integer ...
function utf8.codepoint(s, i, j) end
---@param s string
---@param n integer
---@param i? integer
---@return integer?
function utf8.offset(s, n, i) end
---@param s string
---@return function iterator
function utf8.codes(s) end

-- debug -------------------------------------------------------------------------------------------------
debug = {}
---@param message? string
---@param level? integer
---@return string
function debug.traceback(message, level) end
---@param f function|integer
---@param what? string
---@return table
function debug.getinfo(f, what) end
