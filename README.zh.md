# unirtos-vtls-demos

中文 | [English](README.md)

本仓库推荐通过 unirtos-cli 的 demo 工作流使用，以保证创建、环境拉取和编译流程一致。

## 概述

`unirtos-vtls-demos` 用于展示 UniRTOS 下 VTLS（安全 TLS 通信）能力的整体方案。  
该目录聚焦 TLS 通信示例，覆盖从阻塞到非阻塞的不同 socket 与握手组合流程，便于快速理解 VTLS 集成模式并选择合适实现路径。

VTLS 是嵌入式设备实现安全数据传输的核心机制。UniRTOS 提供了完整的 VTLS 支持框架，支持在阻塞与非阻塞 socket 模型下完成 PDP 激活、DNS 解析及 TLS 握手/数据收发。本目录提供多个典型场景示例，供开发者参考选型并快速集成到自己的产品通信流程中。

## 功能描述

- 支持完整的**阻塞式** VTLS 连接流程，使用阻塞 socket IO 与阻塞 SSL connect 接口（见 [vtls_block_demo](./vtls_block_demo/)）
- 支持使用**非阻塞 socket** 进行 VTLS 数据传输，同时仍使用阻塞 SSL connect 接口（见 [vtls_noblock_demo](./vtls_noblock_demo/)）
- 支持**完全非阻塞**的 VTLS 握手/数据流程，结合非阻塞 socket IO 与 `qcm_ssl_connect_nonblocking`（见 [vtls_noblock_demo2](./vtls_noblock_demo2/)）
- 统一覆盖 VTLS 全流程：PDP 激活 → DNS 解析 → TLS 握手 → 请求发送/响应接收
- 各子 Demo 均可独立编译运行，并包含完整的错误处理与日志输出
- 便于扩展为证书双向认证、多连接管理及量产级安全通信编排等高级场景

## 子 Demo 说明

| 子 Demo | Socket / 握手模式 | 说明 |
|---|---|---|
| [vtls_block_demo](./vtls_block_demo/) | 阻塞 socket + 阻塞握手 | 使用 `qcm_ssl_connect` 实现完整阻塞式 VTLS 流程，集成最简单 |
| [vtls_noblock_demo](./vtls_noblock_demo/) | 非阻塞 socket + 阻塞握手 | 非阻塞 socket IO 结合阻塞式 `qcm_ssl_connect`，接收循环使用 `select` |
| [vtls_noblock_demo2](./vtls_noblock_demo2/) | 非阻塞 socket + 非阻塞握手 | 通过 `qcm_ssl_connect_nonblocking` 实现完全非阻塞握手，并进行状态轮询 |

## 选型参考

| 场景 | 推荐方案 |
|---|---|
| 集成简单，可接受 TLS 操作阻塞 | vtls_block_demo |
| 需要非阻塞 socket IO，但希望握手处理保持简单 | vtls_noblock_demo |
| 高并发场景，需要完全异步、非阻塞的 TLS 握手与数据收发 | vtls_noblock_demo2 |

## 技术社区

技术社区：https://forumschinese.quectel.com/c/66-category/66

## 贡献指南

欢迎参与共建，建议按以下方式提交：
- 提交前先执行一次基础验证：env-setup、build、clean。
- 使用清晰的提交说明，描述改动目的、影响范围和验证结果。
- 新增功能或行为变化时，同步更新 README 与相关文档。
- 通过 Issue 或 Pull Request 提交问题修复与功能改进。
