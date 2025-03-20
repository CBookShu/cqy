# cqy.h

一个 P2P 的节点分布式小工具，代码由 C++ 协程组织。

## 示例

具体示例，请参见 `ping` 和 `pong`。

## 配置

配置文件请参见 `build/config1.json` 和 `build/config2.json`。

## 结构

- 单个节点即一个 `cqy::cqy_app`。
- `cqy::cqy_app` 下可以自定义多个 `cqy_ctx_t`。

## 功能

- `cqy_ctx_t` 之间可以通过异步消息进行通信，也可以通过 RPC 调用。
- 即使是跨 `cqy::cqy_app` 的调用，使用起来与调用本地函数一样。
- `cqy_ctx_t` 注册的 RPC 函数和 `on_msg` 都是无竞争的。

**注意**：协程函数一旦 `await` 让出当前操作，下一个消息或 RPC 可能会在 `await` 结束之前调用。

## TODO
- add test
- add script
- add custom msg pack
- support node config reload
- support route name to node
