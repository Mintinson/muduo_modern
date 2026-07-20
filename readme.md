# chaoxi

`chaoxi`(抄袭) 这是一个基于 C++ 的网络库练习项目，结构参考了 muduo 的常见组织方式。

## 主要内容

- `base/`：基础工具和通用组件
- `net/`：网络相关核心代码
- `examples/`：示例程序
- `tests/`：测试代码

## 构建

项目使用 CMake 构建，建议在 `build/` 目录下编译。

## 运行

先完成编译，再根据 `examples/` 或 `tests/` 中的目标程序运行。

## 说明

这个仓库主要用于学习和实验网络编程，代码细节以各个模块为准。

## 生成文档

该仓库使用 doxygen 注释风格。如果想要网页版本文档，可以在系统安装了 `doxygen` 和 `graphviz` 命令后，运行：

```bash
doxygen Doxyfile
```

并在生成的 `docs` 目录下查看生成的 HTML 网页文档。