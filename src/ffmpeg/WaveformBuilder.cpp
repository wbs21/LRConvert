#include "ffmpeg/WaveformBuilder.h"

#include <algorithm>
#include <cmath>

namespace lr {
namespace {

/// 算一段 [t0, t1)，写进共享大数组里自己那一段的下标区间。
/// 只写 [idx(t0), idx(t1))，与其他 worker 的区间不重叠 —— 这是并发安全的根据。
bool buildSegment(FfmpegApi &api, const std::string &path, double t0, double t1,
                  AudioPeaks &out, const std::atomic<bool> &stop, std::string &error) {
    const std::size_t total = out.size();
    const double pps = out.pointsPerSec;
    const double sampleStep = 1.0 / double(kPeakSampleRate);

    auto sink = [&](const int16_t *samples, int count, double startSec) -> bool {
        // 换文件时要能立刻收工：否则 stop() 的 join 会卡住 GUI 线程
        // （一小时片子的一段就是好几分钟音频）
        if (stop.load(std::memory_order_relaxed)) return true;
        double t = startSec;
        for (int i = 0; i < count; ++i, t += sampleStep) {
            if (t >= t1) return true;    // 越过段尾，这段算完了
            if (t < t0) continue;        // seek 往回退 1 秒多解的部分，丢掉
            if (pps <= 0.0) break;
            const auto idx = static_cast<std::size_t>((t - out.fromSec) * pps);
            if (idx >= total) continue;
            const int16_t v = samples[i];
            if (v < out.min[idx]) out.min[idx] = v;
            if (v > out.max[idx]) out.max[idx] = v;
        }
        return false;
    };

    const bool ok = AudioPeakBuilder::decodeRange(api, path, t0, t1, true, sink, error);
    // 被 stop 打断的段不完整，不标 ready（不然会画出半截波形）
    if (stop.load(std::memory_order_relaxed)) return false;

    // 采样投完之后才标 ready：绘制只画 ready=1 的片，
    // 所以不会看到"min 已更新、max 还没更新"的半截状态。
    const std::size_t i0 = out.indexAt(t0);
    const std::size_t i1 = (t1 >= out.toSec) ? total : out.indexAt(t1);
    for (std::size_t i = i0; i < i1 && i < out.ready.size(); ++i) out.ready[i] = 1;

    return ok;
}

}  // namespace

WaveformBuilder::~WaveformBuilder() { stop(); }

bool WaveformBuilder::start(FfmpegApi &api, const std::string &path, double duration,
                            double pointsPerSec, int segments, int workers,
                            const std::shared_ptr<AudioPeaks> &target) {
    stop();
    (void)pointsPerSec;  // 片数由 target 自己决定（allocate 时已定）
    if (duration <= 0.0 || target == nullptr || target->empty()) return false;

    target_ = target;
    segments = std::clamp(segments, 1, 256);
    workers = std::clamp(workers, 1, 16);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();
        const double segLen = duration / double(segments);
        for (int i = 0; i < segments; ++i) {
            const double t0 = double(i) * segLen;
            const double t1 = (i == segments - 1) ? duration : double(i + 1) * segLen;
            pending_.push_back(Segment{i, t0, t1});
        }
        total_ = segments;
    }

    doneCount_ = 0;
    stopFlag_ = false;
    running_ = true;
    for (int i = 0; i < workers; ++i) {
        threads_.emplace_back([this, &api, path]() { workerMain(&api, path); });
    }
    return true;
}

void WaveformBuilder::stop() {
    stopFlag_ = true;
    for (auto &t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
    running_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();
        total_ = 0;
    }
    target_.reset();
}

void WaveformBuilder::setPriorityRange(double from, double to) {
    std::lock_guard<std::mutex> lock(mutex_);
    priFrom_ = from;
    priTo_ = to;
    // 只影响"下一次取哪个"，已经在跑的段不打断 —— 打断要重新 seek，不划算
}

bool WaveformBuilder::takeNext(Segment &out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopFlag_ || pending_.empty()) return false;

    const double center = (priFrom_ + priTo_) * 0.5;
    auto score = [&](const Segment &s) {
        const double sc = (s.t0 + s.t1) * 0.5;
        if (sc >= priFrom_ && sc < priTo_) {
            return std::abs(sc - center);   // 页内：越靠中间越先算
        }
        // 页外：排到后面，按离可见区的距离排
        const double d = (sc < priFrom_) ? (priFrom_ - sc) : (sc - priTo_);
        return 1e9 + d;
    };

    auto best = pending_.begin();
    double bestScore = score(*best);
    for (auto it = pending_.begin() + 1; it != pending_.end(); ++it) {
        const double v = score(*it);
        if (v < bestScore) {
            bestScore = v;
            best = it;
        }
    }
    out = *best;
    pending_.erase(best);
    return true;
}

void WaveformBuilder::workerMain(FfmpegApi *api, std::string path) {
    Segment seg;
    while (takeNext(seg)) {
        if (stopFlag_) break;
        std::string error;
        if (!buildSegment(*api, path, seg.t0, seg.t1, *target_, stopFlag_, error)) {
            if (errorCb_ && !stopFlag_) errorCb_(error);
        }
        if (stopFlag_) break;
        const int done = ++doneCount_;
        if (progressCb_ && !stopFlag_) progressCb_(seg.index, seg.t0, seg.t1, done, total_);
        // 最后一段算完就把"算中"收掉（stop() 那边也会清）
        if (done >= total_) running_ = false;
    }
}

}  // namespace lr
