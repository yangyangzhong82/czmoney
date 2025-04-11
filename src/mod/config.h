#pragma once

#include <string>

namespace my_mod {

struct Config {
    int version = 1; // 配置文件版本号

    std::string db_host     = "127.0.0.1";
    std::string db_user     = "your_username";
    std::string db_password = "your_password";
    std::string db_name     = "your_database";
    unsigned int db_port    = 3306;

};

} // namespace my_mod
