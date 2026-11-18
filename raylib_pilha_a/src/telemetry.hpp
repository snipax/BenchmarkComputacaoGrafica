#ifndef TELEMETRY_HPP
#define TELEMETRY_HPP

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    // Evita conflitos de nomes entre wingdi.h/winuser.h e a Raylib
    #define NOGDI
    #define NOUSER
    #include <windows.h>
    #include <psapi.h>
#elif defined(__linux__)
    #include <time.h>
    #include <unistd.h>
#endif

struct FrameSample {
    uint64_t frame_index;
    double elapsed_sec;
    double frame_time_ms; // Equacao (3): T_quadro(n) = t_swap(n) - t_swap(n-1)
    double fps_instant;
    double jitter_ms;     // Equacao (4): J(n) = |T_quadro(n) - T_quadro(n-1)|
    double rss_memory_mb; // Consumo de memoria fisica residente (RSS)
    int planet_count;
    int total_triangles;
};

struct StageSummary {
    std::string engine_name;
    std::string mode_name;
    std::string stage_id;
    int planet_count = 0;
    int total_triangles = 0;
    size_t total_frames = 0;
    double avg_fps = 0.0;
    double mean_frame_ms = 0.0;
    double stddev_frame_ms = 0.0;
    double p99_frame_ms = 0.0;
    double mean_jitter_ms = 0.0;
    double max_jitter_ms = 0.0;
    double peak_rss_mb = 0.0;
};

class TelemetryCollector {
public:
    TelemetryCollector() {
        InitClock();
    }

    // Leitura do relogio monotonico de hardware em segundos
    double GetMonotonicSeconds() const {
#if defined(_WIN32)
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return static_cast<double>(counter.QuadPart) / static_cast<double>(qpc_freq_.QuadPart);
#elif defined(__linux__)
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
#else
        return 0.0;
#endif
    }

