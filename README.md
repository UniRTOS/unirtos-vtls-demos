# unirtos-vtls-demos

[中文](README.zh.md) | English

This repository is recommended to be used via the unirtos-cli demo workflow to ensure consistent project creation, environment setup, and build processes.

## Overview

`unirtos-vtls-demos` provides a comprehensive demonstration of VTLS (secure TLS communication) capabilities on UniRTOS.  
This directory focuses on TLS secure communication examples, covering different socket/handshake combinations from blocking to non-blocking flows, so you can quickly understand VTLS integration patterns and choose the right implementation route for your product.

VTLS is a core mechanism for secure data transmission in embedded devices. UniRTOS provides a complete VTLS support framework that enables PDP activation, DNS resolution, and TLS handshake/data transfer over both blocking and non-blocking socket models. This directory provides multiple typical scenario examples for developers to reference and quickly integrate into their own product communication workflows.

## Feature Description

- Supports a full **blocking** VTLS connection flow, using blocking socket IO and the blocking SSL connect interface (see [vtls_block_demo](./vtls_block_demo/))
- Supports VTLS data transmission with a **non-blocking socket** while still using the blocking SSL connect interface (see [vtls_noblock_demo](./vtls_noblock_demo/))
- Supports a **fully non-blocking** VTLS handshake/data flow, combining non-blocking socket IO with `qcm_ssl_connect_nonblocking` (see [vtls_noblock_demo2](./vtls_noblock_demo2/))
- Covers the complete VTLS workflow: PDP activation → DNS resolution → TLS handshake → request send/response receive
- Each sub-demo can be compiled and run independently, with full error handling and log output
- Easily extensible to certificate-based mutual authentication, multi-connection management, and production-grade secure communication orchestration

## Sub-Demo Overview

| Sub-Demo | Socket / Handshake Mode | Description |
|---|---|---|
| [vtls_block_demo](./vtls_block_demo/) | Blocking socket + blocking handshake | Full blocking VTLS flow using `qcm_ssl_connect`; simplest to integrate |
| [vtls_noblock_demo](./vtls_noblock_demo/) | Non-blocking socket + blocking handshake | Non-blocking socket IO combined with blocking `qcm_ssl_connect`, using `select` for the receive loop |
| [vtls_noblock_demo2](./vtls_noblock_demo2/) | Non-blocking socket + non-blocking handshake | Fully non-blocking handshake via `qcm_ssl_connect_nonblocking` with state polling |

## Mode Selection Guide

| Scenario | Recommended Solution |
|---|---|
| Simple integration, blocking on TLS operations is acceptable | vtls_block_demo |
| Need non-blocking socket IO but simple handshake handling is preferred | vtls_noblock_demo |
| Need fully asynchronous, non-blocking TLS handshake and IO for high-concurrency scenarios | vtls_noblock_demo2 |

## Technical Community

Forum: https://forumschinese.quectel.com/c/66-category/66

## Contributing

Contributions are welcome. Please follow these guidelines:
- Run a basic validation before submitting: env-setup, build, clean.
- Use clear commit messages describing the purpose, scope of changes, and validation results.
- Update README and related documentation when adding features or changing behavior.
- Submit bug fixes and feature improvements via Issues or Pull Requests.
