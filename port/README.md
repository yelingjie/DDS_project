# pubsub 移植接口

应用只包含 `include/pubsub.h`；`port/pubsub_port.h` 是移植实现者使用的内部接口。

```text
业务任务 ── pubsub.h ── src/pubsub.c ── pubsub_port.h ── pubsub_port.c ── 操作系统
```

默认 `pubsub_port.c` 实现普通 FreeRTOS / ESP-IDF 适配。移植到 RT-Thread、Zephyr、ThreadX 等系统时，在构建中替换此文件，实现相同函数；核心代码和业务调用接口不需要修改。构建中只链接一个 port 实现。

## 需要实现的组件

| 移植接口 | 必须满足的语义 | 默认 FreeRTOS 实现 |
| --- | --- | --- |
| `ps_port_mutex_create/lock/unlock` | 跨任务、跨核互斥；创建失败返回 NULL | Mutex semaphore |
| `ps_port_queue_create/destroy` | 定长消息队列；失败返回 NULL；destroy 回收失败初始化中的队列 | `xQueueCreate` / `vQueueDelete` |
| `ps_port_queue_try_send/try_receive` | 非阻塞 FIFO，复制整个消息；失败不改变队列 | `xQueueSend` / `xQueueReceive`，等待参数 0 |
| `ps_port_queue_count` | 当前排队消息数；调用时核心持有总线锁 | `uxQueueMessagesWaiting` |
| `ps_port_event_create/set/clear/wait` | 初始清除、持续置位的通知标志，等待成功不清位 | Event group 的一个低位 |
| `ps_port_now_ms` | 线程安全、单调递增的 64 位毫秒时钟 | tick + 内核溢出计数快照 |

三个句柄都是不透明指针，不公开原生 OS 类型。适配器可直接转换原生句柄，也可以指向自己分配的包装对象。当前生命周期只需要 queue destroy：当队列创建成功、事件创建失败时，核心调用它回收队列；已经成功注册的客户端和总线锁具有应用生命周期。

## 超时约定

所有端口超时参数均为 `uint32_t` 毫秒：

- `0`：立即尝试一次。
- `PS_PORT_WAIT_FOREVER`：无限等待，对应公开 API 的 `PS_WAIT_FOREVER`。
- 其他数值：有限等待。原生 tick 转换应向上取整，避免正数毫秒被转换成 0 tick。

互斥锁有限等待应涵盖整个指定等待时间；如果原生系统的最大阻塞长度较小，由适配器拆分等待。事件有限等待允许提前返回 false，核心会检查同一个总超时预算后重试；事件无限等待必须真正等待到事件置位。

不要把过大的有限等待直接截断/转换成原生“无限等待”常量。默认适配器将有限等待限制为 `portMAX_DELAY - 1` tick，超出原生范围时拆分或由核心重试。它支持 16/32 位 tick，要求 `INCLUDE_vTaskSuspend=1` 以保证无限等待。

单调时钟必须处理原生计数器回绕，不可直接返回 `tick * 毫秒系数` 后在 tick 回绕时归零。默认实现通过 `vTaskSetTimeOutState()` 获取 tick 与溢出计数的同步快照，并组合成 64 位时间。对应字段定义参考 [FreeRTOS task.h](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/include/task.h)。若目标系统提供可靠的 64 位 uptime 函数，可以直接使用。

## 事件约定

事件是电平标志，不是计数信号量，也不是一次性脉冲：

1. `set` 将标志保持置位，并唤醒等待者；连续 set 可合并。
2. `wait` 发现已置位时立即成功，**不能自动清位**。
3. 只有 `clear` 可以清除标志。
4. 核心在同一把总线锁内完成“入队 + set”和“空队列检查 + clear”。

使用自动复位事件或计数信号量直接替代，会改变通知语义，可能造成队列仍有消息时通知被消费。移植实现必须保持以上约定。

## 构建边界

核心只需要两个 include 路径：`include/` 与 `port/`，编译 `src/pubsub.c` 时不需要任何 OS 头文件路径。默认适配器编译时才需要 FreeRTOS 头文件，`ESP_PLATFORM` 分支使用 `freertos/...` 路径。

对外 API 有意改为系统无关形式：

```c
#include "pubsub.h"

/* depth 是 uint32_t，timeout 是毫秒，无须 pdMS_TO_TICKS。 */
ps_client_create(8, &client);
ps_receive(client, &message, 100);
ps_receive(client, &message, PS_WAIT_FOREVER);
/* message.timestamp_ms：发布时的 64 位单调毫秒时间。 */
```

`examples/pubsub_demo.c` 属于 FreeRTOS 应用示例，任务创建和启动屏障仍由示例管理。核心框架不创建线程，因此 port 不要求提供线程创建接口。换系统后保留 `pubsub.h` 调用，根据目标系统修改示例中的任务创建方式即可。

## 验证移植

当前主机测试覆盖公开头文件独立编译、核心无 OS 头文件编译、订阅路由和并发收发，并验证以下原生配置：32 位 tick / 1000Hz、32 位 tick / 100Hz、16 位 tick / 1000Hz。检查了正数毫秒向上取整、有限/无限等待分离、原生 tick 回绕后的单调时钟。

新 port 实现至少应保持事件电平语义、FIFO 独立副本、超时总预算和跨核互斥。主机模拟测试不替代目标系统或 ESP32 板上验证。
