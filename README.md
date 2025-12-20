## punp - Simple Punctuation Processor 

一个基于C++编写的文本符号替换工具, 支持多线程将多文本按照制定规则将符号替换, 不依赖任何第三方库

由于本人有时写文档时容易中英文标点混用, 很不美观, 如果是写tex也可能会出问题, 借助像vscode等工具自带的查找替换工具一个标点一个标点替换过于麻烦, 遂写了这个项目, 凑合能用

### 规则编写说明

规则写在`.prules`中, 逻辑非常简单: 
- 注释: 
    - 行注释: `//`
    - 块注释: `/*  */`
- 规则:
    - 采用类似常见编程语言的函数调用风格, 一个语句的大致样式为 `KEYWORD(ARG_KEY "str", ...);`
        - 其中 `KEYWORD` 和 `ARG_KEY` 可以大小写混写, 但推荐全部大写
        - 对于有多个 `ARG_KEY` 的函数, 顺序可以任意, 如 `REPLACE()` 可以先写 `TO` 再写 `FROM`
        - 函数可换行写
        - **如果被 `""` 包起来的字符串中需要含有 `"` 需要加转义字符 `\`**
    - 替换相关:
        - 添加替换规则: `REPLACE(FROM "from str", TO "to str");`
        - 删除替换规则: `DEL(FROM "replace str");`
        - 清除当前已导入的替换规则: `CLEAR();`
    - 保护文本不被替换相关:
        - 添加保护区域规则: `PROTECT(START_MARKER "start marker", END_MARKER "end_marker");`
        - 添加指定保护内容规则: `PROTECT_CONTENT(CONTENT "protected content");`
            - 等价于 `PROTECT(START_MARKER "protected content", END_MARKER "");`
- 除了上面指定的规则外, 其他语句均不能正确识别
- 对于Linux, 先在`~/.local/share/punp/`中查找规则文件`.prules`, 然后再在当前目录中找

> 其实不仅可以对标点做修改, 也可以对任意字符修改替换, 只要自定义了相关规则即可

### 编译并安装程序

- 编译
```bash
cmake -Bbuild && cmake --build ./build
```
- 安装(首先确保已经编译通过)
```bash
cmake --install ./build
```

### 使用说明

- 程序后可接多个 option 和多个文件路径
- Options 说明: 
    - `-V`, `--version`: 版本信息
    - `-h`, `--help`: 使用说明
    - `-u`, `--update [stable|nightly]`: 自更新, 默认为 stable. 对于 stable, 将只更新到上一次标记 tag 的位置; 对于 nightly, 将更新到最新的一次 commit
    - `-r`, `--recursive`: 对一个目录递归的处理里面的文件
    - `-e`, `--extension`: 对导入文件路径中的文件按照文件后缀名过滤, 是否加 `.` 均可
    - `-v`, `--verbose`: 详细的结果输出
    - `-t`, `--threads <n>`: 使用的最大线程数, `n`为一个正整数, 默认情况下, 程序将使用 `min(n_task, hw_max_threads)` 个线程
    - `-E`, `--exclude <path>`: 排除指定文件/目录或通配符匹配的路径(可以多次使用). 注意在 shell 中使用 `*` 或 `?` 时建议加引号以避免被 shell 扩展
    - `-H`, `--hidden`: 将隐藏的文件和目录放入搜索空间中
    - `-n`, `--dry-run`: 进行一次不做任何更改的试运行, 仅打印将要处理的文件路径
    - `-f`, `--rule-file <path>`: 使用特定的配置文件路径而不是在当前目录中找
    - `-c`, `--console <rules>`: 允许直接在命令行写规则配置而不需要专门写一个配置文件
    - `--ignore-global-rule-file`: 不导入 `$HOME/.local/share/punp/.prules` 中的规则
    - `--enable-latex-jumping`: 尝试针对 latex 文件中 `\input` 和 `\include` 的 latex 文件递归跳转处理
    - `--show-example`: 使用示例以及说明
- 路径通配符:
    - `*`: 单跳通配符, 通配任意0个或任意多个字符
    - `?`: 单跳通配符, 通配任意单个字符
    - `**`: 目录多跳通配符

### Change log

见 [CHANGE_LOG.md](./CHANGE_LOG.md)
