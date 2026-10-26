--- lumen.test: small unit-test framework for game code.
--
--   local t = require "lumen.test"
--   t.describe("inventory", function()
--     t.it("adds items", function()
--       t.eq(inv:count("sword"), 1)
--     end)
--   end)
--   os.exit(t.run() and 0 or 1)
local M = {}

local suites = { { name = "", tests = {}, before = {}, after = {} } }
local current = suites[1]

---Deep equality (tables compared structurally, cycles safe).
function M.deep_equal(a, b, seen)
  if a == b then return true end
  if type(a) ~= "table" or type(b) ~= "table" then return false end
  seen = seen or {}
  if seen[a] == b then return true end
  seen[a] = b
  for k, v in pairs(a) do
    if not M.deep_equal(v, b[k], seen) then return false end
  end
  for k in pairs(b) do
    if a[k] == nil then return false end
  end
  return true
end

local function repr(v, depth)
  depth = depth or 0
  local tv = type(v)
  if tv == "string" then return string.format("%q", v) end
  if tv ~= "table" then return tostring(v) end
  if depth > 3 then return "{...}" end
  local parts, n = {}, 0
  local keys = {}
  for k in pairs(v) do keys[#keys + 1] = k end
  table.sort(keys, function(x, y) return tostring(x) < tostring(y) end)
  for _, k in ipairs(keys) do
    n = n + 1
    if n > 12 then parts[#parts + 1] = "..."; break end
    local key = (type(k) == "string" and k:match("^[%a_][%w_]*$")) and k or ("[" .. repr(k, depth + 1) .. "]")
    parts[#parts + 1] = key .. " = " .. repr(v[k], depth + 1)
  end
  return "{ " .. table.concat(parts, ", ") .. " }"
end
M.repr = repr

local function fail(msg, level)
  error(setmetatable({ message = msg }, { __tostring = function(s) return s.message end }), (level or 1) + 1)
end

function M.eq(actual, expected, msg)
  if not M.deep_equal(actual, expected) then
    fail((msg and (msg .. ": ") or "") .. "expected " .. repr(expected) .. ", got " .. repr(actual), 2)
  end
end
function M.neq(actual, unexpected, msg)
  if M.deep_equal(actual, unexpected) then
    fail((msg and (msg .. ": ") or "") .. "did not expect " .. repr(unexpected), 2)
  end
end
function M.truthy(v, msg) if not v then fail(msg or ("expected a truthy value, got " .. repr(v)), 2) end end
function M.falsy(v, msg) if v then fail(msg or ("expected a falsy value, got " .. repr(v)), 2) end end
function M.is_nil(v, msg) if v ~= nil then fail(msg or ("expected nil, got " .. repr(v)), 2) end end
function M.near(actual, expected, eps, msg)
  eps = eps or 1e-6
  if type(actual) ~= "number" or math.abs(actual - expected) > eps then
    fail((msg and (msg .. ": ") or "") .. "expected " .. tostring(expected) .. " ± " .. eps .. ", got " .. repr(actual), 2)
  end
end
function M.matches(s, pattern, msg)
  if type(s) ~= "string" or not s:find(pattern) then
    fail((msg and (msg .. ": ") or "") .. "expected " .. repr(s) .. " to match " .. pattern, 2)
  end
end
function M.type_is(v, t, msg)
  if type(v) ~= t then fail((msg and (msg .. ": ") or "") .. "expected type " .. t .. ", got " .. type(v), 2) end
end
---Asserts that fn raises an error; optionally checks the message against a Lua pattern.
function M.throws(fn, pattern, msg)
  local ok, err = pcall(fn)
  if ok then fail((msg and (msg .. ": ") or "") .. "expected an error, none was raised", 2) end
  if pattern and not tostring(err):find(pattern) then
    fail((msg and (msg .. ": ") or "") .. "error " .. repr(tostring(err)) .. " does not match " .. pattern, 2)
  end
end

function M.describe(name, fn)
  local parent = current
  current = { name = (parent.name ~= "" and (parent.name .. " > ") or "") .. name, tests = {}, before = {}, after = {} }
  for _, b in ipairs(parent.before) do current.before[#current.before + 1] = b end
  for _, a in ipairs(parent.after) do current.after[#current.after + 1] = a end
  suites[#suites + 1] = current
  fn()
  current = parent
end
function M.before_each(fn) current.before[#current.before + 1] = fn end
function M.after_each(fn) current.after[#current.after + 1] = fn end
function M.it(name, fn) current.tests[#current.tests + 1] = { name = name, fn = fn } end
function M.skip(name) current.tests[#current.tests + 1] = { name = name, skip = true } end

---Runs every registered test. Returns true when all passed, plus a result table.
function M.run(opts)
  opts = opts or {}
  local filter = opts.filter
  local passed, failed, skipped = 0, 0, 0
  local failures = {}
  local out = opts.print or print
  for _, s in ipairs(suites) do
    for _, t in ipairs(s.tests) do
      local full = (s.name ~= "" and (s.name .. " > ") or "") .. t.name
      if filter and not full:find(filter, 1, true) then
        -- filtered out
      elseif t.skip then
        skipped = skipped + 1
        out("  skip  " .. full)
      else
        local ok, err = true, nil
        for _, b in ipairs(s.before) do
          local ok2, e2 = pcall(b)
          if not ok2 then ok, err = false, e2; break end
        end
        if ok then ok, err = xpcall(t.fn, function(e)
          if type(e) == "table" then return e end
          return { message = tostring(e), trace = debug.traceback("", 2) }
        end) end
        for _, a in ipairs(s.after) do pcall(a) end
        if ok then
          passed = passed + 1
          if opts.verbose or os.getenv("LUMEN_TEST_VERBOSE") then out("  ok    " .. full) end
        else
          failed = failed + 1
          local msg = type(err) == "table" and err.message or tostring(err)
          failures[#failures + 1] = { name = full, message = msg, trace = type(err) == "table" and err.trace or nil }
          out("  FAIL  " .. full .. "\n        " .. tostring(msg):gsub("\n", "\n        "))
        end
      end
    end
  end
  out(string.format("%d passed, %d failed, %d skipped", passed, failed, skipped))
  return failed == 0, { passed = passed, failed = failed, skipped = skipped, failures = failures }
end

---Forgets all registered tests (useful between files in one process).
function M.reset()
  suites = { { name = "", tests = {}, before = {}, after = {} } }
  current = suites[1]
end

return M
