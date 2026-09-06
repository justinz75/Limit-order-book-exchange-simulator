//makes sure this file is only read once
#pragma once

#include <iostream>
#include <string>

//a small stand in for a test framework. these tests used assert before, but assert is compiled out when
//NDEBUG is defined, so a release build would report that everything passed without checking anything
struct TestRunner {
    int checks_run = 0;
    int checks_failed = 0;

    //records the result of one check, and prints the ones that fail rather than stopping at the first
    void check(bool passed, const std::string& description) {
        checks_run++;

        if (!passed) {
            checks_failed++;
            std::cout << "  failed: " << description << "\n";
        }
    }

    //prints how the suite did and returns the exit code main should hand back
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
