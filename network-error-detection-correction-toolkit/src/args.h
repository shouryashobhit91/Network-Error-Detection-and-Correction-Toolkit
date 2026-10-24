// args.h - tiny command line parser: --key value, --flag, positional arguments
#pragma once
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct Args {
    std::map<std::string, std::string> kv;
    std::vector<std::string> pos;
    Args(int argc, char **argv) {
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            if (a.rfind("--", 0) == 0)
                kv[a.substr(2)] = (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2)) ? argv[++i] : "1";
            else
                pos.push_back(a);
        }
    }
    bool has(const std::string &k) const { return kv.count(k) > 0; }
    std::string get(const std::string &k, const std::string &d = "") const { return has(k) ? kv.at(k) : d; }
    double num(const std::string &k, double d) const { return has(k) ? std::atof(kv.at(k).c_str()) : d; }
};
