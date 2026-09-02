#!/bin/sh

g++ -o ftpd main.cc -O2 -lyaml-cpp -std=c++17 -pthread -Wc++20-extensions
