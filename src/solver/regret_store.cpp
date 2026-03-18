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

// File names within the store directory.
static const char* kFileNames[4] = {
    "regrets_p0.bin", "regrets_p1.bin",
    "strategy_p0.bin", "strategy_p1.bin"
};

static const char* kMetaFile = "meta.txt";

// ── Construction (new store) ─────────────────────────────────────────────

RegretStore::RegretStore(uint64_t num_info_sets, int max_actions,
                         const std::string& dir)
    : num_info_sets_(num_info_sets), max_actions_(max_actions), dir_(dir) {

    assert(num_info_sets > 0);
    assert(max_actions > 0);

    if (dir.empty()) {
        // Anonymous mode: heap-allocated, zero-initialized.
        anonymous_ = true;
        size_t n = totalEntries();
        for (int i = 0; i < 4; ++i) {
            regions_[i].addr = nullptr;
            regions_[i].size = 0;
            regions_[i].fd = -1;
        }
        regrets_[0]  = new float[n]();
        regrets_[1]  = new float[n]();
        strategy_[0] = new float[n]();
        strategy_[1] = new float[n]();
    } else {
        // File-backed mode: create mmap'd files.
        anonymous_ = false;
        for (int i = 0; i < 4; ++i)
            mapFile(i, dir + "/" + kFileNames[i], /*create=*/true);
        regrets_[0]  = static_cast<float*>(regions_[0].addr);
        regrets_[1]  = static_cast<float*>(regions_[1].addr);
        strategy_[0] = static_cast<float*>(regions_[2].addr);
        strategy_[1] = static_cast<float*>(regions_[3].addr);
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
    meta >> num_info_sets >> max_actions >> iteration;

    if (num_info_sets == 0 || max_actions == 0)
        throw std::runtime_error("Invalid metadata in " + dir + "/" + kMetaFile);

    // Create store and map existing files.
    RegretStore store;
    store.num_info_sets_ = num_info_sets;
    store.max_actions_ = max_actions;
    store.anonymous_ = false;
    store.dir_ = dir;

    for (int i = 0; i < 4; ++i)
        store.mapFile(i, dir + "/" + kFileNames[i], /*create=*/false);

    store.regrets_[0]  = static_cast<float*>(store.regions_[0].addr);
    store.regrets_[1]  = static_cast<float*>(store.regions_[1].addr);
    store.strategy_[0] = static_cast<float*>(store.regions_[2].addr);
    store.strategy_[1] = static_cast<float*>(store.regions_[3].addr);

    return store;
}

// ── Destructor ───────────────────────────────────────────────────────────

RegretStore::~RegretStore() {
    if (anonymous_) {
        delete[] regrets_[0];
        delete[] regrets_[1];
        delete[] strategy_[0];
        delete[] strategy_[1];
    } else {
        unmapAll();
    }
}

// ── Move ─────────────────────────────────────────────────────────────────

RegretStore::RegretStore(RegretStore&& other) noexcept
    : num_info_sets_(other.num_info_sets_),
      max_actions_(other.max_actions_),
      regrets_(other.regrets_),
      strategy_(other.strategy_),
      regions_(other.regions_),
      anonymous_(other.anonymous_),
      dir_(std::move(other.dir_)) {
    other.num_info_sets_ = 0;
    other.max_actions_ = 0;
    other.regrets_ = {nullptr, nullptr};
    other.strategy_ = {nullptr, nullptr};
    other.regions_ = {};
}

RegretStore& RegretStore::operator=(RegretStore&& other) noexcept {
    if (this != &other) {
        if (anonymous_) {
            delete[] regrets_[0];
            delete[] regrets_[1];
            delete[] strategy_[0];
            delete[] strategy_[1];
        } else {
            unmapAll();
        }
        num_info_sets_ = other.num_info_sets_;
        max_actions_ = other.max_actions_;
        regrets_ = other.regrets_;
        strategy_ = other.strategy_;
        regions_ = other.regions_;
        anonymous_ = other.anonymous_;
        dir_ = std::move(other.dir_);

        other.num_info_sets_ = 0;
        other.max_actions_ = 0;
        other.regrets_ = {nullptr, nullptr};
        other.strategy_ = {nullptr, nullptr};
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
    for (int i = 0; i < 4; ++i) {
        if (regions_[i].addr)
            msync(regions_[i].addr, regions_[i].size, flags);
    }
}

void RegretStore::checkpoint(const std::string& dir, uint64_t iteration) const {
    if (!anonymous_) {
        // Sync the current files first.
        for (int i = 0; i < 4; ++i) {
            if (regions_[i].addr)
                msync(regions_[i].addr, regions_[i].size, MS_SYNC);
        }
    }

    // Write metadata.
    std::string meta_path = dir + "/" + kMetaFile;
    std::ofstream meta(meta_path);
    if (!meta)
        throw std::runtime_error("Cannot write " + meta_path);
    meta << num_info_sets_ << " " << max_actions_ << " " << iteration << "\n";
    meta.close();

    // If anonymous, dump arrays to files.
    if (anonymous_) {
        size_t bytes = totalEntries() * sizeof(float);
        const float* ptrs[4] = {regrets_[0], regrets_[1], strategy_[0], strategy_[1]};
        for (int i = 0; i < 4; ++i) {
            std::string path = dir + "/" + kFileNames[i];
            std::ofstream ofs(path, std::ios::binary);
            if (!ofs)
                throw std::runtime_error("Cannot write " + path);
            ofs.write(reinterpret_cast<const char*>(ptrs[i]), bytes);
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
    for (int i = 0; i < 4; ++i) {
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
