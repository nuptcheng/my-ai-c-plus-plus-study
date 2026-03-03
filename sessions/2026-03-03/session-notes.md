# Session记录 - 2026-03-03
[返回 README](../../README.md)

## Session概述
- **日期**: 2026-03-03
- **时长**: 约60分钟
- **主要主题**: 实时库索引选型（Hash vs B+树）、设备筛选与排序、按值范围查询

---

## 学生提出的问题

### 问题1: 实时库索引选型（共享内存 + 哈希 vs B+树）
<a id="问题1-实时库索引选型"></a>

**原始问题**:
- “我有1000W点实时库，批量更新/批量点查都是几十W级别，我朋友用了B+树，我没看懂使用场景，请帮我分析下，有没有必要使用，选什么技术栈？”
- “probuff过来更新10W条数据，我要更新实时库；也会批量取10W–20W做二次计算”

**学生的初始理解**:
- 认为可能需要B+树来做批量查询或更新
- 不清楚共享内存与索引结构的分工

**教学方法**:
- 先明确主负载类型（等值点查/更新 vs 范围/排序）
- 对比Hash与B+树的适用性与复杂度
- 给出架构建议与批处理路径

**提供的代码示例**:
```cpp
// C++标准：C++17
#include <map>
#include <tuple>
#include <vector>
#include <limits>
#include <iostream>

using DevPntKey = std::tuple<int,int>;
using DevValPntKey = std::tuple<int,double,int>;

int main() {
    std::map<DevPntKey, int> by_dev_pnt;
    auto ins_dev_pnt = [&](int dev, int pnt, int payload) {
        by_dev_pnt.emplace(DevPntKey{dev, pnt}, payload);
    };
    for (int p = 1; p <= 5; ++p) ins_dev_pnt(1, p, p * 10);

    auto lo1 = DevPntKey{1, std::numeric_limits<int>::min()};
    auto hi1 = DevPntKey{1, std::numeric_limits<int>::max()};
    auto it1 = by_dev_pnt.lower_bound(lo1);
    auto ed1 = by_dev_pnt.upper_bound(hi1);
    std::vector<int> out_pnt_sorted;
    for (; it1 != ed1; ++it1) out_pnt_sorted.push_back(it1->second);
    std::cout << out_pnt_sorted.size() << "\n";

    std::map<DevValPntKey, int> by_dev_val_pnt;
    auto ins_dev_val = [&](int dev, double val, int pnt, int payload) {
        by_dev_val_pnt.emplace(DevValPntKey{dev, val, pnt}, payload);
    };
    ins_dev_val(1, 98.0, 1, 100);
    ins_dev_val(1, 99.5, 2, 200);
    ins_dev_val(1, 100.1, 3, 300);

    double thr = 99.99;
    auto lo2 = DevValPntKey{1, thr, std::numeric_limits<int>::min()};
    auto hi2 = DevValPntKey{1, std::numeric_limits<double>::infinity(), std::numeric_limits<int>::max()};
    auto it2 = by_dev_val_pnt.lower_bound(lo2);
    auto ed2 = by_dev_val_pnt.upper_bound(hi2);
    std::vector<int> out_val_ge;
    for (; it2 != ed2; ++it2) out_val_ge.push_back(it2->second);
    std::cout << out_val_ge.size() << "\n";
    return 0;
}
```

**理解检查**:
- **提问**: 你的主负载是否以等值点查/更新为主？是否需要频繁的范围/排序？
- **学生回应**: 主负载是批量等值（10W–20W），但也有按设备筛选并按测点编号排序、按值阈值过滤的需求
- **理解程度**: ✅完全理解

**跟进行动**:
- 采用“共享内存快照 + 哈希主索引 + 设备维度二级有序结构（按需）”的架构；范围/排序负载高频时为设备维度维护有序索引

---

### 问题2: B‑Tree 的使用场景是什么
<a id="问题2-b-tree-使用场景"></a>

**原始问题**:
- “B+树不是必需品，仅在确有大量范围/前缀/排序遍历时更合适，能给出应用场景吗？”
- “我要筛选1000W点里面value值在某个范围内的所有测点，用B‑Tree更快？”

**学生的初始理解**:
- 对B‑Tree适用的查询类型不确定

**教学方法**:
- 引用权威文档明确B‑Tree支持的操作（比较、范围、ORDER BY、前缀），Hash仅支持等值
- 用复合键示例说明设备内排序、阈值筛选、范围扫描

**权威来源（数据库索引行为）**:
- MySQL 8.x: B‑Tree支持 =, >, >=, <, <=, BETWEEN 与左前缀LIKE；Hash仅等值、不能用于ORDER BY或范围扫描  
  https://dev.mysql.com/doc/refman/8.0/en/index-btree-hash.html
- PostgreSQL: B‑Tree可处理等值与范围查询；Hash仅等值  
  https://www.postgresql.org/docs/current/indexes-types.html

**理解检查**:
- **提问**: 你的范围筛选是全库还是设备内？是否需要分页/Top‑K？
- **学生回应**: 既有设备内排序，又可能做按值区间筛选；若高频则需要有序索引
- **理解程度**: ✅完全理解

**跟进行动**:
- 若范围/排序查询高频：为设备维度维护有序二级索引（键建议为 device_id, value, pnt_id），查询用 lower_bound 做范围扫描
- 若偶发：用哈希取子集后线性过滤，结合“偏移排序→顺序读”优化访存

---

## 知识盲点识别

### 高优先级盲点
- **有序索引维护成本评估**: 如何在高写入场景下维护每设备有序索引的插入/删除代价
  - 严重程度: 中高
  - 影响范围: 高频范围/排序查询的引入时机
  - 建议行动: 压测“排序数组重建 vs B‑Tree增量维护”的更新成本

### 中优先级盲点
- **跨进程一致性策略**: 读无锁快照（RCU/双缓冲）与细粒度锁的权衡
  - 严重程度: 中
  - 建议行动: 原型对比两种策略的延迟与一致性

### 低优先级盲点
- **复合键选择**: (device_id, value, pnt_id) vs (device_id, pnt_id, value) 的排序输出差异
  - 严重程度: 低
  - 建议行动: 根据常用排序维度选择主序

---

## 已掌握的主题

### 实时库索引选型与架构
- **置信度**: 高
- **掌握的关键点**:
  1. 主负载等值查询/更新优先哈希；范围/排序用有序二级索引
  2. 共享内存承载当前值快照；偏移排序提高批量读/写局部性
  3. 设备维度维护紧凑列表或有序容器支持筛选与排序

### B‑Tree使用场景
- **置信度**: 高
- **掌握的关键点**:
  1. 支持比较运算与BETWEEN范围、ORDER BY、有序遍历
  2. 复合键可做设备内排序、按值阈值快速范围扫描
  3. Hash仅等值，不用于排序/范围

---

## 关键见解与后续跟进
- 主索引用哈希满足批量等值；为高频范围/排序需求在设备维度引入有序二级索引
- 原型与压测：对比“线性过滤方案”与“有序索引方案”的延迟、吞吐与维护成本
