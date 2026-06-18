-- bench/bench_lua.lua — time the Lua benchmark programs with the same
-- best-of-K methodology as bench_spt.c. os.clock() is CPU time, which for these
-- single-threaded CPU-bound loops matches wall-clock. Prints "<name>\t<sec>\t<result>".
local benches = {
  { name = "intloop",   file = "bench/intloop.lua",   n = 20000000 },
  { name = "floatloop", file = "bench/floatloop.lua", n = 10000000 },
  { name = "listsum",   file = "bench/listsum.lua",   n = 3000 },
  { name = "mapsum",    file = "bench/mapsum.lua",    n = 2000 },
  { name = "fib",       file = "bench/fib.lua",        n = 32 },
}
local WARM, REPS = 2, 7
for _, b in ipairs(benches) do
  local bench = dofile(b.file)
  local result
  for _ = 1, WARM do result = bench(b.n) end
  local best = math.huge
  for _ = 1, REPS do
    local t0 = os.clock()
    result = bench(b.n)
    local dt = os.clock() - t0
    if dt < best then best = dt end
  end
  io.write(string.format("%s\t%.6f\t%.6g\n", b.name, best, result))
  io.flush()
end
