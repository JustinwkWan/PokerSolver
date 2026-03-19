#include "solver/regret_store.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* kMetaFile = "meta.txt";

// Generate file name for a given player and type (regrets/strategy).
static std::string makeFileName(int player, bool is_strategy) {
    return (is_strategy ? "strategy_p" : "regrets_p") +
           std::to_string(player) + ".bin";
}

// ── Construction (new store) ─────────────────────────────────────────────

RegretStore::RegretStore(uint64_t num_info_sets, int max_actions,
                         const std::string& dir, int num_players)
    : num_info_sets_(num_info_sets), max_actions_(max_actions),
      num_players_(num_players), dir_(dir) {

    assert(num_info_sets > 0);
    assert(max_actions > 0);
    assert(num_players >= 2 && num_players <= kMaxPlayers);

    int num_arrays = num_players * 2;

    if (dir.empty()) {
        // Anonymous mode: heap-allocated, zero-initialized.
        anonymous_ = true;
        size_t n = totalEntries();
        for (int i = 0; i < kMaxPlayers * 2; ++i) {
            regions_[i].addr = nullptr;
            regions_[i].size = 0;
            regions_[i].fd = -1;
        }
        for (int p = 0; p < num_players; ++p) {
            regrets_[p]  = new float[n]();
            strategy_[p] = new float[n]();
        }
    } else {
        // File-backed mode: create directory if needed, then mmap files.
        anonymous_ = false;
        mkdir(dir.c_str(), 0755);  // ignore error if exists
        for (int p = 0; p < num_players; ++p) {
            mapFile(p, dir + "/" + makeFileName(p, false), /*create=*/true);
            mapFile(num_players + p, dir + "/" + makeFileName(p, true), /*create=*/true);
        }
        for (int p = 0; p < num_players; ++p) {
            regrets_[p]  = static_cast<float*>(regions_[p].addr);
            strategy_[p] = static_cast<float*>(regions_[num_players + p].addr);
        }
    }
}

// ── Load existing store ──────────────────────────────────────────────────

RegretStore RegretStore::load(const std::string& dir) {
    // Read metadata.
    std::ifstream meta(dir + "/" + kMetaFile);
    if (!meta)
        throw std::runtime_error("Cannot open " + dir + "/" + kMetaFile);

    uint64_t num_info_sets = 0;
    int max_actions = 0;
    uint64_t iteration = 0;
    int num_players = 2;  // default for backward compat
    meta >> num_info_sets >> max_actions >> iteration;
    // Try to read num_players (may not exist in old files).
    if (meta >> num_players) {
        // Successfully read num_players
    } else {
        num_players = 2;
    }

    if (num_info_sets == 0 || max_actions == 0)
        throw std::runtime_error("Invalid metadata in " + dir + "/" + kMetaFile);

    // Create store and map existing files.
    RegretStore store;
    store.num_info_sets_ = num_info_sets;
    store.max_actions_ = max_actions;
    store.num_players_ = num_players;
    store.anonymous_ = false;
    store.dir_ = dir;

    for (int p = 0; p < num_players; ++p) {
        store.mapFile(p, dir + "/" + makeFileName(p, false), /*create=*/false);
        store.mapFile(num_players + p, dir + "/" + makeFileName(p, true), /*create=*/false);
    }

    for (int p = 0; p < num_players; ++p) {
        store.regrets_[p]  = static_cast<float*>(store.regions_[p].addr);
        store.strategy_[p] = static_cast<float*>(store.regions_[num_players + p].addr);
    }

    return store;
}

// ── Destructor ───────────────────────────────────────────────────────────

RegretStore::~RegretStore() {
    if (anonymous_) {
        for (int p = 0; p < num_players_; ++p) {
            delete[] regrets_[p];
            delete[] strategy_[p];
        }
    } else {
        unmapAll();
    }
}

// ── Move ─────────────────────────────────────────────────────────────────

RegretStore::RegretStore(RegretStore&& other) noexcept
    : num_info_sets_(other.num_info_sets_),
      max_actions_(other.max_actions_),
      num_players_(other.num_players_),
      regrets_(other.regrets_),
      strategy_(other.strategy_),
      regions_(other.regions_),
      anonymous_(other.anonymous_),
      dir_(std::move(other.dir_)) {
    other.num_info_sets_ = 0;
    other.max_actions_ = 0;
    other.num_players_ = 0;
    other.regrets_ = {};
    other.strategy_ = {};
    other.regions_ = {};
}

RegretStore& RegretStore::operator=(RegretStore&& other) noexcept {
    if (this != &other) {
        if (anonymous_) {
            for (int p = 0; p < num_players_; ++p) {
                delete[] regrets_[p];
                delete[] strategy_[p];
            }
        } else {
            unmapAll();
        }
        num_info_sets_ = other.num_info_sets_;
        max_actions_ = other.max_actions_;
        num_players_ = other.num_players_;
        regrets_ = other.regrets_;
        strategy_ = other.strategy_;
        regions_ = other.regions_;
        anonymous_ = other.anonymous_;
        dir_ = std::move(other.dir_);

        other.num_info_sets_ = 0;
        other.max_actions_ = 0;
        other.num_players_ = 0;
        other.regrets_ = {};
        other.strategy_ = {};
        other.regions_ = {};
    }
    return *this;
}

