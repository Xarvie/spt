# SPT 语言变更记录

## for-each iter() 内置函数 + 语法糖

### 新增
- `iter(nvars, obj)` 内置函数：按对象类型分发 list/map 迭代器
- Parser 自动包：`for (auto v : l)` → `for (auto __it_k, v : iter(2, l))`
- JIT 支持 `iter_list_next`（与 pairs 共享 list lowering）
- `ipairs` 修复：返回 4 值（SPT 协议）+ list 越界检查

### 语义
- `for (auto v : l)` — list 值迭代
- `for (auto i, v : l)` — list 索引+值
- `for (auto v : m)` — map 值迭代
- `for (auto k, v : m)` — map 键值
- `for (auto x : foo())` — call 透传（不包）
- `for (auto x : a, b, c)` — 多表达式透传（兼容旧式）

### JIT
- list 1/2 变量：JIT 加速（iter_list_next == pairs luaB_next lowering）
- map：回退解释器（SPTIR_CALL 无后端 lowering，未来扩展）

### 性能基准（bench/results/compare.md）
| 场景 | JIT 关 (s) | JIT 开 (s) | 加速比 |
|------|-----------|-----------|--------|
| iter_list_1var | 0.2300 | 0.1860 | 1.24x |
| iter_list_2var | 0.2290 | 0.1870 | 1.22x |
| pairs_list_2var | 0.1960 | 0.1640 | 1.20x |
| iter_map_1var | 0.1840 | 0.2030 | 0.91x |
| pairs_map_2var | 0.1960 | 0.2200 | 0.89x |

iter list JIT 加速 ≈ pairs list JIT（同一 lowering），map 不 JIT（回退解释器）。

### 注意事项
- 隐藏控制变量 `__it_k`：1 变量 for-each 时 parser 自动添加，使 C=2
- 不要在用户代码中声明名为 `iter` 的函数后使用 `for (auto x : list)` 语法糖（会被解析为调用用户函数而非内置 iter）
