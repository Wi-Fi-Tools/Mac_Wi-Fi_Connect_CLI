# MacOS Wi-Fi CLI工具

## 背景

通过CLI工具的实现，掌握MacOS中使用api对MacOS的Wi-Fi连接进行管理

## 实现逻辑

核心逻辑功能集的实现基于**networksetup(macOS 系统自带的命令行工具)**，通过execvp调用该工具，并将输出重定向，以便程序调用.

## 项目结构

```txt
├── build.sh                        // 编译脚本
├── CMakeLists.txt
├── docs
├── include                         // 头文件
│   └── wifi_direct
│       ├── command_handler.h
│       ├── network_info.h
│       └── wifi_manager.h
├── python                          // python原始实现
│   └── wifi_direct.py
├── readme.md               
├── scripts                         // 调用swift脚本扫描Wi-Fi
│   └── scan_wifi.swift
└── src                             // c++源码
    ├── command_handler.cpp
    ├── main.cpp
    └── wifi_manager.cpp
```

## Get start

- 编译产物
  ```shell
  bash build.sh
  ```

- 运行
  ```shell
  ./build/wifi_direct   # 会输出help
  ```