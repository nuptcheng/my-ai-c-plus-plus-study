#include "net/logger.h"
#include <taos.h> // 涛思数据库头文件
#include <string>
#include <chrono>
#include <vector>
#include <algorithm>
#include <ctime>
#include <map>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <random>
#include <iomanip>

// 全局变量
std::atomic<int> total_tables_created(0);
std::atomic<int> total_rows_inserted(0);
std::mutex g_mutex;

// 使用C++11的方式获取当前时间戳(ms)
int64_t get_timestamp_ms() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// 获取当前微秒级时间戳
int64_t get_timestamp_us() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

// 获取格式化的时间戳字符串（精确到微秒）
std::string get_formatted_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1000000;

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));

    char timestamp[48];
    sprintf(timestamp, "%s.%06ld", buffer, now_us);
    return std::string(timestamp);
}

void print_usage(const char* prog_name) {
    printf("用法: %s [是否创建表] [线程数] [子表数量] [插入次数]\n", prog_name);
    printf("  是否创建表: 1表示创建超级表和子表，0表示不创建\n");
    printf("  线程数: 创建表和插入数据的线程数\n");
    printf("  子表数量: 要创建的子表数量\n");
    printf("  插入次数: 每个子表插入的记录数\n");
    printf("示例: %s 1 16 10000 1000 - 创建超级表和1万个子表，每个子表写入1000条记录，使用16个线程\n", prog_name);
}

// 统一子表名生成逻辑，使用taosbenchmark中的前缀方式
std::string get_table_name(int id) {
    char name[64];
    sprintf(name, "s_%d", id);
    return std::string(name);
}

// 创建超级表
bool create_super_table(TAOS* taos) {
    // 删除已存在的表
    TAOS_RES* res = taos_query(taos, "DROP STABLE IF EXISTS history");
    taos_free_result(res);

    // 根据test.json中的定义，创建超级表
    std::string create_stmt = "CREATE STABLE IF NOT EXISTS history ("
                              "ts TIMESTAMP, "
                              "v FLOAT, "    // test.json中的column
                              "r FLOAT, "    // test.json中的column
                              "q INT, "      // test.json中的column
                              "b TINYINT"    // test.json中的column
                              ") TAGS ("
                              "i VARCHAR(32), "  // test.json中的tag
                              "c TINYINT"        // test.json中的tag
                              ")";

    res = taos_query(taos, create_stmt.c_str());
    int code = taos_errno(res);
    if (code != 0) {
        log_error("创建超级表失败: %s", taos_errstr(res));
        taos_free_result(res);
        return false;
    }
    taos_free_result(res);
    return true;
}

// 线程函数：创建子表
void create_tables_thread(TAOS* taos, int start_id, int end_id, int thread_id) {
    char sql[256];
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> c_dist(0, 10); // 根据test.json中c的范围 0-10

    log_info("线程%d开始创建子表，范围: %d - %d", thread_id, start_id, end_id);

    for (int i = start_id; i < end_id; i++) {
        std::string table_name = get_table_name(i);
        // 根据test.json中的定义，i是一个带前缀的递增值，c是0-10的随机值
        std::string tag_i = "SN_" + std::to_string(i);
        int tag_c = c_dist(gen);

        sprintf(sql, "CREATE TABLE IF NOT EXISTS %s USING history TAGS ('%s', %d)",
                table_name.c_str(), tag_i.c_str(), tag_c);

        TAOS_RES* res = taos_query(taos, sql);
        int code = taos_errno(res);
        if (code != 0) {
            log_error("线程%d创建子表%s失败: %s", thread_id, table_name.c_str(), taos_errstr(res));
            taos_free_result(res);
            continue;
        }
        taos_free_result(res);

        // 原子操作增加计数器
        total_tables_created++;
    }

    log_info("线程%d完成子表创建任务", thread_id);
}

