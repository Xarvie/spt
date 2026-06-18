-- matches bench/listsum.spt  (Lua tables are 1-based; values 0..n-1 as in SPT)
local function bench(n)
  local xs = {}
  local i = 1
  while i <= n do xs[i] = i - 1; i = i + 1 end
  local s = 0
  local rep = 0
  while rep < n do
    local j = 1
    while j <= n do s = s + xs[j]; j = j + 1 end
    rep = rep + 1
  end
  return s
end
return bench
