// Replay-pipeline benchmark harness (docs/10_REPLAY_PERFORMANCE_BENCHMARK.md).
// Measures the production pipeline used by Replay mode's Core path:
//   file read -> parseReplayLog(text) -> analyzeReplayLog(log) [incl. statistics]
// plus an analyze-only phase for attribution. Single-threaded, Release core.
//
//   build:  g++ -std=c++20 -O3 -I../../src bench.cpp ../../build/release/libmodbuslens_core.a -o bench.exe
//   run:    bench.exe <sample.mlog> <iterations>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "core/replay/ReplayAnalysis.h"
#include "core/replay/ReplayLog.h"

using namespace modbuslens::core;

static std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: bench.exe <sample.mlog> <iterations>\n");
        return 2;
    }
    const std::string path = argv[1];
    const int iters = std::atoi(argv[2]);

    // --- validation pass (also warms the code and OS file cache) ---
    const std::string text = readFile(path);
    auto warm = parseReplayLog(text);
    if (!std::holds_alternative<ReplayLog>(warm)) {
        std::printf("parse failed\n");
        return 1;
    }
    const auto& log = std::get<ReplayLog>(warm);
    auto wr = analyzeReplayLog(log);
    if (!std::holds_alternative<ReplayBatchAnalysis>(wr)) {
        std::printf("analyze failed\n");
        return 1;
    }
    const auto& batch = std::get<ReplayBatchAnalysis>(wr);
    std::printf("# records=%zu analyzed=%zu unsupported=%zu stats.success=%d\n",
                log.transactions.size(), batch.transactions.size(),
                batch.unsupportedRecords.size(), batch.statistics.successCount);

    struct Rec {
        double read, parse, analyze, total, analyzeOnly;
    };
    std::vector<Rec> rs;
    rs.reserve(iters);

    for (int i = 0; i < iters; ++i) {
        // ---- production pipeline: read + parse + analyze ----
        auto t0 = std::chrono::steady_clock::now();
        const std::string t2 = readFile(path);
        auto t1 = std::chrono::steady_clock::now();
        auto pr = parseReplayLog(t2);
        auto t2t = std::chrono::steady_clock::now();
        auto ar = analyzeReplayLog(std::get<ReplayLog>(pr));
        auto t3 = std::chrono::steady_clock::now();
        // ---- analyze-only, reusing the pre-parsed model ----
        auto ar2 = analyzeReplayLog(log);
        auto t4 = std::chrono::steady_clock::now();

        Rec r{};
        r.read = std::chrono::duration<double, std::milli>(t1 - t0).count();
        r.parse = std::chrono::duration<double, std::milli>(t2t - t1).count();
        r.analyze = std::chrono::duration<double, std::milli>(t3 - t2t).count();
        r.total = std::chrono::duration<double, std::milli>(t3 - t0).count();
        r.analyzeOnly = std::chrono::duration<double, std::milli>(t4 - t3).count();
        rs.push_back(r);
        std::printf("iter %2d: read %7.2f | parse %7.2f | analyze %7.2f | total %7.2f ms\n",
                    i, r.read, r.parse, r.analyze, r.total);
    }

    auto med = [&](auto proj) {
        std::vector<double> v;
        v.reserve(rs.size());
        for (const auto& r : rs) v.push_back(proj(r));
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    const double n = static_cast<double>(log.transactions.size());
    std::printf("---- medians (n=%d iters) ----\n", iters);
    std::printf("read          : %9.2f ms\n", med([](const Rec& r) { return r.read; }));
    std::printf("parse         : %9.2f ms\n", med([](const Rec& r) { return r.parse; }));
    std::printf("analyze       : %9.2f ms\n", med([](const Rec& r) { return r.analyze; }));
    std::printf("total         : %9.2f ms\n", med([](const Rec& r) { return r.total; }));
    // parse+analyze (pipeline without disk IO), per-iteration median
    const double coreMed = med([](const Rec& r) { return r.parse + r.analyze; });
    std::printf("parse+analyze : %9.2f ms  ->  %.2f M records/s, %.1f ns/record\n",
                coreMed, n / coreMed / 1000.0, coreMed * 1e6 / n);
    std::printf("analyze-only  : %9.2f ms (pre-parsed model, attribution only)\n",
                med([](const Rec& r) { return r.analyzeOnly; }));
    return 0;
}