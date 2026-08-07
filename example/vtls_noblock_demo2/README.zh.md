# vtls_noblock_demo2

中文 | [English](README.md)

本仓库推荐通过 unirtos-cli 的 demo 工作流使用，以保证创建、环境拉取和编译流程一致。

## 功能描述

本 Demo 采用非阻塞 socket + 非阻塞 SSL 接口（`qcm_ssl_connect_nonblocking`），验证完整 VTLS 非阻塞握手与数据收发流程。

- 演示在指定 CID 上完成 PDP 激活与 DNS 解析
- 演示非阻塞 socket 初始化及 VTLS 回调式 IO 适配
- 演示通过 `qcm_ssl_connect_nonblocking` + `done` 状态轮询推进握手
- 演示通过 `ssl_ctx->state` 判断握手阶段并控制后续逻辑
- 演示握手完成后发送 HTTP 请求并读取 TLS 响应数据

## 快速上手

### 1. 安装 UniRTOS 工具链

- [开发准备](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/开发准备/开发准备.html)
- [安装交叉编译工具链](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/环境搭建/环境搭建.html)
- [安装 Python3](https://www.python.org/downloads/)
- [安装 git](https://git-scm.com)
- 安装 unirtos-cli：`pip install unirtos-cli`

以上工具安装完成后，确认以下命令可用：

```bash
python --version # Python3
git --version
unirtos --version # 1.0.5 及以上版本
unirtos-cli version # 1.0.11 及以上版本
```

### 2. 使用 unirtos-cli 拉取 demo

先查看可用 demo 与版本：

```bash
unirtos-cli ls-demos
```

创建本 demo 工程：

```bash
unirtos-cli new -r unirtos-vtls-demos
```

如需指定版本：

```bash
unirtos-cli new -r unirtos-vtls-demos -v 1.0.0
```

### 3. 进入工程并编译

```bash
cd unirtos-vtls-demos-1.0.0/example/vtls_noblock_demo2
unirtos-cli env-setup
unirtos-cli build
```

## 常用命令

```bash
# 打开 SDK 菜单配置
unirtos-cli menuconfig

# 清理构建产物
unirtos-cli clean
```

## 技术社区

技术社区：https://forumschinese.quectel.com/c/66-category/66

## 贡献指南

欢迎参与共建，建议按以下方式提交：
- 提交前先执行一次基础验证：env-setup、build、clean。
- 使用清晰的提交说明，描述改动目的、影响范围和验证结果。
- 新增功能或行为变化时，同步更新 README 与相关文档。
- 通过 Issue 或 Pull Request 提交问题修复与功能改进。
