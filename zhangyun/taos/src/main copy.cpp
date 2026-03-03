#include "net/logger.h"
#include <taos.h> // 涛思数据库头文件

int main(int argc, char** argv) {
    init_logger(argc, argv);

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

    // 创建表 - 注意这里涛思数据库的超级表语法
    res = taos_query(taos, "CREATE STABLE IF NOT EXISTS history (ts TIMESTAMP, d FLOAT, r FLOAT, q INT) TAGS (i INT, c INT)");
    code = taos_errno(res);
    if (code != 0) {
        log_error("创建表失败: %s", taos_errstr(res));
        taos_free_result(res);
        taos_close(taos);
        return 1;
    }
    taos_free_result(res);

    // 创建100条测试数据
    for (int i = 0; i < 100; i++) {
        int data_id = 10000000 + i; // i值从1000000开始
        int category = 1; // c值固定为1

        // 创建子表
        char sql[256];
        sprintf(sql, "CREATE TABLE IF NOT EXISTS h_%d USING history TAGS (%d, %d)",
                data_id, data_id, category);

        res = taos_query(taos, sql);
        code = taos_errno(res);
        if (code != 0) {
            log_error("创建子表失败: %s", taos_errstr(res));
            taos_free_result(res);
            continue;
        }
        taos_free_result(res);

        // 插入数据
        sprintf(sql, "INSERT INTO h_%d VALUES (NOW, %f, %f, %d)",
                data_id, i * 1.1, i * 0.5, i * 10);

        res = taos_query(taos, sql);
        code = taos_errno(res);
        if (code != 0) {
            log_error("插入数据失败: %s", taos_errstr(res));
            taos_free_result(res);
        } else {
            log_info("成功插入数据到h_%d表，ID=%d", i, data_id);
            taos_free_result(res);
        }
    }

    // 查询数据
    TAOS_RES* result = taos_query(taos, "SELECT * FROM history");
    code = taos_errno(result);
    if (code != 0) {
        log_error("查询失败: %s", taos_errstr(result));
        taos_free_result(result);
    } else {
        TAOS_ROW row;
        int rows = 0;
        while ((row = taos_fetch_row(result))) {
            rows++;
        }
        log_info("共查询到 %d 条记录", rows);
        taos_free_result(result);
    }

    // 关闭连接
    taos_close(taos);
    log_info("涛思数据库测试完成", "");

    return 0;
}
