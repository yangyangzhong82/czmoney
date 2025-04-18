# czmoney 脚本 API 文档

本插件通过 LeviLamina 的 RemoteCall 功能导出了一系列 API 函数，供脚本插件（如 LegacyScriptEngine 的 JavaScript 插件）调用。

## 导入 API

在你的脚本插件中，你可以使用 `ll.imports` 函数来导入 czmoney 提供的 API。所有函数都导出在 `czmoney` 命名空间下。

```javascript
// 导入单个函数
const getPlayerBalance = ll.imports("czmoney", "getPlayerBalance");
const addPlayerBalance = ll.imports("czmoney", "addPlayerBalance");
// ... 导入其他需要的函数

// 也可以一次性导入所有函数到一个对象中（如果脚本引擎支持或自行封装）
const czmoneyAPI = {
    getPlayerBalance: ll.imports("czmoney", "getPlayerBalance"),
    getPlayerBalanceOrInit: ll.imports("czmoney", "getPlayerBalanceOrInit"),
    setPlayerBalance: ll.imports("czmoney", "setPlayerBalance"),
    addPlayerBalance: ll.imports("czmoney", "addPlayerBalance"),
    subtractPlayerBalance: ll.imports("czmoney", "subtractPlayerBalance"),
    hasAccount: ll.imports("czmoney", "hasAccount"),
    formatBalance: ll.imports("czmoney", "formatBalance"),
    parseBalance: ll.imports("czmoney", "parseBalance"),
    transferBalance: ll.imports("czmoney", "transferBalance")
};
```

## API 函数列表

**重要提示:**
*   API 函数内部使用 `int64_t` (64位整数) 来存储金额，单位是 **分** (即实际金额乘以 100)，以避免浮点数精度问题。
*   当 API 函数接受 `Number` 类型的金额参数时，通常指的是 **元** (例如 123.45)。插件内部会自动将其转换为分进行处理。
*   当 API 函数返回 `Number` 类型的余额时，通常是 **分**。你需要将其除以 100 来得到以元为单位的实际金额。

以下是所有可用的 API 函数及其用法：

---

### `getPlayerBalance(uuid, currencyType)`

获取玩家指定货币类型的余额。

*   **参数:**
    *   `uuid` (String): 玩家的 UUID。
    *   `currencyType` (String): 货币类型 (例如 "money", "points")。
*   **返回值:** (Number) 玩家的余额 (整数，**分**)。如果账户不存在或查询失败，返回 0。

**示例:**
```javascript
const playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
const balanceInCents = czmoneyAPI.getPlayerBalance(playerUuid, "money");
if (balanceInCents !== 0 || czmoneyAPI.hasAccount(playerUuid, "money")) { // 需要额外判断账户是否存在来区分0余额和无账户
    logger.info(`玩家 ${playerUuid} 的余额为: ${balanceInCents / 100} 元`); // 除以 100 得到元
} else {
    logger.info(`玩家 ${playerUuid} 的 money 账户不存在。`);
}
```

---

### `getPlayerBalanceOrInit(uuid, currencyType)`

获取玩家指定货币类型的余额，如果账户不存在则根据配置初始化。

*   **参数:**
    *   `uuid` (String): 玩家的 UUID。
    *   `currencyType` (String): 货币类型。
*   **返回值:** (Number) 玩家的余额 (整数，**分**)。

**示例:**
```javascript
const playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
const balanceInCents = czmoneyAPI.getPlayerBalanceOrInit(playerUuid, "money");
logger.info(`玩家 ${playerUuid} 的余额 (或初始余额) 为: ${balanceInCents / 100} 元`);
```

---

### `setPlayerBalance(uuid, currencyType, amount, [reason1], [reason2], [reason3])`

设置玩家指定货币类型的余额。如果账户不存在，将自动创建。

*   **参数:**
    *   `uuid` (String): 玩家的 UUID。
    *   `currencyType` (String): 货币类型。
    *   `amount` (Number): 要设置的余额 (**元**，例如 123.45)。
    *   `reason1` (String, 可选): 操作理由 1。
    *   `reason2` (String, 可选): 操作理由 2。
    *   `reason3` (String, 可选): 操作理由 3。
*   **返回值:** (Boolean) 操作是否成功 (例如金额无效、低于最低余额等会导致失败)。

**示例:**
```javascript
const playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
const success = czmoneyAPI.setPlayerBalance(playerUuid, "money", 500.75, "后台操作", "管理员设置");
if (success) {
    logger.info(`成功设置玩家 ${playerUuid} 的余额。`);
} else {
    logger.error(`设置玩家 ${playerUuid} 余额失败。`);
}
```

---

### `addPlayerBalance(uuid, currencyType, amountToAdd, [reason1], [reason2], [reason3])`

增加玩家指定货币类型的余额。如果账户不存在，将自动创建并初始化。

*   **参数:**
    *   `uuid` (String): 玩家的 UUID。
    *   `currencyType` (String): 货币类型。
    *   `amountToAdd` (Number): 要增加的金额 (**元**，必须为正数，例如 10.50)。
    *   `reason1` (String, 可选): 操作理由 1。
    *   `reason2` (String, 可选): 操作理由 2。
    *   `reason3` (String, 可选): 操作理由 3。