// 线程函数：使用STMT方式插入数据
void insert_data_thread(TAOS* taos, int start_id, int end_id, int rows_per_table, int thread_id) {
    // 创建随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> v_dist(220.0, 240.0); // 根据test.json中v的范围
    std::uniform_real_distribution<> r_dist(0.1, 10.0);    // 根据test.json中r的范围
    std::uniform_int_distribution<> q_dist(0, 16);         // 根据test.json中q的范围
    std::uniform_int_distribution<> b_dist(0, 10);         // 根据test.json中b的范围

    log_info("线程%d开始插入数据，子表范围: %d - %d, 每表%d行", thread_id, start_id, end_id, rows_per_table);

    int local_inserted = 0;

    for (int table_id = start_id; table_id < end_id; table_id++) {
        std::string table_name = get_table_name(table_id);

        // 创建STMT对象
        TAOS_STMT* stmt = taos_stmt_init(taos);
        if (stmt == NULL) {
            log_error("线程%d为表%s初始化STMT失败", thread_id, table_name.c_str());
            continue;
        }

        // 准备SQL语句
        std::string sql = "INSERT INTO ? VALUES(?, ?, ?, ?, ?)";
        if (taos_stmt_prepare(stmt, sql.c_str(), 0) != 0) {
            log_error("线程%d为表%s准备STMT失败: %s", thread_id, table_name.c_str(), taos_stmt_errstr(stmt));
            taos_stmt_close(stmt);
            continue;
        }

        // 设置表名
        if (taos_stmt_set_tbname(stmt, table_name.c_str()) != 0) {
            log_error("线程%d为表%s设置表名失败: %s", thread_id, table_name.c_str(), taos_stmt_errstr(stmt));
            taos_stmt_close(stmt);
            continue;
        }

        // 定义数据结构
        struct {
            int64_t ts;
            float v_val;
            float r_val;
            int32_t q_val;
            int8_t b_val;
        } data;

        // 定义长度变量
        int32_t ts_len = sizeof(int64_t);
        int32_t float_len = sizeof(float);
        int32_t int_len = sizeof(int32_t);
        int32_t tinyint_len = sizeof(int8_t);

        // 绑定参数
        TAOS_MULTI_BIND params[5];

        // 时间戳参数
        params[0].buffer_type = TSDB_DATA_TYPE_TIMESTAMP;
        params[0].buffer_length = sizeof(data.ts);
        params[0].buffer = &data.ts;
        params[0].length = &ts_len;
        params[0].is_null = NULL;
        params[0].num = 1;

        // v字段参数
        params[1].buffer_type = TSDB_DATA_TYPE_FLOAT;
        params[1].buffer_length = sizeof(data.v_val);
        params[1].buffer = &data.v_val;
        params[1].length = &float_len;
        params[1].is_null = NULL;
        params[1].num = 1;

        // r字段参数
        params[2].buffer_type = TSDB_DATA_TYPE_FLOAT;
        params[2].buffer_length = sizeof(data.r_val);
        params[2].buffer = &data.r_val;
        params[2].length = &float_len;
        params[2].is_null = NULL;
        params[2].num = 1;

        // q字段参数
        params[3].buffer_type = TSDB_DATA_TYPE_INT;
        params[3].buffer_length = sizeof(data.q_val);
        params[3].buffer = &data.q_val;
        params[3].length = &int_len;
        params[3].is_null = NULL;
        params[3].num = 1;

        // b字段参数
        params[4].buffer_type = TSDB_DATA_TYPE_TINYINT;
        params[4].buffer_length = sizeof(data.b_val);
        params[4].buffer = &data.b_val;
        params[4].length = &tinyint_len;
        params[4].is_null = NULL;
        params[4].num = 1;

        // 逐行插入数据
        for (int row = 0; row < rows_per_table; row++) {
            // 设置时间戳，每条记录间隔1000ms（根据test.json中的timestamp_step）
            data.ts = get_timestamp_ms() + row * 1000;

            // 生成随机数据
            data.v_val = v_dist(gen);
            data.r_val = r_dist(gen);
            data.q_val = q_dist(gen);
            data.b_val = b_dist(gen);

            // 绑定参数
            if (taos_stmt_bind_param(stmt, params) != 0) {
                log_error("线程%d为表%s绑定参数失败: %s", thread_id, table_name.c_str(), taos_stmt_errstr(stmt));
                continue;
            }

            // 添加一行记录到批处理
            if (taos_stmt_add_batch(stmt) != 0) {
                log_error("线程%d为表%s添加批次失败: %s", thread_id, table_name.c_str(), taos_stmt_errstr(stmt));
                continue;
            }
        }

        // 执行批量插入
        if (taos_stmt_execute(stmt) != 0) {
            log_error("线程%d为表%s执行STMT失败: %s", thread_id, table_name.c_str(), taos_stmt_errstr(stmt));
            taos_stmt_close(stmt);
            continue;
        }

        // 关闭STMT
        taos_stmt_close(stmt);

        // 更新本地计数器
        local_inserted += rows_per_table;

        // 定期更新全局计数器，避免过多的原子操作
        if (table_id % 10 == 0 || table_id == end_id - 1) {
            total_rows_inserted += local_inserted;
            local_inserted = 0;

            // 定期输出进度
            if (table_id % 100 == 0 || table_id == end_id - 1) {
                int progress = (table_id - start_id + 1) * 100 / (end_id - start_id);
                log_info("线程%d进度: %d%%, 已处理%d个表", thread_id, progress, table_id - start_id + 1);
            }
        }
    }

    // 确保所有记录都计入总数
    if (local_inserted > 0) {
        total_rows_inserted += local_inserted;
    }

    log_info("线程%d完成数据插入任务", thread_id);
}

