# ISSUE-022: Automated AI Settings Test Reached Real Provider Endpoint

> 任务关联：T034-B2.1（Recovery Incident）。状态：FIXED IN WORKING TREE /
> NOT YET COMMITTED / FINAL FULL REGRESSION PENDING。Human 最终验收与 commit
> 前不得标记 CLOSED。

## Observed

T034-B2.1 恢复期间（HTTP 注入 seam 修复前），`AiSettingsController` 的
Test Connection 自动化测试（SET-11/13/14/15/16/17）发起了**真实公网请求**：

- 目标 endpoint：`https://api.deepseek.com/chat/completions`（真实
  api.deepseek.com，非 localhost fake）
- 凭据：`FAKE_MODBUSLENS_SECRET_B2_DO_NOT_LEAK`（**fake** 测试哨兵值，非
  Human 真实 key）
- Provider 响应：**HTTP 401 authentication failure**（DeepSeek 返回
  authentication_error）

实证：测试进程 QWARN 输出 8 处
`qt.network.http2: stream 1 finished with error: "Host requires authentication"`
（`AiSettingsTest::set11/13/14/15/16/17` 各一次，记录于当时测试运行输出
`dbg_all2.txt`）。FakeHttpClient 的 `callCount` 保持 0——请求绕过了注入
fake，直达生产 transport。

## Expected

所有自动化 AI 测试必须离线/确定性运行，**不得**触达生产 provider endpoint
（Human §20/§21：fake/injected/localhost transport；SET 测试必须断言
transport ownership）。

## Impact

- **Security boundary violation: YES** —— 自动化测试在无意间向真实
  DeepSeek 生产 endpoint 发送了出站流量（自动化测试脱离离线/确定性边界）
- 计划外公网流量：每个受影响 slot 一次真实 HTTPS POST（共 6 次）
- Known credential/data exposure：**NONE OBSERVED**
- Human credential exposure：**NONE**（测试仅使用显式 fake credential
  `FAKE_MODBUSLENS_SECRET_B2_DO_NOT_LEAK`；Human 真实 key 未被测试进程
  加载——测试 init 对全部凭据类 env 执行了 qunsetenv）
- Raw secret logging：**NONE OBSERVED**（fake 值本身可入档；无
  Authorization 头/原始响应记录）
- Business-state mutation：**NONE OBSERVED**（Test Connection 按 §14 契约
  不触 Candidate/Diagnosis/Agent/Manual QA/DeviceProfile/Manual cache）
- Actual observed impact：unintended public-network request using fake
  credential，HTTP 401 authentication response
- 不夸大：无证据支持 credential leak / data breach 定性

## Root Cause

`AiSettingsController` 的 Test Connection 路径**初始缺少确定性 HTTP 注入
seam**：控制器构造函数只创建了生产 `QtChatCompletionsHttpClient`（指向
profile 固定官方 endpoint），无 `IChatCompletionsHttpClient&` 注入构造重载。
测试声明的 `FakeHttpClient` 因无人消费而闲置；生产 transport 按契约向
profile endpoint 发起了真实请求。

## Security Assessment

- TEST ISOLATION / SECURITY NEAR MISS（非生产事故——发生在未提交的开发期
  测试代码中，未发生生产部署）
- 泄露面分析：fake credential 是唯一外发凭据；无 Human key 参与；无日志
  记录该请求的 header/body
- 若测试使用真实 key 将构成实际泄露——该形态被本轮修复阻断（注入 seam +
  env 清理使测试进程**结构上无法**携带/选择 Human 凭据）

## Fix

1. `AiSettingsController` 增加 TEST-ONLY 注入构造
   `AiSettingsController(IChatCompletionsHttpClient &http, QObject*)`：
   测试注入捕获型 fake client，生产路径不变（默认构造仍持有真实 client）
2. 测试全部改用注入构造；FakeHttpClient 在本地捕获全部请求
3. 测试 init 对凭据类 env（MODELSCOPE_API_KEY / DEEPSEEK_API_KEY /
   MODBUSLENS_AI_*）执行 qunsetenv——测试进程结构上无 Human 凭据
4. 验证测试 fn01/fn02 双向证明 forensic gate 行为

## Verification

- 注入修复后：ai_settings **23/23 PASS**（含 SET-11..17 全部改走 fake
  transport：bearer=fake-deepseek-key、endpoint 断言、synthetic probe 断言）
- 修复后测试运行输出中 `qt.network.http2` 出现次数 = **0**（修复前 = 8）
- 相关 candidate targets 回归全 PASS（19/25/22/20/14/36/43/15）

## Regression Protection

- SET 测试永久断言 transport ownership（fn01/fn02 + ds_j 系列）
- 新增治理规则（T034 task §Preventive Actions P4/P5）：自动化 AI 测试不得
  具备触达公网 provider endpoint 的能力；AI 测试必须 unset/隔离全部凭据 env
- 注入 seam 为结构性防线：测试进程无法选择生产 transport

## 教训

测试隔离缺陷的暴露依赖"真实 provider 拒绝了 fake 凭据"这一外部巧合——若
provider 静默接受，近失将不可见。隔离必须是**结构性的**（测试无法构造真实
网络路径），而非依赖外部失败来暴露。
