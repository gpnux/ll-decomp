# 工具入口

命令在仓库根目录执行。环境准备和构建流程见 [README](../README.md)。

## 日常使用

| 入口 | 用途 |
|---|---|
| `make split` / `gba_split.py` | 从原版 ROM、分段配置和当前源码生成参考汇编与资源 |
| `make` / `make check` | 编译并校验 ROM / 仅比较已有 ROM 与原版的 SHA1 |
| `make objdiff` | 编译工作对象与参考对象，供 Objdiff 比较 |
| `make objdiff-config` / `gba_objdiff.py` | 生成当前模块的 Objdiff 配置 |
| `make compdb` / `gba_compdb.py` | 按 Makefile 的实际 C 模块和 include 路径生成 clangd 编译数据库 |
| `bin2c.py` | 将资源字节转换为 C 数值或数组 |
| `charmap_for_preproc.py` | 生成原版 preproc 可读取的四套码表，供当前 Makefile 使用 |

`gba/` 是分割、符号、资源与 Objdiff 配置共用的库，`gba/tests/` 保存分割和汇编、链接测试。

运行工具测试：

```sh
.venv/bin/python -m unittest discover -s tools/gba/tests -v
```

## 工具链

- `agbcc/`：游戏模块使用 `agbcc`，部分 SDK 模块使用 `old_agbcc`。
- `preproc/`：将文本和码表交给 C 编译器前进行预处理。
- `gbadisasm/`：拆分器使用的 ARM/Thumb 反汇编器，包含构建所需的 Capstone 源码包。

clangd 使用 ARM7TDMI、GNU C89 和实际预处理搜索路径。旧版编译器允许的指针类型转换在编辑器中保留为警告；该配置不改变 ROM 编译器及其选项。
