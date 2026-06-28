# for_iter 报告

生成时间: 2026-06-28 19:25:00
SptScript: build\bin\sptscript.exe
BenchFile: bench\for_iter_compare.spt
Reps: 3 (best-of)

## 结果汇总

| 场景 | JIT 关 (s) | JIT 开 (s) | 加速比 | sum 一致 |
|------|-----------|-----------|--------|---------|
| iter_list_1var | 0.2300 | 0.1860 | 1.24x | yes |
| iter_list_2var | 0.2290 | 0.1870 | 1.22x | yes |
| iter_map_1var | 0.1840 | 0.2030 | 0.91x | yes |
| pairs_list_2var | 0.1960 | 0.1640 | 1.20x | yes |
| pairs_map_2var | 0.1960 | 0.2200 | 0.89x | yes |

## JIT 关 详细输出
```
iter_list_1var : 0.23s sum=495000000
iter_list_2var : 0.229s sum=495000000
iter_map_1var : 0.184s sum=990000000
pairs_list_2var : 0.196s sum=495000000
pairs_map_2var : 0.196s sum=990000000
```

## JIT 开 详细输出
```
iter_list_1var : 0.186s sum=495000000
iter_list_2var : 0.187s sum=495000000
iter_map_1var : 0.203s sum=990000000
pairs_list_2var : 0.164s sum=495000000
pairs_map_2var : 0.22s sum=990000000
```
