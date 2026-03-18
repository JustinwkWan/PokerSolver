// http_server.cpp — REST API server wrapping the poker_solver public API.
//
// All solver work is delegated to the functions in poker_solver/solver.h.
// This file only handles HTTP routing, JSON serialization, and job management.

#include "http_server.h"
#include "poker_solver/solver.h"
#include "abstraction/action_abstraction.h"
#include "tree/game_tree.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>

using json = nlohmann::json;

namespace poker_solver {

// ── Helpers ──────────────────────────────────────────────────────────────

static SolverConfig configFromJson(const json& j) {
    SolverConfig cfg;
    if (j.contains("stack_depth_bb"))   cfg.stack_depth_bb   = j["stack_depth_bb"];
    if (j.contains("num_iterations"))   cfg.num_iterations   = j["num_iterations"];
    if (j.contains("output_path"))      cfg.output_path      = j["output_path"];
    if (j.contains("preflop_sizes"))    cfg.preflop_sizes    = j["preflop_sizes"].get<std::vector<double>>();
    if (j.contains("flop_sizes"))       cfg.flop_sizes       = j["flop_sizes"].get<std::vector<double>>();
    if (j.contains("turn_sizes"))       cfg.turn_sizes       = j["turn_sizes"].get<std::vector<double>>();
    if (j.contains("river_sizes"))      cfg.river_sizes      = j["river_sizes"].get<std::vector<double>>();
    if (j.contains("dcfr_alpha"))       cfg.dcfr_alpha       = j["dcfr_alpha"];
    if (j.contains("dcfr_beta"))        cfg.dcfr_beta        = j["dcfr_beta"];
    if (j.contains("dcfr_gamma"))       cfg.dcfr_gamma       = j["dcfr_gamma"];
    if (j.contains("checkpoint_every")) cfg.checkpoint_every = j["checkpoint_every"];
    if (j.contains("seed"))             cfg.seed             = j["seed"];
    if (j.contains("buckets")) {
        auto b = j["buckets"].get<std::vector<int>>();
        for (int i = 0; i < 4 && i < (int)b.size(); ++i)
            cfg.buckets[i] = b[i];
    }
    return cfg;
}

static json strategyToJson(const StrategyResult& r) {
    json j;
    j["actions"] = r.actions;
    j["probabilities"] = r.probabilities;
    return j;
}

static void jsonResponse(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

static void errorResponse(httplib::Response& res, const std::string& msg, int status = 400) {
    jsonResponse(res, {{"error", msg}}, status);
}

// ── Impl ─────────────────────────────────────────────────────────────────

struct HttpServer::Impl {
    httplib::Server svr;
};

HttpServer::HttpServer(int port, const std::string& static_dir)
    : port_(port), static_dir_(static_dir), impl_(std::make_unique<Impl>()) {}

HttpServer::~HttpServer() {
    stop();
    // Join any lingering job threads.
    std::lock_guard<std::mutex> lk(jobs_mu_);
    for (auto& [id, job] : jobs_) {
        job->cancel_requested = true;
        if (job->thread.joinable()) job->thread.join();
    }
}

std::string HttpServer::newJobId() {
    static std::mt19937_64 rng(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream ss;
    ss << std::hex << dist(rng);
    return ss.str();
}

void HttpServer::stop() {
    if (impl_) impl_->svr.stop();
}

void HttpServer::run() {
    auto& svr = impl_->svr;

    // ── CORS middleware ──────────────────────────────────────────────────
    svr.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // ── POST /api/solve — start async solve ─────────────────────────────
    svr.Post("/api/solve", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { errorResponse(res, "invalid JSON"); return; }

        auto cfg = configFromJson(body);
        auto job = std::make_shared<SolveJob>();
        job->id = newJobId();
        job->total_iterations = cfg.num_iterations;
        job->status = JobStatus::Running;

        {
            std::lock_guard<std::mutex> lk(jobs_mu_);
            jobs_[job->id] = job;
        }

        // Run solve in background thread.
        job->thread = std::thread([job, cfg]() {
            try {
                // Use a modified solve that updates progress.
                // For now, delegate to the public API directly.
                poker_solver::solve(cfg);
                job->iteration = cfg.num_iterations;
                job->progress = 100;
                job->status = JobStatus::Completed;
            } catch (const std::exception& e) {
                job->error = e.what();
                job->status = JobStatus::Failed;
            }
        });

        jsonResponse(res, {{"job_id", job->id}});
    });

    // ── GET /api/solve/:id/status ───────────────────────────────────────
    svr.Get(R"(/api/solve/([a-f0-9]+)/status)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        std::lock_guard<std::mutex> lk(jobs_mu_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            errorResponse(res, "job not found", 404);
            return;
        }
        auto& job = it->second;
        json j;
        j["job_id"] = job->id;
        j["status"] = job->status == JobStatus::Running   ? "running" :
                       job->status == JobStatus::Completed ? "completed" :
                       job->status == JobStatus::Failed    ? "failed" :
                       "cancelled";
        j["progress"] = job->progress;
        j["iteration"] = job->iteration;
        j["total_iterations"] = job->total_iterations;
        if (!job->error.empty()) j["error"] = job->error;
        jsonResponse(res, j);
    });