// ── Regret matching ──────────────────────────────────────────────────────

void RegretStore::getStrategy(int player, uint64_t info_set_id, int num_actions,
                               float* out) const {
    const float* r = regretSlice(player, info_set_id);
    float sum = 0.0f;
    for (int a = 0; a < num_actions; ++a) {
        float pos = std::max(r[a], 0.0f);
        out[a] = pos;
        sum += pos;
    }
    if (sum > 0.0f) {
        float inv = 1.0f / sum;
        for (int a = 0; a < num_actions; ++a)
            out[a] *= inv;
    } else {
        float uniform = 1.0f / num_actions;
        for (int a = 0; a < num_actions; ++a)
            out[a] = uniform;
    }
}

void RegretStore::getAverageStrategy(int player, uint64_t info_set_id,
                                      int num_actions, float* out) const {
    const float* s = strategySlice(player, info_set_id);
    float sum = 0.0f;
    for (int a = 0; a < num_actions; ++a)
        sum += s[a];
    if (sum > 0.0f) {
        float inv = 1.0f / sum;
        for (int a = 0; a < num_actions; ++a)
            out[a] = s[a] * inv;
    } else {
        float uniform = 1.0f / num_actions;
        for (int a = 0; a < num_actions; ++a)
            out[a] = uniform;
    }
}

// ── Persistence ──────────────────────────────────────────────────────────

void RegretStore::sync(bool blocking) {
    if (anonymous_) return;
    int flags = blocking ? MS_SYNC : MS_ASYNC;
    int num_arrays = num_players_ * 2;
    for (int i = 0; i < num_arrays; ++i) {
        if (regions_[i].addr)
            msync(regions_[i].addr, regions_[i].size, flags);
    }
}

void RegretStore::checkpoint(const std::string& dir, uint64_t iteration) const {
    mkdir(dir.c_str(), 0755);  // ignore error if exists
    if (!anonymous_) {
        int num_arrays = num_players_ * 2;
        for (int i = 0; i < num_arrays; ++i) {
            if (regions_[i].addr)
                msync(regions_[i].addr, regions_[i].size, MS_SYNC);
        }
    }

    // Write metadata.
    std::string meta_path = dir + "/" + kMetaFile;
    std::ofstream meta(meta_path);
    if (!meta)
        throw std::runtime_error("Cannot write " + meta_path);
    meta << num_info_sets_ << " " << max_actions_ << " "
         << iteration << " " << num_players_ << "\n";
    meta.close();

    // If anonymous, dump arrays to files.
    if (anonymous_) {
        size_t bytes = totalEntries() * sizeof(float);
        for (int p = 0; p < num_players_; ++p) {
            // Regrets
            std::string path = dir + "/" + makeFileName(p, false);
            std::ofstream ofs(path, std::ios::binary);
            if (!ofs)
                throw std::runtime_error("Cannot write " + path);
            ofs.write(reinterpret_cast<const char*>(regrets_[p]), bytes);

            // Strategy
            path = dir + "/" + makeFileName(p, true);
            ofs = std::ofstream(path, std::ios::binary);
            if (!ofs)
                throw std::runtime_error("Cannot write " + path);
            ofs.write(reinterpret_cast<const char*>(strategy_[p]), bytes);
        }
    }
}

// ── mmap helpers ─────────────────────────────────────────────────────────

void RegretStore::mapFile(int idx, const std::string& path, bool create) {
    size_t bytes = totalEntries() * sizeof(float);

    int flags = O_RDWR;
    if (create) flags |= O_CREAT | O_TRUNC;

    int fd = open(path.c_str(), flags, 0644);
    if (fd < 0)
        throw std::runtime_error("Cannot open " + path + ": " + strerror(errno));

    if (create) {
        // Extend file to the required size.
        if (ftruncate(fd, bytes) != 0) {
            close(fd);
            throw std::runtime_error("ftruncate failed on " + path + ": " +
                                     strerror(errno));
        }
    } else {
        // Verify existing file size.
        struct stat st;
        if (fstat(fd, &st) != 0 || static_cast<size_t>(st.st_size) < bytes) {
            close(fd);
            throw std::runtime_error("File " + path + " is too small");
        }
    }

    void* addr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("mmap failed on " + path + ": " +
                                 strerror(errno));
    }

    regions_[idx] = {addr, bytes, fd};
}

void RegretStore::unmapAll() {
    int num_arrays = num_players_ * 2;
    for (int i = 0; i < num_arrays; ++i) {
        if (regions_[i].addr) {
            munmap(regions_[i].addr, regions_[i].size);
            regions_[i].addr = nullptr;
        }
        if (regions_[i].fd >= 0) {
            close(regions_[i].fd);
            regions_[i].fd = -1;
        }
    }
}
