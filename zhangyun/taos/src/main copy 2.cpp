#include "net/logger.h"
#include <taos.h> // 涛思数据库头文件
#include <string>
#include <chrono>
#include <vector>
#include <algorithm>
#include <ctime>
#include <map>
#include <sstream>

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
    printf("用法: %s [测点数量N] [重复写入次数M] [每次写入数量X]\n", prog_name);
    printf("  测点数量N: 将创建N个子表，默认为1000000\n");
    printf("  重复写入次数M: 每个测点重复写入M次，默认为1\n");
    printf("  每次写入数量X: 每次写入X条记录，默认为1000\n");
    printf("示例: %s 100000 10 5000 - 创建10万个测点，每个测点写入10次，每次写入5000条，总计写入100万条\n", prog_name);
}

// 待写入的数据结构
struct InsertData {
    int table_id;
    float d_val;
    float r_val;
    int q_val;
    std::string timestamp;  // 存储格式化的时间戳字符串
};

// 统一子表名生成逻辑，确保创建和写入使用相同的子表名
std::string get_table_name(int id) {
    char name[32];
    sprintf(name, "h_%d", id + 100000000);
    return std::string(name);
}

int main(int argc, char** argv) {
    init_logger(argc, argv);

    // 解析命令行参数
    int points_count = 1000000;  // 测点数量N，默认100万
    int repeat_times = 1;        // 重复写入次数M，默认1次
    int batch_size = 1000;       // 每次写入数量X，默认1000条
    int tables_per_batch = 1000;   // 每批处理的表数量，避免SQL过长

    if (argc > 1) {
        points_count = atoi(argv[1]);
        if (points_count <= 0) {
            log_error("无效的测点数量: %s", argv[1]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (argc > 2) {
        repeat_times = atoi(argv[2]);
        if (repeat_times <= 0) {
            log_error("无效的重复写入次数: %s", argv[2]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (argc > 3) {
        batch_size = atoi(argv[3]);
        if (batch_size <= 0) {
            log_error("无效的每次写入数量: %s", argv[3]);
            print_usage(argv[0]);
            return 1;
        }
    }

    int total_records = points_count * repeat_times;
    log_info("准备写入测试: 测点数量=%d, 重复次数=%d, 每次写入=%d, 总记录数=%d",
             points_count, repeat_times, batch_size, total_records);

    // 连接涛思数据库
    TAOS* taos = taos_connect("localhost", "root", "taosdata", NULL, 0);
    if (taos == NULL) {
        log_error("连接涛思数据库失败: %s", taos_errstr(NULL));
        return 1;
    }

    // 创建数据库
    TAOS_RES* res = taos_query(taos, "CREATE DATABASE IF NOT EXISTS test");
    int code = taos_errno(res);
    if (code != 0) {
        log_error("创建数据库失败: %s", taos_errstr(res));
        taos_free_result(res);
        taos_close(taos);
        return 1;
    }
    taos_free_result(res);

    // 使用数据库
    res = taos_query(taos, "USE test");
    code = taos_errno(res);
    if (code != 0) {
        log_error("使用数据库失败: %s", taos_errstr(res));
        taos_free_result(res);
        taos_close(taos);
        return 1;
    }
    taos_free_result(res);

    // 删除已存在的表
    res = taos_query(taos, "DROP STABLE IF EXISTS history");
    taos_free_result(res);

    // 创建超级表 - 使用超级表/子表结构
    res = taos_query(taos, "CREATE STABLE IF NOT EXISTS history (ts TIMESTAMP, d FLOAT, r FLOAT, q INT) TAGS (i INT, c INT)");
    code = taos_errno(res);
    if (code != 0) {
        log_error("创建超级表失败: %s", taos_errstr(res));
        taos_free_result(res);
        taos_close(taos);
        return 1;
    }
    taos_free_result(res);

    // 记录程序总开始时间
    auto program_start_time = std::chrono::high_resolution_clock::now();

    log_info("开始准备测试...", "");

    // 第一阶段：预先创建所有子表
    log_info("第一阶段：创建子表...", "");
    auto tables_start_time = std::chrono::high_resolution_clock::now();

    char sql[102400] = {0};  // 使用更大的缓冲区
    int total_tables_created = 0;
    int display_step = points_count / 10 > 0 ? points_count / 10 : 1;  // 每创建10%显示一次进度

    // 创建所有子表，table_id从0到points_count-1，每个子表单独执行创建
    for (int i = 0; i < points_count; i++) {
        std::string table_name = get_table_name(i);
        int data_id = i;  // 使用0到points_count-1的范围
        int category = 1;

        sprintf(sql, "CREATE TABLE IF NOT EXISTS %s USING history TAGS (%d, %d)",
                table_name.c_str(), data_id, category);

        // 单独执行每个子表的创建
        res = taos_query(taos, sql);
        code = taos_errno(res);
        if (code != 0) {
            log_error("创建子表%s失败: %s", table_name.c_str(), taos_errstr(res));
            taos_free_result(res);
            continue;
        }
        taos_free_result(res);

        total_tables_created++;

        // 每创建一定数量的子表打印一次进度，避免长时间无输出
        if (i % display_step == 0 || i == points_count - 1) {
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = current_time - tables_start_time;
            double create_rate = total_tables_created / elapsed.count();
            log_info("子表创建进度: %.1f%%，已创建 %d 个子表，耗时 %.2f 秒，创建速率 %.2f 个/秒",
                     (i + 1) * 100.0 / points_count,
                     total_tables_created,
                     elapsed.count(),
                     create_rate);
        }
    }

    // 子表创建完成，计算耗时
    auto tables_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> tables_elapsed = tables_end_time - tables_start_time;
    log_info("子表创建完成，共创建 %d 个子表，耗时 %.2f 秒",
             total_tables_created, tables_elapsed.count());

    // 第二阶段：准备所有要写入的数据
    log_info("第二阶段：准备写入数据...", "");
    auto prepare_start_time = std::chrono::high_resolution_clock::now();

    // 创建待写入数据集合，按表ID分组
    std::map<int, std::vector<InsertData>> table_data_map;

    // 获取基准时间戳，确保所有记录时间递增
    auto base_time = std::chrono::system_clock::now();

    // 准备所有要写入的数据
    for (int r = 0; r < repeat_times; r++) {
        for (int p = 0; p < points_count; p++) {
            InsertData data;
            data.table_id = p;
            data.d_val = (r * points_count + p) * 0.1f;
            data.r_val = (r * points_count + p) * 0.05f;
            data.q_val = (r * points_count + p) * 10;

            // 为每条记录创建一个微秒级递增时间戳，确保唯一性
            auto record_time = base_time + std::chrono::microseconds(r * points_count + p);
            auto time_t_value = std::chrono::system_clock::to_time_t(record_time);
            auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                              record_time.time_since_epoch()).count() % 1000000;

            char buffer[32];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t_value));

            char timestamp[48];
            sprintf(timestamp, "%s.%06ld", buffer, micros);
            data.timestamp = timestamp;

            // 将数据按表ID分组存储
            table_data_map[data.table_id].push_back(data);
        }
    }

    auto prepare_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> prepare_elapsed = prepare_end_time - prepare_start_time;
    log_info("数据准备完成，共准备 %d 个测点的数据，每个测点 %d 条，共 %d 条记录，耗时 %.2f 秒",
             table_data_map.size(), repeat_times, total_records, prepare_elapsed.count());

    // 第三阶段：批量写入数据（开始正式计时）
    log_info("第三阶段：开始写入数据（计时开始）...", "");
    auto data_start_time = std::chrono::high_resolution_clock::now();
    int total_written = 0;
    int total_batches = 0;

    // 按照批次写入数据，每个批次包含多个表的数据
    std::vector<int> table_ids;
    for (const auto &pair : table_data_map) {
        table_ids.push_back(pair.first);
    }

    // 计算需要多少批次才能写入所有表的数据
    int total_batch_count = (table_ids.size() + tables_per_batch - 1) / tables_per_batch;

    // 按批次处理所有表
    for (int batch = 0; batch < total_batch_count; batch++) {
        // 确定当前批次的表范围
        int start_idx = batch * tables_per_batch;
        int end_idx = std::min(start_idx + tables_per_batch, (int)table_ids.size());

        // 构造批量插入SQL语句
        std::stringstream ss;
        ss << "INSERT INTO ";

        int batch_record_count = 0;
        bool first_table = true;

        // 为每个表构造VALUES子句
        for (int i = start_idx; i < end_idx; i++) {
            int table_id = table_ids[i];
            const std::vector<InsertData> &records = table_data_map[table_id];
            if (records.empty()) {
                continue;
            }

            if (!first_table) {
                ss << " ";
            }

            std::string table_name = get_table_name(table_id);
            ss << table_name << " VALUES ";

            bool first_record = true;
            for (const InsertData &data : records) {
                if (!first_record) {
                    ss << ", ";
                }

                ss << "(\"" << data.timestamp << "\", "
                   << data.d_val << ", "
                   << data.r_val << ", "
                   << data.q_val << ")";

                first_record = false;
                batch_record_count++;
            }

            first_table = false;
        }

        // 如果没有数据要写入，跳过这个批次
        if (batch_record_count == 0) {
            continue;
        }

        std::string batch_sql = ss.str();

        // 执行批量插入
        res = taos_query(taos, batch_sql.c_str());
        code = taos_errno(res);
        if (code != 0) {
            log_error("批量插入数据失败(批次%d): %s", batch, taos_errstr(res));
            taos_free_result(res);
        } else {
            taos_free_result(res);
            total_written += batch_record_count;
        }

        total_batches++;

        // 每完成一定比例的批次输出一次进度
        if (batch % (total_batch_count / 10 + 1) == 0 || batch == total_batch_count - 1) {
            auto current_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = current_time - data_start_time;
            double write_rate = total_written / elapsed.count();
            double progress = (batch + 1) * 100.0 / total_batch_count;

            log_info("写入进度: %.1f%%，已处理 %d/%d 批次，已写入 %d 条记录，耗时 %.2f 秒，写入速率 %.2f 条/秒",
                     progress, batch + 1, total_batch_count, total_written, elapsed.count(), write_rate);
        }
    }

    // 计算数据写入耗时
    auto data_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> data_elapsed = data_end_time - data_start_time;

    // 计算总耗时
    std::chrono::duration<double> total_elapsed = data_end_time - program_start_time;

    // 输出性能统计结果
    log_info("写入测试完成，各阶段耗时统计:", "");
    log_info("子表创建阶段: %.2f 秒", tables_elapsed.count());
    log_info("数据准备阶段: %.2f 秒", prepare_elapsed.count());
    log_info("数据写入阶段: %.2f 秒", data_elapsed.count());
    log_info("总耗时: %.2f 秒", total_elapsed.count());
    log_info("测点数量: %d", points_count);
    log_info("每个测点重复写入: %d 次", repeat_times);
    log_info("每批处理表数: %d", tables_per_batch);
    log_info("总批次数: %d", total_batches);
    log_info("总写入记录数: %d 条", total_written);
    log_info("平均写入速率: %.2f 条/秒", total_written / data_elapsed.count());
    log_info("每批次平均写入: %.2f 条", (double)total_written / total_batches);

    // 关闭连接
    taos_close(taos);
    return 0;
}
