#pragma once

// ── HTTP Server for GTO Poker Solver ────────────────────────────────────
//
// Wraps the poker_solver public API behind a JSON REST interface.
// Serves static frontend files from a configurable directory.

#include "poker_solver/solver.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace poker_solver {

// ── Async job tracking ──────────────────────────────────────────────────

enum class JobStatus { Running, Completed, Failed, Cancelled };

struct SolveJob {
    std::string id;
    JobStatus   status = JobStatus::Running;
    std::string error;
    int         progress = 0;       // 0-100
    int         iteration = 0;
    int         total_iterations = 0;
    std::thread thread;

    // Result (for exploit jobs).
    ExploitResult exploit_result{};

    // Cancellation flag.
    std::atomic<bool> cancel_requested{false};
};

// ── HttpServer ──────────────────────────────────────────────────────────

class HttpServer {
public:
    HttpServer(int port, const std::string& static_dir);
    ~HttpServer();

    // Blocking — runs the server until stopped.
    void run();
    void stop();

private:
    int         port_;
    std::string static_dir_;

    // Active jobs.
    std::mutex                                  jobs_mu_;
    std::map<std::string, std::shared_ptr<SolveJob>> jobs_;

    std::string newJobId();

    // Implementation detail — the httplib server is constructed in run().
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace poker_solver