    // Leitura do consumo de memoria fisica RSS (Resident Set Size) em Megabytes
    double ReadProcessRssMB() const {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
        }
        return 0.0;
#elif defined(__linux__)
        // Leitura direta do pseudossistema de arquivos /proc/self/statm conforme Secao III.D do artigo
        std::ifstream statm("/proc/self/statm");
        long total_pages = 0;
        long rss_pages = 0;
        if (statm >> total_pages >> rss_pages) {
            long page_size_bytes = sysconf(_SC_PAGESIZE);
            return (static_cast<double>(rss_pages) * static_cast<double>(page_size_bytes)) / (1024.0 * 1024.0);
        }
        return 0.0;
#else
        return 0.0;
#endif
    }

    void StartStage(const std::string& engine_name,
                    const std::string& mode_name,
                    const std::string& stage_id,
                    int planet_count,
                    int total_triangles,
                    int rss_sample_interval = 30) {
        engine_name_ = engine_name;
        mode_name_ = mode_name;
        stage_id_ = stage_id;
        planet_count_ = planet_count;
        total_triangles_ = total_triangles;
        rss_interval_ = (rss_sample_interval > 0) ? rss_sample_interval : 30;

        samples_.clear();
        samples_.reserve(3600); // Reserva previa para evitar realocacoes durante o benchmark

        stage_start_time_ = GetMonotonicSeconds();
        last_swap_time_ = stage_start_time_;
        prev_frame_time_ms_ = 0.0;
        cached_rss_mb_ = ReadProcessRssMB();
        frame_counter_ = 0;
    }

    // Chamado imediatamente apos cada EndDrawing() / buffer swap
    void RecordBufferSwap(bool record_sample) {
        double now = GetMonotonicSeconds();
        double dt_ms = (now - last_swap_time_) * 1000.0;
        last_swap_time_ = now;

        if (!record_sample) {
            // Fase de aquecimento (warmup): apenas atualiza a referencia temporal
            prev_frame_time_ms_ = dt_ms;
            stage_start_time_ = now;
            return;
        }

        if (frame_counter_ % rss_interval_ == 0) {
            cached_rss_mb_ = ReadProcessRssMB();
        }

        double jitter_ms = (frame_counter_ == 0) ? 0.0 : std::abs(dt_ms - prev_frame_time_ms_);
        prev_frame_time_ms_ = dt_ms;

        FrameSample sample;
        sample.frame_index = frame_counter_;
        sample.elapsed_sec = now - stage_start_time_;
        sample.frame_time_ms = dt_ms;
        sample.fps_instant = (dt_ms > 1e-6) ? (1000.0 / dt_ms) : 0.0;
        sample.jitter_ms = jitter_ms;
        sample.rss_memory_mb = cached_rss_mb_;
        sample.planet_count = planet_count_;
        sample.total_triangles = total_triangles_;

        samples_.push_back(sample);
        frame_counter_++;
    }

    bool ExportStageCsv(const std::string& filepath) const {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "[ERRO] Nao foi possivel abrir o arquivo CSV: " << filepath << "\n";
            return false;
        }

        file << "engine,mode,stage_id,frame,elapsed_s,frame_time_ms,fps,jitter_ms,rss_mb,planets,triangles\n";
        file << std::fixed << std::setprecision(4);

        for (const auto& s : samples_) {
            file << engine_name_ << ","
                 << mode_name_ << ","
                 << stage_id_ << ","
                 << s.frame_index << ","
                 << s.elapsed_sec << ","
                 << s.frame_time_ms << ","
                 << s.fps_instant << ","
                 << s.jitter_ms << ","
                 << s.rss_memory_mb << ","
                 << s.planet_count << ","
                 << s.total_triangles << "\n";
        }
        return true;
    }

    StageSummary ComputeSummary() const {
        StageSummary sum;
        sum.engine_name = engine_name_;
        sum.mode_name = mode_name_;
        sum.stage_id = stage_id_;
        sum.planet_count = planet_count_;
        sum.total_triangles = total_triangles_;
        sum.total_frames = samples_.size();

        if (samples_.empty()) return sum;

        std::vector<double> ft_values;
        ft_values.reserve(samples_.size());

        double sum_ft = 0.0;
        double sum_jitter = 0.0;
        double max_jitter = 0.0;
        double max_rss = 0.0;

        for (const auto& s : samples_) {
            ft_values.push_back(s.frame_time_ms);
            sum_ft += s.frame_time_ms;
            sum_jitter += s.jitter_ms;
            if (s.jitter_ms > max_jitter) max_jitter = s.jitter_ms;
            if (s.rss_memory_mb > max_rss) max_rss = s.rss_memory_mb;
        }

        sum.mean_frame_ms = sum_ft / static_cast<double>(samples_.size());
        sum.avg_fps = (sum.mean_frame_ms > 1e-6) ? (1000.0 / sum.mean_frame_ms) : 0.0;
        sum.mean_jitter_ms = sum_jitter / static_cast<double>(samples_.size());
        sum.max_jitter_ms = max_jitter;
        sum.peak_rss_mb = max_rss;

        double sq_diff = 0.0;
        for (double ft : ft_values) {
            double d = ft - sum.mean_frame_ms;
            sq_diff += d * d;
        }
        sum.stddev_frame_ms = std::sqrt(sq_diff / static_cast<double>(samples_.size()));

        std::sort(ft_values.begin(), ft_values.end());
        size_t p99_idx = static_cast<size_t>(std::floor(0.99 * static_cast<double>(ft_values.size() - 1)));
        sum.p99_frame_ms = ft_values[p99_idx];

        return sum;
    }

    double GetPrevFrameTimeMs() const { return prev_frame_time_ms_; }
    double GetCachedRssMB() const { return cached_rss_mb_; }

private:
    void InitClock() {
#if defined(_WIN32)
        QueryPerformanceFrequency(&qpc_freq_);
#endif
    }

#if defined(_WIN32)
    LARGE_INTEGER qpc_freq_;
#endif

    std::string engine_name_ = "Raylib_Pilha_A";
    std::string mode_name_ = "mesh_density";
    std::string stage_id_ = "MD_500";
    int planet_count_ = 1;
    int total_triangles_ = 500;
    int rss_interval_ = 30;

    double stage_start_time_ = 0.0;
    double last_swap_time_ = 0.0;
    double prev_frame_time_ms_ = 0.0;
    double cached_rss_mb_ = 0.0;
    uint64_t frame_counter_ = 0;

    std::vector<FrameSample> samples_;
};

#endif // TELEMETRY_HPP