# chaoxi

`chaoxi` 是一个基于 C++ 的网络库练习项目，整体结构参考了 muduo 的常见组织方式。

## 目录结构

- `base/`：基础工具和通用组件
- `net/`：网络相关核心代码
- `examples/`：示例程序
- `tests/`：测试代码

## 构建

项目使用 CMake 构建，建议在 `build/` 目录下进行编译。

### 依赖

- 核心库仅需支持 C++23 的编译器
- `benchmark` 依赖 `google-benchmark`
- 单元测试依赖 `gtest`
- 以上依赖均通过 CMake 的 `FetchContent` 自动拉取，无需手动额外配置

### 构建步骤

```bash
mkdir build
cd build

# 配置完整项目：库、examples、tests、benchmarks
cmake -S ..

# 如果只想构建 chaoxi 库，可以关闭其他选项
# cmake -S .. -DCHAOXI_BUILD_EXAMPLES=OFF -DCHAOXI_BUILD_TESTS=OFF -DCHAOXI_BUILD_BENCHMARKS=OFF

cmake --build .
```

## 作为第三方库使用

如果你在一个新项目里使用它，推荐把这个仓库作为子目录接入，例如放在 `third_party/chaoxi/`，然后在你自己的 `CMakeLists.txt` 里这样写：

```cmake
set(CHAOXI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CHAOXI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CHAOXI_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/chaoxi)
target_link_libraries(your_target PUBLIC chaoxi::chaoxi)
```

如果你是通过 `git clone` 方式引入，也可以先把仓库放到项目目录下，再用同样的方式通过 `add_subdirectory()` 接入。这样只会编译库本身，`examples/`、`tests/` 和 `benchmark/` 都会保持关闭。

```cmake
add_subdirectory(chaoxi)
target_link_libraries(your_target PUBLIC chaoxi::chaoxi)
```

## 运行

编译完成后，可根据 `examples/` 或 `tests/` 中对应的目标程序运行。

## 文档生成

本仓库使用 Doxygen 注释风格。若系统已安装 `doxygen` 和 `graphviz`，可以执行：

```bash
doxygen Doxyfile
```

生成后的 HTML 文档位于 `docs/` 目录下。

## 说明

该仓库主要用于学习和实验网络编程，具体实现细节以各个模块为准。

## 参考

- https://github.com/chenshuo/muduo
