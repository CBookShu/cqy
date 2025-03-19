cqy.h
一个p2p 的node 节点分布式小工具，代码由C++协程组织。


具体示例，看ping和pong
配置看build/config1.json 和 build/config2.json

单个node 节点即一个cqy::cqy_app
cqy::cqy_app 下面可以自定义多个 cqy_ctx_t

cqy_ctx_t 之间可以通过异步的消息，也可以通过rpc来调用。
cqy_ctx_t 之间哪怕是跨 cqy::cqy_app，调用起来，跟调用本地是一样的。
cqy_ctx_t 注册的rpc 函数，还有on_msg 都是无竞争的。【注意】协程函数一旦await
  让出当前的操作，下一个msg或者rpc就有可能会在 await 结束之前调用。
