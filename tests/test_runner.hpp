//makes sure this file is only read once
#pragma once

#include <iostream>
#include <string>

//a small test helper used instead of assert, which is compiled out in release builds
struct TestRunner {
    int checks_run = 0;
    int checks_failed = 0;

    //records one check and prints it if it fails
    void check(bool passed, const std::string& description) {
        checks_run++;

        if (!passed) {
            checks_failed++;
            std::cout << "  failed: " << description << "\n";
        }
    }

    //prints how the suite did and returns the exit code for main
    int summary(const std::string& suite_name) const {
        std::cout << suite_name << ": "
                  << (checks_run - checks_failed) << "/" << checks_run
                  << " checks passed\n";

        if (checks_failed > 0) {
            return 1;
        }

        return 0;
    }
};