*   **返回值:** (Boolean) 操作是否成功。

**示例:**
```javascript
const playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
const success = czmoneyAPI.addPlayerBalance(playerUuid, "points", 25.0, "任务奖励");
if (success) {
    logger.info(`成功为玩家 ${playerUuid} 增加积分。`);
} else {
    logger.error(`为玩家 ${playerUuid} 增加积分失败。`);
}
```

---

### `subtractPlayerBalance(uuid, currencyType, amountToSubtract, [reason1], [reason2], [reason3])`

减少玩家指定货币类型的余额。如果账户不存在或余额不足，操作将失败。

*   **参数:**
    *   `uuid` (String): 玩家的 UUID。
    *   `currencyType` (String): 货币类型。
    *   `amountToSubtract` (Number): 要减少的金额 (**元**，必须为正数，例如 5.25)。
    *   `reason1` (String, 可选): 操作理由 1。
    *   `reason2` (String, 可选): 操作理由 2。
    *   `reason3` (String, 可选): 操作理由 3。
*   **返回值:** (Boolean) 操作是否成功。

**示例:**
```javascript
const playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
const success = czmoneyAPI.subtractPlayerBalance(playerUuid, "money", 15.50, "商店购买", "商品 A");
if (success) {
    logger.info(`成功扣除玩家 ${playerUuid} 的余额。`);
} else {
    logger.error(`扣除玩家 ${playerUuid} 余额失败 (可能余额不足)。`);
}
```

---

### `hasAccount(uuid, currencyType)`

检查玩家账户是否存在。

*   **参数:**
    *   `uuid` (String): 玩家的 UUID。
    *   `currencyType` (String): 货币类型。
*   **返回值:** (Boolean) 账户是否存在。

**示例:**
```javascript
const playerUuid = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
if (czmoneyAPI.hasAccount(playerUuid, "money")) {
    logger.info(`玩家 ${playerUuid} 存在 money 账户。`);
} else {
    logger.info(`玩家 ${playerUuid} 不存在 money 账户。`);
}
```

---

### `formatBalance(amount)`

将整数余额 (**分**) 格式化为带两位小数的字符串 (**元**)。

*   **参数:**
    *   `amount` (Number): 整数余额 (**分**)。
*   **返回值:** (String) 格式化后的金额字符串 (例如 "123.45")。

**示例:**
```javascript
const balanceInCents = 12345;
const formattedString = czmoneyAPI.formatBalance(balanceInCents); // "123.45"
logger.info(`格式化后的金额: ${formattedString}`);

const balanceFromAPI = czmoneyAPI.getPlayerBalance("some_uuid", "money");
logger.info(`格式化后的余额: ${czmoneyAPI.formatBalance(balanceFromAPI)}`);
```

---

### `parseBalance(formattedAmount)`

将带小数的字符串金额 (**元**) 解析为整数余额 (**分**)。

*   **参数:**
    *   `formattedAmount` (String): 格式化的金额字符串 (**元**，例如 "123.45", "0.5", "100")。
*   **返回值:** (Number) 整数余额 (**分**)。如果格式无效，返回 0。

**示例:**
```javascript
const amountString = "50.25";
const balanceInCents = czmoneyAPI.parseBalance(amountString); // 5025
// 注意：需要谨慎处理解析结果为 0 的情况，因为它可能是有效输入 "0" 或无效输入
if (balanceInCents !== 0 || ["0", "0.0", "0.00"].includes(amountString)) {
   logger.info(`解析后的整数金额 (分): ${balanceInCents}`);
} else {
   logger.error(`无效的金额格式: ${amountString}`);
}

const invalidString = "abc";
const invalidCents = czmoneyAPI.parseBalance(invalidString); // 0
logger.info(`解析无效字符串后的金额 (分): ${invalidCents}`);
```

---

### `transferBalance(senderUuid, receiverUuid, currencyType, amountToTransfer, [reason1], [reason2], [reason3])`

从一个玩家向另一个玩家转账。会处理配置中的转账税。

*   **参数:**
    *   `senderUuid` (String): 转出方玩家 UUID。
    *   `receiverUuid` (String): 接收方玩家 UUID。
    *   `currencyType` (String): 货币类型。
    *   `amountToTransfer` (Number): 要转账的金额 (**元**，必须为正数，例如 10.50)。这是发送方扣除的金额，接收方收到的金额会减去税费。
    *   `reason1` (String, 可选): 操作理由 1 (默认为 "Transfer")。
    *   `reason2` (String, 可选): 操作理由 2 (例如 发起者名称)。
    *   `reason3` (String, 可选): 操作理由 3 (例如 接收者名称)。
*   **返回值:** (Boolean) 操作是否成功 (例如发送方余额不足、配置不允许转账等会导致失败)。

**示例:**
```javascript
const sender = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
const receiver = "yyyyyyyy-yyyy-yyyy-yyyy-yyyyyyyyyyyy";
const success = czmoneyAPI.transferBalance(sender, receiver, "money", 100.0, "玩家转账", "玩家A", "玩家B");
if (success) {
    logger.info(`成功从 ${sender} 转账给 ${receiver}。`);
} else {
    logger.error(`转账失败 (可能发送方余额不足或配置不允许转账)。`);
}
