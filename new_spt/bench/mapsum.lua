-- matches bench/mapsum.spt (integer-keyed map; same insert + repeated-sum shape)
local function bench(n)
  local m = {}
  local i = 0
  while i < n do m[i] = i * 2; i = i + 1 end
  local s = 0
  local rep = 0
  while rep < n do
    local j = 0
    while j < n do s = s + m[j]; j = j + 1 end
    rep = rep + 1
  end
  return s
end
return bench
