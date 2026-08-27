
## 本轮新确认：🔴 Bug B —— 析构函数错误的 epoll 计数 → 服务器崩溃（纯状态机+记账问题）

**根因**：`connection::~connection()`（`types.hh`）对每个仍然有效的 fd 无条件执行 `G::ep.dec()`，假设"有效 ⇒ 已注册进 epoll"。但**这个假设对 dacceptor 不成立**：

| 状态 | dacceptor | 是否在 epoll | 
|---|---|---|
| idle + PASV 后 | valid | **否**（只创建，未 add）|
| before_transfer（LIST 后）| valid | 是（`prepare_data_transfer` add）|

于是在 `idle` 态、刚发完 `PASV` 时删除连接（`QUIT` 或直接断连 EOF），析构会为**从未注册**的 acceptor 多减一次 `n`，使 `epoll::wait()` 以 `events(n)==空/0` 调用 `epoll_wait` → EINVAL → 未捕获异常 `terminate`。

**实测**（ASan 版 + **用户的生产二进制**均复现，生产版 core dumped）：
```
PASV: 227 ...
QUIT: 221 Bye.
（下一个连接）ConnectionRefused  ← 服务器已死
log: terminate ... wait: Invalid argument
```
触发只需两行命令：`PASV` → `QUIT`；或者 PASV 后**直接断开控制连接**（连 QUIT 都不用）。**一个普通登录用户即可让整个 FTP 服务崩溃。**

**对照已验证安全**（证明 bug 只针对"未注册 fd"）：EOF 发生在 `before_transfer`（acceptor 已注册）时删除正确、服务器存活；REIN 会先经 `abort_transfer_preparation` 关闭 acceptor（再删除时 `valid()==false`），同样安全。

**修复方向**：析构不能光看 `valid()`，要按状态判断每个 fd 是否注册过（或统一在删除路径先走 `abort_transfer_preparation`/`complete_data_transfer`）。

---

## 上一轮两个崩溃 bug 的根因归类（状态机视角）

- **🔴 Bug A（worker 传输 + QUIT/REIN → UAF）**：`complete_data_transfer` 在 `ready_to_close` 时 `delete &c`，之后 `on_worker_event` 仍访问 `c.ef`、`G::ep.dec()`。此外该路径删除后**没把 `&c` 加入 `skips`**，同批次残留事件也可能再命中已释放连接。生产版同样崩溃（计数错乱 → `epoll_wait` EINVAL）。
- **🔴 Bug C（`respond()` 超长回复栈溢出）**：`snprintf` 返回值被直接累加 `off`，截断后 `off` 仍按全长走，`buf[off++]` 越界。（上一轮已确认）

---

## 其他状态/事件循环观察（次要，但值得记下）

1. **ABOR 在 worker 传输中只设 flag、无及时响应**：worker 阻塞在 blocking send 上，客户端要等数据连接关闭/排空才收到 451。若客户端既不读也不关数据连接，worker 线程与连接**永久泄漏**（控制连接 EOF 走 `wf=destroy` 时同样被卡住）。
2. **`on_data_message` 不处理 `EPOLLERR/EPOLLHUP`**：evloop 传输中对端异常关闭时没有显式错误分支（实测小列表因数据量 < socket 缓冲基本瞬时完成，实际较难触发，属潜在隐患）。
3. **evloop 路径 ABOR 泄漏堆上的 `c.handler`**：`do_ABOR` 直接调 `complete_data_transfer`，不经 `invoke_data_handler`，`delete hp` 不会执行。
4. **绝对路径/`..` 可逃逸 home**：`c.wd / 绝对路径` 会整体替换（C++ filesystem 语义），配合 `ensure_permission` 尚为恒 `true` 的 stub，登录用户可 LIST 任意目录——安全边界待权限实现落地。
5. **PORT 连接器只监听 `EPOLLOUT`**：若 connect 只报 `EPOLLERR/HUP`（不含 OUT），连接会永久卡在 `before_transfer`（实测 Linux 拒绝连接会带 OUT，425 正常，属防御性缺口）。
