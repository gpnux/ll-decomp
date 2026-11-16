# Lunar Legend 项目使用教程

## 准备环境

需要 Python 3.10+、GNU Make、主机编译器 GCC/G++ 和 ARM 工具链，确保 `arm-none-eabi-cpp`、`arm-none-eabi-as`、`arm-none-eabi-ld`、`arm-none-eabi-objcopy` 可用。项目附带 agbcc；preproc 和 gbadisasm 会在首次使用时从附带源码编译。

将原版 ROM 命名为 `baserom.gba`，放在仓库根目录。在根目录创建 Python 环境并安装依赖：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

## 分割和构建 ROM

首次使用时执行：

```sh
make split
make -j4
```

`make split` 按 [config/splat.ll.yaml](config/splat.ll.yaml) 和 [config/ll.cfg](config/ll.cfg) 生成汇编、资源与 Objdiff 配置；`make -j4` 编译并链接 ROM，随后比较生成 ROM 与 `baserom.gba` 的 SHA1。

分割采用 splat 的源码分类方式：已有 C 文件中被 `INCLUDE_ASM` 引用的入口写入 `asm/nonmatchings/<模块>/`，其余入口写入 `asm/matchings/<模块>/`。扫描包括未启用分支中的汇编回退，忽略注释和字符串；分割不运行预处理器或 C 编译器。目录表示源码引用情况，构建末尾的 SHA1 比较检查生成 ROM 是否与原版一致。

构建产物为：

- `ll.gba`：生成的 ROM。
- `build/ll.elf`：带符号的 ELF。
- `build/ll.map`：链接地址映射。
- `build/<模块>.o`：工作对象。

只校验已有 ROM 时执行：

```sh
make check
```

从头重新编译并准备 Objdiff 参考对象时执行：

```sh
make clean
make -j4 all objdiff
```

`make objdiff` 编译工作对象和参考对象，供 Objdiff 比较。确认某个函数已由 C 实现时，还需检查 `build/<模块>.o.s` 中的编译器输出及汇编依赖；ROM 一致和源码分类本身不证明 C 来源。

## 修改源码和配置

修改 C、汇编或头文件后，直接增量构建：

```sh
make -j4
```

完成或撤回 C 实现后，运行 `make split` 刷新源码分类，再运行 `make -j4` 构建并校验 ROM；普通 `make` 不自动重新分类。

修改分段 YAML、`config/ll.cfg`、[函数符号](config/symbols.ll.txt) 或 [数据符号](config/symbols.ll.data.txt) 后，重新分割并构建：

```sh
make split
make -j4
```

函数名称写入 `config/symbols.ll.txt`，BSS、只读数据和固定地址名称写入 `config/symbols.ll.data.txt`；两个文件都按地址排序。已恢复 RAM 定义的模块改名时，同步修改 [config/ram.ll.ld](config/ram.ll.ld) 中的对象路径，再运行上述命令。

确实需要的固定地址解析直接写入 `asm/linker.ld`；已有 C 存储定义和 RAM 布局中的定义不重复生成 `PROVIDE`。数据符号配置记录原地址，实际存储仍由 C 对象和 RAM 链接布局提供。

只编译指定模块或强制重编译时执行：

```sh
make build/menu.o expected/menu.o
make -B build/menu.o
```

工具入口与构建依赖见 [tools/README.md](tools/README.md)。

