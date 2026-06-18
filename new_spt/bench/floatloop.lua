-- matches bench/floatloop.spt
local function bench(n)
  local s = 0.0
  local x = 0.5
  local i = 0
  while i < n do
    s = s + x * x - x
    x = x + 0.0000001
    i = i + 1
  end
  return s
end
return bench