int main(int argc, char** argv) {
    init_logger(argc, argv);

    // 解析命令行参数
    bool do_create = true;      // 是否创建表
    int thread_count = 16;      // 线程数
    int table_count = 10000;    // 子表数量
    int rows_per_table = 1000;  // 每个子表插入的行数

    if (argc < 5) {
        log_error("参数不足", "");
        print_usage(argv[0]);
        return 1;
    }

    do_create = atoi(argv[1]) != 0;
    thread_count = atoi(argv[2]);
    table_count = atoi(argv[3]);
    rows_per_table = atoi(argv[4]);

    if (thread_count <= 0 || table_count <= 0 || rows_per_table <= 0) {
        log_error("参数无效: 线程数、表数量和插入次数必须大于0", "");
        print_usage(argv[0]);
        return 1;
    }

    log_info("准备测试: 创建表=%s, 线程数=%d, 子表数量=%d, 每表行数=%d",
             do_create ? "是" : "否", thread_count, table_count, rows_per_table);

    // 连接涛思数据库
    TAOS* taos = taos_connect("localhost", "root", "taosdata", NULL, 0);
    if (taos == NULL) {
        log_error("连接涛思数据库失败: %s", taos_errstr(NULL));
        return 1;
    }

    // 创建数据库(根据test.json中的设置)
    TAOS_RES* res = taos_query(taos, "DROP DATABASE IF EXISTS spark");
    taos_free_result(res);

    res = taos_query(taos, "CREATE DATABASE IF NOT EXISTS spark VGROUPS 16 PRECISION 'ms'");
    int code = taos_errno(res);
    if (code != 0) {
        log_error("创建数据库失败: %s", taos_errstr(res));
        taos_free_result(res);
        taos_close(taos);
        return 1;
    }
    taos_free_result(res);

    // 使用数据库
    res = taos_query(taos, "USE spark");
    code = taos_errno(res);
    if (code != 0) {
        log_error("使用数据库失败: %s", taos_errstr(res));
        taos_free_result(res);
        taos_close(taos);
        return 1;
    }
    taos_free_result(res);

    // 记录程序总开始时间
    auto program_start_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> tables_elapsed(0);

    // 第一阶段：创建超级表和子表（如果需要）
    if (do_create) {
        log_info("第一阶段：创建超级表和子表...", "");
        auto tables_start_time = std::chrono::high_resolution_clock::now();

        // 创建超级表
        if (!create_super_table(taos)) {
            taos_close(taos);
            return 1;
        }

        // 创建子表（多线程）
        std::vector<std::thread> create_threads;
        int tables_per_thread = (table_count + thread_count - 1) / thread_count;

        for (int i = 0; i < thread_count; i++) {
            int start_id = i * tables_per_thread;
            int end_id = std::min(start_id + tables_per_thread, table_count);

            if (start_id >= table_count) {
                break;
            }

            // 为每个线程创建一个独立的连接
            TAOS* thread_taos = taos_connect("localhost", "root", "taosdata", "spark", 0);
            if (thread_taos == NULL) {
                log_error("为线程%d创建数据库连接失败", i);
                continue;
            }

            // 启动创建表的线程
            create_threads.emplace_back(create_tables_thread, thread_taos, start_id, end_id, i);
        }

        // 等待所有创建表的线程完成
        for (auto &thread : create_threads) {
            thread.join();
        }

        // 计算子表创建耗时
        auto tables_end_time = std::chrono::high_resolution_clock::now();
        tables_elapsed = tables_end_time - tables_start_time;
        log_info("子表创建完成，共创建 %d 个子表，耗时 %.2f 秒",
                 total_tables_created.load(), tables_elapsed.count());
    }

    // 第二阶段：使用STMT方式插入数据
    log_info("第二阶段：开始插入数据...", "");
    auto insert_start_time = std::chrono::high_resolution_clock::now();

    // 多线程插入数据
    std::vector<std::thread> insert_threads;
    int tables_per_thread = (table_count + thread_count - 1) / thread_count;

    for (int i = 0; i < thread_count; i++) {
        int start_id = i * tables_per_thread;
        int end_id = std::min(start_id + tables_per_thread, table_count);

        if (start_id >= table_count) {
            break;
        }

        // 为每个线程创建一个独立的连接
        TAOS* thread_taos = taos_connect("localhost", "root", "taosdata", "spark", 0);
        if (thread_taos == NULL) {
            log_error("为线程%d创建数据库连接失败", i);
            continue;
        }

        // 启动插入数据的线程
        insert_threads.emplace_back(insert_data_thread, thread_taos, start_id, end_id, rows_per_table, i);
    }

    // 等待所有插入数据的线程完成
    for (auto &thread : insert_threads) {
        thread.join();
    }

    // 计算数据插入耗时
    auto insert_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> insert_elapsed = insert_end_time - insert_start_time;

    // 计算总耗时
    std::chrono::duration<double> total_elapsed = insert_end_time - program_start_time;

    // 输出性能统计结果
    log_info("测试完成，统计结果:", "");
    if (do_create) {
        log_info("子表创建阶段: %.2f 秒", tables_elapsed.count());
        log_info("子表创建数量: %d", total_tables_created.load());
        log_info("子表创建速率: %.2f 个/秒", total_tables_created.load() / tables_elapsed.count());
    }

    log_info("数据插入阶段: %.2f 秒", insert_elapsed.count());
    log_info("总耗时: %.2f 秒", total_elapsed.count());
    log_info("子表数量: %d", table_count);
    log_info("每个子表插入行数: %d", rows_per_table);
    log_info("线程数: %d", thread_count);
    log_info("理论总行数: %d", table_count * rows_per_table);
    log_info("实际插入行数: %d", total_rows_inserted.load());
    log_info("插入数据速率: %.2f 万行/秒", total_rows_inserted.load() / insert_elapsed.count() / 10000);

    // 关闭连接
    taos_close(taos);
    return 0;
}
