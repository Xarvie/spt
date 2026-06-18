-- matches bench/intloop.spt
local function bench(n)
  local s = 0
  local i = 0
  while i < n do
    s = s + (i * 7 + 13) % 101 - 50
    i = i + 1
  end
  return s
end
return bench