    // ── POST /api/solve/:id/cancel ──────────────────────────────────────
    svr.Post(R"(/api/solve/([a-f0-9]+)/cancel)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        std::lock_guard<std::mutex> lk(jobs_mu_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            errorResponse(res, "job not found", 404);
            return;
        }
        it->second->cancel_requested = true;
        it->second->status = JobStatus::Cancelled;
        jsonResponse(res, {{"cancelled", true}});
    });

    // ── POST /api/query — query strategy for one hand ───────────────────
    svr.Post("/api/query", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { errorResponse(res, "invalid JSON"); return; }

        try {
            auto cfg = configFromJson(body);
            std::string strategy_path = body.value("strategy_path", cfg.output_path);
            std::string hand    = body.at("hand");
            std::string board   = body.value("board", "");
            std::string history = body.value("history", "");

            auto result = poker_solver::query(strategy_path, hand, board, history, cfg);
            jsonResponse(res, strategyToJson(result));
        } catch (const std::exception& e) {
            errorResponse(res, e.what());
        }
    });

    // ── POST /api/query/range — query all 1326 combos ───────────────────
    svr.Post("/api/query/range", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { errorResponse(res, "invalid JSON"); return; }

        try {
            auto cfg = configFromJson(body);
            std::string strategy_path = body.value("strategy_path", cfg.output_path);
            std::string board   = body.value("board", "");
            std::string history = body.value("history", "");

            // Enumerate all 1326 unique combos.
            static const char* RANKS = "23456789TJQKA";
            static const char* SUITS = "cdhs";
            json combos = json::array();

            for (int c1 = 0; c1 < 52; ++c1) {
                for (int c2 = c1 + 1; c2 < 52; ++c2) {
                    std::string hand;
                    hand += RANKS[c1 / 4];
                    hand += SUITS[c1 % 4];
                    hand += RANKS[c2 / 4];
                    hand += SUITS[c2 % 4];

                    try {
                        auto result = poker_solver::query(strategy_path, hand, board, history, cfg);
                        if (!result.actions.empty()) {
                            json combo;
                            combo["hand"] = hand;
                            combo["actions"] = result.actions;
                            combo["probabilities"] = result.probabilities;
                            combos.push_back(combo);
                        }
                    } catch (...) {
                        // Skip invalid combos (e.g., board card conflicts).
                    }
                }
            }

            jsonResponse(res, {{"combos", combos}});
        } catch (const std::exception& e) {
            errorResponse(res, e.what());
        }
    });

    // ── POST /api/subgame — async subgame solve ─────────────────────────
    svr.Post("/api/subgame", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { errorResponse(res, "invalid JSON"); return; }

        try {
            auto cfg = configFromJson(body);
            std::string hand       = body.at("hand");
            std::string board      = body.at("board");
            std::string history    = body.value("history", "");
            int iterations         = body.value("iterations", 10000);

            auto job = std::make_shared<SolveJob>();
            job->id = newJobId();
            job->total_iterations = iterations;
            job->status = JobStatus::Running;

            {
                std::lock_guard<std::mutex> lk(jobs_mu_);
                jobs_[job->id] = job;
            }

            job->thread = std::thread([job, hand, board, history, iterations, cfg]() {
                try {
                    poker_solver::solve_subgame(hand, board, history, iterations, cfg);
                    job->progress = 100;
                    job->status = JobStatus::Completed;
                } catch (const std::exception& e) {
                    job->error = e.what();
                    job->status = JobStatus::Failed;
                }
            });

            jsonResponse(res, {{"job_id", job->id}});
        } catch (const std::exception& e) {
            errorResponse(res, e.what());
        }
    });

    // ── POST /api/exploit — async exploitability measurement ────────────
    svr.Post("/api/exploit", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { errorResponse(res, "invalid JSON"); return; }

        try {
            auto cfg = configFromJson(body);
            std::string strategy_path = body.value("strategy_path", cfg.output_path);
            int samples = body.value("samples", 100000);

            auto job = std::make_shared<SolveJob>();
            job->id = newJobId();
            job->total_iterations = samples;
            job->status = JobStatus::Running;

            {
                std::lock_guard<std::mutex> lk(jobs_mu_);
                jobs_[job->id] = job;
            }

            job->thread = std::thread([job, strategy_path, samples, cfg]() {
                try {
                    auto r = poker_solver::measure_exploitability(strategy_path, samples, cfg);
                    job->exploit_result = r;
                    job->progress = 100;
                    job->status = JobStatus::Completed;
                } catch (const std::exception& e) {
                    job->error = e.what();
                    job->status = JobStatus::Failed;
                }
            });

            jsonResponse(res, {{"job_id", job->id}});
        } catch (const std::exception& e) {
            errorResponse(res, e.what());
        }
    });

    // ── GET /api/tree — get tree structure ──────────────────────────────
    svr.Get("/api/tree", [](const httplib::Request& req, httplib::Response& res) {
        try {
            int stack = 100;
            if (req.has_param("stack")) stack = std::stoi(req.get_param_value("stack"));

            SolverConfig cfg;
            cfg.stack_depth_bb = stack;
            int stack_chips = cfg.stack_depth_bb * 2;

            ActionAbstraction aa(BetSizeConfig{
                cfg.preflop_sizes, cfg.flop_sizes, cfg.turn_sizes, cfg.river_sizes});
            GameTree tree(stack_chips, aa);
            tree.build();

            json j;
            j["num_nodes"] = tree.numNodes();
            j["num_action_nodes"] = tree.numActionNodes();
            j["num_terminal_nodes"] = tree.numTerminalNodes();

            // Return root info.
            const auto& root = tree.root();
            j["root"]["type"] = root.type == NodeType::Action ? "action" : "terminal";
            j["root"]["player"] = root.player;
            j["root"]["street"] = root.street;
            j["root"]["num_actions"] = root.num_actions;

            json acts = json::array();
            for (int a = 0; a < root.num_actions; ++a) {
                const auto& act = tree.action(0, a);
                json aj;
                aj["type"] = act.type == ActionType::Fold     ? "fold" :
                             act.type == ActionType::Check    ? "check" :
                             act.type == ActionType::Call     ? "call" :
                             "raise";
                aj["amount"] = act.amount;
                aj["child_index"] = tree.child(0, a);
                acts.push_back(aj);
            }
            j["root"]["actions"] = acts;

            jsonResponse(res, j);
        } catch (const std::exception& e) {
            errorResponse(res, e.what(), 500);
        }
    });

    // ── GET /api/tree/:node — get node details ──────────────────────────
    svr.Get(R"(/api/tree/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        try {
            uint32_t node_idx = std::stoul(req.matches[1]);
            int stack = 100;
            if (req.has_param("stack")) stack = std::stoi(req.get_param_value("stack"));

            SolverConfig cfg;
            cfg.stack_depth_bb = stack;
            int stack_chips = cfg.stack_depth_bb * 2;

            ActionAbstraction aa(BetSizeConfig{
                cfg.preflop_sizes, cfg.flop_sizes, cfg.turn_sizes, cfg.river_sizes});
            GameTree tree(stack_chips, aa);
            tree.build();

            if (node_idx >= tree.numNodes()) {
                errorResponse(res, "node index out of range", 404);
                return;
            }

            const auto& nd = tree.node(node_idx);
            json j;
            j["index"] = node_idx;
            j["type"] = nd.type == NodeType::Action ? "action" : "terminal";
            j["player"] = nd.player;
            j["street"] = nd.street;
            j["num_actions"] = nd.num_actions;
            j["pot"] = nd.pot;
            j["fold_player"] = nd.fold_player;

            if (nd.type == NodeType::Action) {
                json acts = json::array();
                for (int a = 0; a < nd.num_actions; ++a) {
                    const auto& act = tree.action(node_idx, a);
                    json aj;
                    aj["type"] = act.type == ActionType::Fold     ? "fold" :
                                 act.type == ActionType::Check    ? "check" :
                                 act.type == ActionType::Call     ? "call" :
                                 "raise";
                    aj["amount"] = act.amount;
                    aj["child_index"] = tree.child(node_idx, a);
                    acts.push_back(aj);
                }
                j["actions"] = acts;
            }

            jsonResponse(res, j);
        } catch (const std::exception& e) {
            errorResponse(res, e.what(), 500);
        }
    });

    // ── POST /api/strategy/status — check which strategy dirs exist ────
    svr.Post("/api/strategy/status", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { errorResponse(res, "invalid JSON"); return; }

        try {
            auto paths = body.at("paths").get<std::vector<std::string>>();
            json result = json::object();
            for (const auto& p : paths) {
                bool exists = false;
                int iterations = 0;
                // Check for meta.txt which is written after a solve completes.
                std::string meta_path = p + "/meta.txt";
                if (std::filesystem::exists(meta_path)) {
                    exists = true;
                    std::ifstream meta(meta_path);
                    int info_sets = 0, max_actions = 0;
                    if (meta >> info_sets >> max_actions >> iterations) {
                        // ok
                    }
                }
                json entry;
                entry["exists"] = exists;
                entry["iterations"] = iterations;
                result[p] = entry;
            }
            jsonResponse(res, result);
        } catch (const std::exception& e) {
            errorResponse(res, e.what());
        }
    });

    // ── Serve static files ──────────────────────────────────────────────
    if (!static_dir_.empty()) {
        svr.set_mount_point("/", static_dir_);
    }

    // ── SPA fallback: serve index.html for non-API, non-file routes ─────
    svr.set_error_handler([this](const httplib::Request& req, httplib::Response& res) {
        if (res.status == 404 && req.path.substr(0, 4) != "/api" && !static_dir_.empty()) {
            std::ifstream ifs(static_dir_ + "/index.html");
            if (ifs) {
                std::string html((std::istreambuf_iterator<char>(ifs)),
                                  std::istreambuf_iterator<char>());
                res.set_content(html, "text/html");
                res.status = 200;
            }
        }
    });

    std::cout << "Poker Solver UI running at http://localhost:" << port_ << "\n";
    svr.listen("0.0.0.0", port_);
}

}  // namespace poker_solver
