-- matches bench/fib.spt
local function bench(n)
  if n < 2 then return n end
  return bench(n - 1) + bench(n - 2)
end
return bench
