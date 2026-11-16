#!/bin/sh
# Builds and runs the pure C++ core tests (no JUCE needed): sh tests/run.sh
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -Wall -Wextra -o /tmp/scaredy_test test_core.cpp
/tmp/scaredy_test

# Studio 87 Gear Updater link (product id and link string)
(cd .. && g++ -std=c++17 -O2 -Wall -Wextra -o /tmp/s87_link_test tests/test_updater_link.cpp && /tmp/s87_link_test) || exit 1
