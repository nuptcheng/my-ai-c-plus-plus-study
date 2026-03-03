/**
 * @file srealtimedb_bptree_test.cpp
 * @author zhangyun
 * @brief 实时库B+树测试用例
 * @version 1.0.0
 * @date 2026-02-26
 *
 * @copyright Energy Storage Technology Institute Co.,Ltd. All rights reserved.
 */
#include "catch_amalgamated.hpp"
#include "srealtimedb_bptree.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cmath>

TEST_CASE("B+树基础功能测试", "[bptree][basic]") {
    std::cout << "\n--- B+树基础功能测试 ---" << std::endl;

    const sint32 max_records = 256;  // 改为256条记录
    const sint32 key_len = sizeof(uint64);
    const sint32 key_align = 8;

    sint32 bptree_size = SRdbBptreeCtrl::calc_total_memory(max_records, key_len, key_align);

    char* bptree_mem = new char[bptree_size];
    memset(bptree_mem, 0, bptree_size);

    SRdbBptree tree;
    tree.bind(bptree_mem);

    // 单列 uint64 键配置
    sint32 offset = 0;
    BaseDataType datatype = BaseDataType::UINT64;

    SECTION("B+树初始化测试") {
        sint32 ret = tree.init(max_records, 1, &offset, &datatype, key_align);
        REQUIRE(ret == SRDB_OK);
        std::cout << "✓ B+树初始化成功" << std::endl;
    }

    SECTION("B+树插入和查询性能测试") {
        tree.init(max_records, 1, &offset, &datatype, key_align);

        // 计算理论层高
        // B+树每个节点最多有ORDER个键，最少有ORDER/2个键
        // 对于256条记录，理论最大层高 = log(ORDER/2)(256) + 1
        sint32 min_keys_per_node = SRDB_BPTREE_ORDER / 2;
        double theoretical_height = log(max_records) / log(min_keys_per_node) + 1;
        std::cout << "B+树阶数: " << SRDB_BPTREE_ORDER << std::endl;
        std::cout << "理论最大层高: " << static_cast<sint32>(ceil(theoretical_height)) << std::endl;

        // 插入256条记录并计算性能
        auto start = std::chrono::high_resolution_clock::now();

        for (sint32 i = 0; i < max_records; i++) {
            uint64 key = i;
            sint32 ret = tree.insert_key(&key, i);
            REQUIRE(ret == SRDB_OK);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "✓ 插入" << max_records << "条记录成功" << std::endl;
        std::cout << "  插入总耗时: " << duration.count() << "微秒" << std::endl;
        std::cout << "  每次插入平均耗时: " << static_cast<double>(duration.count()) / max_records << "微秒" << std::endl;
        std::cout << "  平均插入速度: " << max_records * 1000.0 / duration.count() << "条/毫秒" << std::endl;

        // 从最大值到0全量查询
        start = std::chrono::high_resolution_clock::now();

        for (sint32 i = max_records - 1; i >= 0; i--) {
            uint64 key = i;
            sint32 record_no = -1;
            sint32 ret = tree.search_key(&key, &record_no);
            REQUIRE(ret == SRDB_OK);
            REQUIRE(record_no == i);
        }

        end = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "✓ 从最大值到0查询" << max_records << "条记录成功" << std::endl;
        std::cout << "  查询总耗时: " << duration.count() << "微秒" << std::endl;
        std::cout << "  每次查询平均耗时: " << static_cast<double>(duration.count()) / max_records << "微秒" << std::endl;
        std::cout << "  平均查询速度: " << max_records * 1000.0 / duration.count() << "条/毫秒" << std::endl;

        // 查询不存在的记录
        uint64 key = 999;
        sint32 record_no = -1;
        sint32 ret = tree.search_key(&key, &record_no);
        REQUIRE(ret == SRDB_ERR_NO_RECORD);
        std::cout << "✓ 查询不存在记录返回正确错误码" << std::endl;
    }

    SECTION("B+树遍历测试") {
        tree.init(max_records, 1, &offset, &datatype, key_align);

        // 插入10条记录
        for (sint32 i = 0; i < 10; i++) {
            uint64 key = i;
            tree.insert_key(&key, i);
        }

        // 获取第一个键
        uint64 key = 0;
        sint32 record_no = -1;
        sint32 ret = tree.get_first_key(&key, &record_no);
        REQUIRE(ret == SRDB_OK);
        REQUIRE(key == 0);
        REQUIRE(record_no == 0);

        // 遍历所有键
        sint32 count = 1;
        while (tree.get_next_key(&key, &record_no) == SRDB_OK) {
            count++;
        }
        REQUIRE(count == 10);
        std::cout << "✓ B+树遍历测试通过，遍历" << count << "条记录" << std::endl;
    }

    delete[] bptree_mem;
}

TEST_CASE("B+树中等数据量测试", "[bptree][medium]") {
    std::cout << "\n--- B+树中等数据量测试 (10万条记录) ---" << std::endl;

    const sint32 max_records = 100000;
    const sint32 key_len = sizeof(uint64);
    const sint32 key_align = 8;

    sint32 bptree_size = SRdbBptreeCtrl::calc_total_memory(max_records, key_len, key_align);

    std::cout << "B+树内存大小: " << bptree_size / 1024 / 1024 << "MB" << std::endl;
    std::cout << "B+树阶数: " << SRDB_BPTREE_ORDER << std::endl;

    // 计算理论层高
    sint32 min_keys_per_node = SRDB_BPTREE_ORDER / 2;
    double theoretical_height = log(max_records) / log(min_keys_per_node) + 1;
    std::cout << "理论最大层高: " << static_cast<sint32>(ceil(theoretical_height)) << std::endl;

    char* bptree_mem = new char[bptree_size];
    memset(bptree_mem, 0, bptree_size);

    SRdbBptree tree;
    tree.bind(bptree_mem);

    // 单列 uint64 键配置
    sint32 offset = 0;
    BaseDataType datatype = BaseDataType::UINT64;
    tree.init(max_records, 1, &offset, &datatype, key_align);

    SECTION("插入10万条记录") {
        auto start = std::chrono::high_resolution_clock::now();

        for (sint32 i = 0; i < 100000; i++) {
            uint64 key = i;
            sint32 ret = tree.insert_key(&key, i);
            REQUIRE(ret == SRDB_OK);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "✓ 插入10万条记录总耗时: " << duration.count() << "ms" << std::endl;
        std::cout << "  每次插入平均耗时: " << static_cast<double>(duration.count()) * 1000 / 100000 << "微秒" << std::endl;
        std::cout << "  平均插入速度: " << 100000.0 / duration.count() * 1000 << "条/秒" << std::endl;

        // 从最大值到0全量查询
        start = std::chrono::high_resolution_clock::now();
        for (sint32 i = 99999; i >= 0; i--) {
            uint64 key = i;
            sint32 record_no = -1;
            sint32 ret = tree.search_key(&key, &record_no);
            REQUIRE(ret == SRDB_OK);
            REQUIRE(record_no == key);
        }

        end = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "✓ 从最大值到0查询10万条记录总耗时: " << duration.count() << "ms" << std::endl;
        std::cout << "  每次查询平均耗时: " << static_cast<double>(duration.count()) * 1000 / 100000 << "微秒" << std::endl;
    }

    delete[] bptree_mem;
}

TEST_CASE("B+树大数据量测试", "[bptree][large]") {
    std::cout << "\n--- B+树大数据量测试 (1000万条记录) ---" << std::endl;

    const sint32 max_records = 10000000;
    const sint32 key_len = sizeof(uint64);
    const sint32 key_align = 8;

    sint32 bptree_size = SRdbBptreeCtrl::calc_total_memory(max_records, key_len, key_align);

    std::cout << "B+树内存大小: " << bptree_size / 1024 / 1024 << "MB" << std::endl;
    std::cout << "B+树阶数: " << SRDB_BPTREE_ORDER << std::endl;

    // 计算理论层高
    sint32 min_keys_per_node = SRDB_BPTREE_ORDER / 2;
    double theoretical_height = log(max_records) / log(min_keys_per_node) + 1;
    std::cout << "理论最大层高: " << static_cast<sint32>(ceil(theoretical_height)) << std::endl;

    char* bptree_mem = new char[bptree_size];
    memset(bptree_mem, 0, bptree_size);

    SRdbBptree tree;
    tree.bind(bptree_mem);

    // 单列 uint64 键配置
    sint32 offset = 0;
    BaseDataType datatype = BaseDataType::UINT64;
    tree.init(max_records, 1, &offset, &datatype, key_align);

    SECTION("顺序插入1000万条记录") {
        auto start = std::chrono::high_resolution_clock::now();

        for (sint32 i = 0; i < 10000000; i++) {
            uint64 key = i;
            sint32 ret = tree.insert_key(&key, i);
            REQUIRE(ret == SRDB_OK);

            if ((i + 1) % 1000000 == 0) {
                auto current = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current - start);
                std::cout << "  已插入 " << (i + 1) / 10000 << "万条记录，耗时: "
                         << elapsed.count() << "秒" << std::endl;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

        std::cout << "✓ 插入1000万条记录总耗时: " << duration.count() << "秒" << std::endl;
        std::cout << "  每次插入平均耗时: " << static_cast<double>(duration.count()) * 1000000 / 10000000 << "微秒" << std::endl;
        std::cout << "  平均插入速度: " << 10000000 / duration.count() << "条/秒" << std::endl;

        // 从最大值到0全量查询
        start = std::chrono::high_resolution_clock::now();
        for (sint32 i = 9999999; i >= 0; i--) {
            uint64 key = i;
            sint32 record_no = -1;
            sint32 ret = tree.search_key(&key, &record_no);
            REQUIRE(ret == SRDB_OK);
            REQUIRE(record_no == key);

            if ((9999999 - i + 1) % 1000000 == 0) {
                auto current = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current - start);
                std::cout << "  已查询 " << (9999999 - i + 1) / 10000 << "万条记录，耗时: "
                         << elapsed.count() << "秒" << std::endl;
            }
        }
        end = std::chrono::high_resolution_clock::now();
        auto duration_sec = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        std::cout << "✓ 从最大值到0查询1000万条记录总耗时: " << duration_sec.count() << "秒" << std::endl;
        std::cout << "  每次查询平均耗时: " << static_cast<double>(duration_sec.count()) * 1000000 / 10000000 << "微秒" << std::endl;
    }

    // SECTION("乱序插入1000万条记录") {
    //     auto start = std::chrono::high_resolution_clock::now();

    //     // 生成乱序序列
    //     std::vector<sint32> keys;
    //     keys.reserve(10000000);
    //     for (sint32 i = 0; i < 10000000; i++) {
    //         keys.push_back(i);
    //     }
    //     std::random_shuffle(keys.begin(), keys.end());
    //     std::cout << "✓ 生成1000万条乱序键值" << std::endl;

    //     // 乱序插入
    //     for (sint32 i = 0; i < 10000000; i++) {
    //         sint32 ret = tree.insert_key(&keys[i], keys[i]);
    //         REQUIRE(ret == SRDB_OK);

    //         if ((i + 1) % 1000000 == 0) {
    //             auto current = std::chrono::high_resolution_clock::now();
    //             auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current - start);
    //             std::cout << "  已乱序插入 " << (i + 1) / 10000 << "万条记录，耗时: "
    //                      << elapsed.count() << "秒" << std::endl;
    //         }
    //     }

    //     auto end = std::chrono::high_resolution_clock::now();
    //     auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    //     std::cout << "✓ 乱序插入1000万条记录总耗时: " << duration.count() << "秒" << std::endl;
    //     std::cout << "  每次插入平均耗时: " << static_cast<double>(duration.count()) * 1000000 / 10000000 << "微秒" << std::endl;
    //     std::cout << "  平均插入速度: " << 10000000 / duration.count() << "条/秒" << std::endl;
    // }

    delete[] bptree_mem;
}

// TEST_CASE("B+树复合键测试", "[bptree][composite]") {
//     std::cout << "\n--- B+树复合键测试 ---" << std::endl;

//     const sint32 max_records = 10000;
//     const sint32 key_len = sizeof(sint32) * 2;
//     const sint32 key_align = 4;

//     sint32 bptree_size = SRdbBptreeCtrl::calc_total_memory(max_records, key_len, key_align);

//     char* bptree_mem = new char[bptree_size];
//     memset(bptree_mem, 0, bptree_size);

//     SRdbKeyBptree tree;
//     tree.bind(bptree_mem);
//     tree.init_bptree(max_records, key_len, key_align);

//     // 初始化复合键比较
//     sint32 offsets[2] = {0, sizeof(sint32)};
//     BaseDataType types[2] = {BaseDataType::SINT32, BaseDataType::SINT32};
//     tree.init_compare(0, 2, offsets, types);

//     SECTION("复合键插入和查询") {
//         // 插入100条复合键记录
//         for (sint32 i = 0; i < 100; i++) {
//             sint32 key[2] = {i / 10, i % 10};
//             sint32 ret = tree.insert_key(key, i);
//             REQUIRE(ret == SRDB_OK);
//         }
//         std::cout << "✓ 插入100条复合键记录成功" << std::endl;

//         // 查询复合键
//         for (sint32 i = 0; i < 100; i++) {
//             sint32 key[2] = {i / 10, i % 10};
//             sint32 record_no = -1;
//             sint32 ret = tree.search_key(key, &record_no);
//             REQUIRE(ret == SRDB_OK);
//             REQUIRE(record_no == i);
//         }
//         std::cout << "✓ 查询100条复合键记录成功" << std::endl;
//     }

//     delete[] bptree_mem;
// }

TEST_CASE("B+树复合键测试", "[bptree][composite]") {
    std::cout << "\n--- B+树复合键测试 ---" << std::endl;

    // 定义复合键结构：设备ID + 时间戳
    struct CompositeKey {
        sint32 device_id;
        sint32 timestamp;
    };

    const sint32 max_records = 1000;
    const sint32 key_len = sizeof(CompositeKey);
    const sint32 key_align = 4;

    sint32 bptree_size = SRdbBptreeCtrl::calc_total_memory(max_records, key_len, key_align);

    char* bptree_mem = new char[bptree_size];
    memset(bptree_mem, 0, bptree_size);

    SRdbBptree tree;
    tree.bind(bptree_mem);

    // 配置复合键：2列，device_id和timestamp
    sint32 col_offset[2] = {
        offsetof(CompositeKey, device_id),
        offsetof(CompositeKey, timestamp)
    };
    BaseDataType col_datatype[2] = {
        BaseDataType::SINT32,
        BaseDataType::SINT32
    };

    SECTION("复合键初始化测试") {
        sint32 ret = tree.init(max_records, 2, col_offset, col_datatype, key_align);
        REQUIRE(ret == SRDB_OK);
        std::cout << "✓ 复合键B+树初始化成功" << std::endl;
    }

    SECTION("复合键插入和查询测试") {
        tree.init(max_records, 2, col_offset, col_datatype, key_align);

        // 插入100条记录：10个设备，每个设备10个时间戳
        std::cout << "插入100条复合键记录（10个设备 × 10个时间戳）..." << std::endl;

        auto start = std::chrono::high_resolution_clock::now();

        for (sint32 device = 0; device < 10; device++) {
            for (sint32 ts = 0; ts < 10; ts++) {
                CompositeKey key = {device, ts};
                sint32 record_no = device * 10 + ts;
                sint32 ret = tree.insert_key(&key, record_no);
                REQUIRE(ret == SRDB_OK);
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "✓ 插入100条复合键记录成功" << std::endl;
        std::cout << "  插入总耗时: " << duration.count() << "微秒" << std::endl;
        std::cout << "  每次插入平均耗时: " << static_cast<double>(duration.count()) / 100 << "微秒" << std::endl;

        // 查询所有记录
        start = std::chrono::high_resolution_clock::now();

        for (sint32 device = 0; device < 10; device++) {
            for (sint32 ts = 0; ts < 10; ts++) {
                CompositeKey key = {device, ts};
                sint32 record_no = -1;
                sint32 ret = tree.search_key(&key, &record_no);
                REQUIRE(ret == SRDB_OK);
                REQUIRE(record_no == device * 10 + ts);
            }
        }

        end = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "✓ 查询100条复合键记录成功" << std::endl;
        std::cout << "  查询总耗时: " << duration.count() << "微秒" << std::endl;
        std::cout << "  每次查询平均耗时: " << static_cast<double>(duration.count()) / 100 << "微秒" << std::endl;

        // 查询不存在的记录
        CompositeKey key_not_exist = {99, 99};
        sint32 record_no = -1;
        sint32 ret = tree.search_key(&key_not_exist, &record_no);
        REQUIRE(ret == SRDB_ERR_NO_RECORD);
        std::cout << "✓ 查询不存在的复合键返回正确错误码" << std::endl;
    }

    SECTION("复合键排序正确性测试") {
        tree.init(max_records, 2, col_offset, col_datatype, key_align);

        // 乱序插入
        std::vector<CompositeKey> keys;
        for (sint32 device = 0; device < 5; device++) {
            for (sint32 ts = 0; ts < 5; ts++) {
                keys.push_back({device, ts});
            }
        }

        // 打乱顺序
        std::srand(static_cast<unsigned>(std::time(nullptr)));
        std::random_shuffle(keys.begin(), keys.end());

        // 插入
        for (size_t i = 0; i < keys.size(); i++) {
            sint32 ret = tree.insert_key(&keys[i], static_cast<sint32>(i));
            REQUIRE(ret == SRDB_OK);
        }
        std::cout << "✓ 乱序插入25条复合键记录成功" << std::endl;

        // 遍历验证排序
        CompositeKey prev_key = {-1, -1};
        sint32 count = 0;

        CompositeKey key;
        sint32 record_no;
        sint32 ret = tree.get_first_key(&key, &record_no);

        while (ret == SRDB_OK) {
            // 验证排序：当前键应该大于前一个键
            if (count > 0) {
                bool is_greater = (key.device_id > prev_key.device_id) ||
                                 (key.device_id == prev_key.device_id && key.timestamp > prev_key.timestamp);
                REQUIRE(is_greater);
            }

            prev_key = key;
            count++;
            ret = tree.get_next_key(&key, &record_no);
        }

        REQUIRE(count == 25);
        std::cout << "✓ 复合键排序正确，遍历" << count << "条记录" << std::endl;
    }

    SECTION("复合键性能测试 - 1000条记录") {
        tree.init(max_records, 2, col_offset, col_datatype, key_align);

        std::cout << "插入1000条复合键记录（100个设备 × 10个时间戳）..." << std::endl;

        auto start = std::chrono::high_resolution_clock::now();

        for (sint32 device = 0; device < 100; device++) {
            for (sint32 ts = 0; ts < 10; ts++) {
                CompositeKey key = {device, ts};
                sint32 record_no = device * 10 + ts;
                sint32 ret = tree.insert_key(&key, record_no);
                REQUIRE(ret == SRDB_OK);
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "✓ 插入1000条复合键记录成功" << std::endl;
        std::cout << "  插入总耗时: " << duration.count() << "微秒" << std::endl;
        std::cout << "  每次插入平均耗时: " << static_cast<double>(duration.count()) / 1000 << "微秒" << std::endl;
        std::cout << "  平均插入速度: " << 1000 * 1000.0 / duration.count() << "条/毫秒" << std::endl;

        // 查询所有记录
        start = std::chrono::high_resolution_clock::now();

        for (sint32 device = 0; device < 100; device++) {
            for (sint32 ts = 0; ts < 10; ts++) {
                CompositeKey key = {device, ts};
                sint32 record_no = -1;
                sint32 ret = tree.search_key(&key, &record_no);
                REQUIRE(ret == SRDB_OK);
                REQUIRE(record_no == device * 10 + ts);
            }
        }

        end = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "✓ 查询1000条复合键记录成功" << std::endl;
        std::cout << "  查询总耗时: " << duration.count() << "微秒" << std::endl;
        std::cout << "  每次查询平均耗时: " << static_cast<double>(duration.count()) / 1000 << "微秒" << std::endl;
        std::cout << "  平均查询速度: " << 1000 * 1000.0 / duration.count() << "条/毫秒" << std::endl;
    }

    delete[] bptree_mem;
}

TEST_CASE("B+树混合类型复合键测试", "[bptree][composite][mixed]") {
    std::cout << "\n--- B+树混合类型复合键测试 ---" << std::endl;

    // 定义混合类型复合键：整数 + 浮点数
    struct MixedKey {
        sint32 id;
        float value;
    };

    const sint32 max_records = 100;
    const sint32 key_len = sizeof(MixedKey);
    const sint32 key_align = 4;

    sint32 bptree_size = SRdbBptreeCtrl::calc_total_memory(max_records, key_len, key_align);

    char* bptree_mem = new char[bptree_size];
    memset(bptree_mem, 0, bptree_size);

    SRdbBptree tree;
    tree.bind(bptree_mem);

    // 配置混合类型复合键
    sint32 col_offset[2] = {
        offsetof(MixedKey, id),
        offsetof(MixedKey, value)
    };
    BaseDataType col_datatype[2] = {
        BaseDataType::SINT32,
        BaseDataType::FLOAT
    };

    SECTION("混合类型复合键插入和查询") {
        tree.init(max_records, 2, col_offset, col_datatype, key_align);

        // 插入50条记录
        for (sint32 i = 0; i < 50; i++) {
            MixedKey key = {i / 10, static_cast<float>(i % 10) * 0.1f};
            sint32 ret = tree.insert_key(&key, i);
            REQUIRE(ret == SRDB_OK);
        }
        std::cout << "✓ 插入50条混合类型复合键记录成功" << std::endl;

        // 查询验证
        for (sint32 i = 0; i < 50; i++) {
            MixedKey key = {i / 10, static_cast<float>(i % 10) * 0.1f};
            sint32 record_no = -1;
            sint32 ret = tree.search_key(&key, &record_no);
            REQUIRE(ret == SRDB_OK);
            REQUIRE(record_no == i);
        }
        std::cout << "✓ 查询50条混合类型复合键记录成功" << std::endl;

        // 验证排序：先按id排序，id相同时按value排序
        MixedKey prev_key = {-1, -1.0f};
        sint32 count = 0;

        MixedKey key;
        sint32 record_no;
        sint32 ret = tree.get_first_key(&key, &record_no);

        while (ret == SRDB_OK) {
            if (count > 0) {
                bool is_greater = (key.id > prev_key.id) ||
                                 (key.id == prev_key.id && key.value > prev_key.value);
                REQUIRE(is_greater);
            }

            prev_key = key;
            count++;
            ret = tree.get_next_key(&key, &record_no);
        }

        REQUIRE(count == 50);
        std::cout << "✓ 混合类型复合键排序正确" << std::endl;
    }

    delete[] bptree_mem;
}

TEST_CASE("B+树复合键大规模性能测试", "[bptree][composite][large]") {
    std::cout << "\n--- B+树复合键大规模性能测试（1000万条记录）---" << std::endl;

    // 定义复合键结构：设备ID + 时间戳
    struct CompositeKey {
        sint32 device_id;
        sint32 timestamp;
    };

    const sint32 max_records = 10000000;  // 1000万条记录
    const sint32 key_len = sizeof(CompositeKey);
    const sint32 key_align = 4;

    std::cout << "计算B+树所需内存..." << std::endl;
    sint32 bptree_size = SRdbBptreeCtrl::calc_total_memory(max_records, key_len, key_align);
    std::cout << "B+树总内存大小: " << bptree_size / (1024 * 1024) << " MB" << std::endl;

    char* bptree_mem = new char[bptree_size];
    memset(bptree_mem, 0, bptree_size);

    SRdbBptree tree;
    tree.bind(bptree_mem);

    // 配置复合键：2列，device_id和timestamp
    sint32 col_offset[2] = {
        offsetof(CompositeKey, device_id),
        offsetof(CompositeKey, timestamp)
    };
    BaseDataType col_datatype[2] = {
        BaseDataType::SINT32,
        BaseDataType::SINT32
    };

    SECTION("复合键大规模插入测试") {
        tree.init(max_records, 2, col_offset, col_datatype, key_align);

        std::cout << "开始插入1000万条复合键记录（10000个设备 × 1000个时间戳）..." << std::endl;

        auto start = std::chrono::high_resolution_clock::now();
        sint32 progress_interval = max_records / 10;  // 每10%报告一次进度

        for (sint32 i = 0; i < max_records; i++) {
            sint32 device = i / 1000;
            sint32 ts = i % 1000;
            CompositeKey key = {device, ts};
            sint32 ret = tree.insert_key(&key, i);
            REQUIRE(ret == SRDB_OK);

            // 进度报告
            if ((i + 1) % progress_interval == 0) {
                auto current = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current - start);
                std::cout << "  已插入 " << (i + 1) / 10000 << "万条记录，耗时 "
                         << elapsed.count() << "秒" << std::endl;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

        std::cout << "✓ 插入1000万条复合键记录成功" << std::endl;
        std::cout << "  插入总耗时: " << duration.count() << "秒" << std::endl;
        std::cout << "  每次插入平均耗时: " << static_cast<double>(duration.count()) * 1000000 / max_records << "微秒" << std::endl;
        std::cout << "  平均插入速度: " << max_records / duration.count() << "条/秒" << std::endl;
        std::cout << "  平均插入速度: " << max_records / (duration.count() * 1000.0) << "条/毫秒" << std::endl;
    }

    SECTION("复合键大规模查询测试") {
        tree.init(max_records, 2, col_offset, col_datatype, key_align);

        // 先插入1000万条记录
        std::cout << "准备测试数据：插入1000万条复合键记录..." << std::endl;
        auto prep_start = std::chrono::high_resolution_clock::now();

        for (sint32 i = 0; i < max_records; i++) {
            sint32 device = i / 1000;
            sint32 ts = i % 1000;
            CompositeKey key = {device, ts};
            sint32 ret = tree.insert_key(&key, i);
            REQUIRE(ret == SRDB_OK);

            // 简化的进度报告
            if ((i + 1) % 1000000 == 0) {
                std::cout << "  已插入 " << (i + 1) / 10000 << "万条" << std::endl;
            }
        }

        auto prep_end = std::chrono::high_resolution_clock::now();
        auto prep_duration = std::chrono::duration_cast<std::chrono::seconds>(prep_end - prep_start);
        std::cout << "✓ 数据准备完成，耗时 " << prep_duration.count() << "秒" << std::endl;

        // 开始查询测试
        std::cout << "\n开始查询1000万条复合键记录..." << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        sint32 progress_interval = max_records / 10;

        for (sint32 i = 0; i < max_records; i++) {
            sint32 device = i / 1000;
            sint32 ts = i % 1000;
            CompositeKey key = {device, ts};
            sint32 record_no = -1;
            sint32 ret = tree.search_key(&key, &record_no);
            REQUIRE(ret == SRDB_OK);
            REQUIRE(record_no == i);

            // 进度报告
            if ((i + 1) % progress_interval == 0) {
                auto current = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current - start);
                std::cout << "  已查询 " << (i + 1) / 10000 << "万条记录，耗时 "
                         << elapsed.count() << "秒" << std::endl;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

        std::cout << "✓ 查询1000万条复合键记录成功" << std::endl;
        std::cout << "  查询总耗时: " << duration.count() << "秒" << std::endl;
        std::cout << "  每次查询平均耗时: " << static_cast<double>(duration.count()) * 1000000 / max_records << "微秒" << std::endl;
        std::cout << "  平均查询速度: " << max_records / duration.count() << "条/秒" << std::endl;
        std::cout << "  平均查询速度: " << max_records / (duration.count() * 1000.0) << "条/毫秒" << std::endl;
    }

    delete[] bptree_mem;
}


