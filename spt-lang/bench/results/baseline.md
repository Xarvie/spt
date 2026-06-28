# for_iter 报告

生成时间: 2026-06-28 16:36:58
SptScript: build\bin\sptscript.exe
BenchFile: bench\for_iter_baseline.spt
Reps: 3 (best-of)

## 结果汇总

| 场景 | JIT 关 (s) | JIT 开 (s) | 加速比 | sum 一致 |
|------|-----------|-----------|--------|---------|
| custom_iter | 0.2120 | 0.2350 | 0.90x | yes |
| list_2var_pairs | 0.2030 | 0.1690 | 1.20x | yes |
| map_2var_pairs | 0.2020 | 0.2250 | 0.90x | yes |
| numeric_for | 0.0890 | 0.0110 | 8.09x | yes |

## JIT 关 详细输出
```
custom_iter : 0.212s sum=495000000
list_2var_pairs : 0.203s sum=495000000
map_2var_pairs : 0.202s sum=990000000
numeric_for : 0.089s sum=495000000
```

## JIT 开 详细输出
```
custom_iter : 0.235s sum=495000000
list_2var_pairs : 0.169s sum=495000000
map_2var_pairs : 0.225s sum=990000000
numeric_for : 0.011s sum=495000000
```
