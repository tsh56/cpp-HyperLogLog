#include <igloo/igloo_alt.h>
#include <igloo/TapTestListener.h>
#include "hyperloglog.hpp"
#include <map>
#include <string>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>
using namespace igloo;
using namespace hll;

namespace {
// Test utilities

static const char alphanum[] = "0123456789"
        "!@#$%^&*"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

static const int alphanumSize = sizeof(alphanum) - 1;

static void genRandomString(size_t len, std::string& str) {
    srand(time(0));
    for (size_t i = 0; i < len; ++i) {
        char c = alphanum[rand() % alphanumSize];
        str.append(&c, 1);
    }
}

static std::map<std::string, bool> GEN_STRINGS;
static void getUniqueString(size_t len, std::string& str) {
    do {
        genRandomString(len, str);
    } while (GEN_STRINGS.find(str) != GEN_STRINGS.end());
    GEN_STRINGS.insert(std::make_pair(str, true));
}

}

class ScopedFile {
public:
    ScopedFile(std::string& filename) : filename_(filename) {
    }

    ~ScopedFile() {
        remove(filename_.c_str());   
    }

    const std::string& getFileName() const {
        return filename_;
    }
private:
    std::string filename_;
};

Describe(hll_HyperLogLogHIP) {
    Describe(create_instance) {
        It(pass_minimum_arugment_in_range) {
            HyperLogLogHIP *hll = new HyperLogLogHIP(4);
            Assert::That(hll != NULL);
            delete hll;
        }

        It(pass_maximum_argument_in_range) {
            HyperLogLogHIP *hll = new HyperLogLogHIP(16);
            Assert::That(hll != NULL);
            delete hll;
        }

        It(pass_out_of_range_argument_min) {
            AssertThrows(std::invalid_argument, HyperLogLogHIP(3));
            Assert::That(LastException<std::invalid_argument>().what(),
                    Is().Containing("bit width must be in the range [4,30]"));
        }

        It(pass_out_of_range_argument_max) {
            AssertThrows(std::invalid_argument, HyperLogLogHIP(31));
            Assert::That(LastException<std::invalid_argument>().what(),
                    Is().Containing("bit width must be in the range [4,30]"));
        }
    };

    It(get_register_size) {
        HyperLogLogHIP *hll = new HyperLogLogHIP(10);
        Assert::That(hll->registerSize(), Equals(1UL << 10));
        delete hll;

        hll = new HyperLogLogHIP(16);
        Assert::That(hll->registerSize(), Equals(1UL << 16));
        delete hll;
    }

    It(estimate_cardinality) {
        uint32_t k = 16;
        uint32_t registerSize = 1UL << k;
        double expectRatio = sqrt(2.0 / M_PI * ((double)registerSize - 2.0));
        double error = 0.0;
        size_t dataNum = (size_t(1) << 16) + size_t(1);
        size_t execNum = 10;
        for (size_t n = 0; n < execNum; ++n) {
            HyperLogLogHIP hll(k);
            std::map<std::string, bool>().swap(GEN_STRINGS);
            for (size_t i = 1; i < dataNum; ++i) {
                std::string str((const char*)&i, sizeof(i));
                hll.add(str.c_str(), str.size());
            }
            double cardinality = hll.estimate();
            error += std::abs(cardinality - dataNum) / dataNum;
        }
        double errorRatio = error / execNum;
        Assert::That(errorRatio, IsLessThan(expectRatio));
    }

    It(dump_and_restore) {
        uint32_t k = 16;
        size_t dataNum = 500;
        HyperLogLogHIP hll(k);
        for (size_t i = 1; i < dataNum; ++i) {
            std::string str;
            getUniqueString((i % 100) + 10, str);
            hll.add(str.c_str(), str.size());
        }
        double cardinality = hll.estimate();
        {
            std::string dumpFile = "./t/hll_test.dump";
            ScopedFile sf(dumpFile);
            std::ofstream ofs(dumpFile.c_str());
            hll.dump(ofs);
            ofs.close();

            std::ifstream ifs(dumpFile.c_str());
            HyperLogLogHIP hll2;
            hll2.restore(ifs);
            ifs.close();
            Assert::That(hll2.estimate(), Equals(cardinality));
        }
    }

    It(clear_register) {
        HyperLogLogHIP hll(16);
        size_t dataNum = 100;
        for (size_t i = 1; i < dataNum; ++i) {
            std::string str;
            getUniqueString(i + 10, str);
            hll.add(str.c_str(), str.size());
        }
        Assert::That(hll.estimate(), !Equals(0.0f));
        hll.clear();
        Assert::That(hll.estimate(), Equals(0.0f));
    }
    
    Describe(merge) {
        It(merge_registers) {
            uint32_t k = 16;
            uint32_t registerSize = 1UL << k;
            double expectRatio = sqrt(2.0 / M_PI * ((double)registerSize - 2.0));
            size_t dataNum = 1 << 10;
            size_t dataNum2 = 1 << 10;
            size_t execNum = 10;
            double error = 0.0;
            for (size_t i = 0; i < execNum; ++i) {
                HyperLogLogHIP hll(k);
                std::map<std::string, bool>().swap(GEN_STRINGS);
                for (size_t i = 1; i < dataNum; ++i) {
                    std::string str;
                    getUniqueString((i % 100) + 10, str);
                    hll.add(str.c_str(), str.size());
                }

                HyperLogLogHIP hll2(k);

                for (size_t i = 1; i < dataNum2; ++i) {
                    std::string str;
                    getUniqueString((i % 100) + 10, str);
                    hll2.add(str.c_str(), str.size());
                }

                hll.merge(hll2);
                double cardinality = hll.estimate();
                error += std::abs(cardinality - (double)(dataNum + dataNum2))/(dataNum + dataNum2);
            }
            double errorRatio = error / execNum;
            Assert::That(errorRatio, IsLessThan(expectRatio));
        }

        It(merge_size_unmatched_registers) {
            HyperLogLogHIP hll(16);
            HyperLogLogHIP hll2(10);
            AssertThrows(std::invalid_argument, hll.merge(hll2));
            Assert::That(LastException<std::invalid_argument>().what(),
                    Is().Containing("number of registers doesn't match:"));
        }
    };
};

int main() {
    DefaultTestResultsOutput output;
    TestRunner runner(output);

    TapTestListener listener;
    runner.AddListener(&listener);

    return runner.Run();
}

