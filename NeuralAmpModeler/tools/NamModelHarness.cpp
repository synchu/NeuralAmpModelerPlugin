#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
#endif

#include "NeuralAmpModelerCore/NAM/activations.h"
#include "NeuralAmpModelerCore/NAM/get_dsp.h"

static const char* GetOpt(int argc, char** argv, const char* name)
{
  for (int i = 1; i < argc - 1; ++i)
    if (std::string(argv[i]) == name)
      return argv[i + 1];
  return nullptr;
}

static bool HasFlag(int argc, char** argv, const char* name)
{
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == name)
      return true;
  return false;
}

static void PrintUsage()
{
  std::cout <<
    "NamModelHarness\n"
    "Usage:\n"
    "  NamModelHarness [--model <path-to-model.nam>] [--sr 48000] [--block 64] [--seconds 5]\n"
    "                 [--freq 110] [--no-prewarm]\n"
    "                 [--fast-tanh | --compare-fast-tanh]\n"
    "                 [--trials 7] [--warmup-blocks 200]\n"
#ifdef _WIN32
    "                 [--pin-core 4] [--priority-high]\n"
#endif
    "\n"
    "Flags:\n"
    "  --fast-tanh           Enable nam::activations::Activation::enable_fast_tanh() for this run.\n"
    "  --compare-fast-tanh   Run baseline suite then fast-tanh suite.\n"
    "  --trials N            Run N timed trials per suite (default 7).\n"
    "  --warmup-blocks N     Run N warmup blocks before each timed trial (default 200).\n"
#ifdef _WIN32
    "  --pin-core N          Pin current thread to CPU core N (reduces jitter).\n"
    "  --priority-high       Raise process/thread priority (reduces jitter).\n"
#endif
    "\n"
    "If --model is omitted, defaults to:\n"
    "  C:\\Users\\npn\\source\\repos\\NeuralAmpModelerPlugin\\NeuralAmpModeler\\complex_marshall_model_test.nam\n"
    "\n"
    "Examples:\n"
    "  NamModelHarness --seconds 20 --compare-fast-tanh --trials 9 --warmup-blocks 500\n";
}

static void PrintBuildInfo()
{
  std::cout << "Build: ";

#if defined(__clang__)
  // clang-cl defines both __clang__ and _MSC_VER; print both so it's unambiguous.
  std::cout << "clang-cl " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__
            << " (msvc-compat " << _MSC_VER << ")";
#elif defined(_MSC_VER)
  std::cout << "MSVC " << _MSC_VER;
#else
  std::cout << "UnknownCompiler";
#endif

  std::cout << ", " << (sizeof(void*) == 8 ? "x64" : "x86");

#if defined(__AVX2__)
  std::cout << ", AVX2";
#elif defined(__AVX__)
  std::cout << ", AVX";
#elif defined(__SSE4_2__)
  std::cout << ", SSE4.2";
#elif defined(__SSE2__)
  std::cout << ", SSE2";
#endif

  std::cout << ", built " << __DATE__ << " " << __TIME__ << "\n\n";
}

struct BenchResult
{
  double elapsedSeconds = 0.0;
  double audioSeconds = 0.0;
  double realtimeFactor = 0.0;
};

struct BenchSummary
{
  double minElapsedSeconds = 0.0;
  double medianElapsedSeconds = 0.0;
  double meanElapsedSeconds = 0.0;

  double minRt = 0.0;
  double medianRt = 0.0;
  double meanRt = 0.0;
};

static BenchSummary Summarize(const std::vector<BenchResult>& results)
{
  std::vector<double> elapsed;
  std::vector<double> rt;
  elapsed.reserve(results.size());
  rt.reserve(results.size());

  for (const auto& r : results)
  {
    elapsed.push_back(r.elapsedSeconds);
    rt.push_back(r.realtimeFactor);
  }

  auto medianOf = [](std::vector<double> v) -> double
  {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    if (n == 0)
      return 0.0;
    if ((n & 1) == 1)
      return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
  };

  BenchSummary s;
  s.minElapsedSeconds = *std::min_element(elapsed.begin(), elapsed.end());
  s.medianElapsedSeconds = medianOf(elapsed);
  s.meanElapsedSeconds = std::accumulate(elapsed.begin(), elapsed.end(), 0.0) / elapsed.size();

  s.minRt = *std::min_element(rt.begin(), rt.end());
  s.medianRt = medianOf(rt);
  s.meanRt = std::accumulate(rt.begin(), rt.end(), 0.0) / rt.size();
  return s;
}

static void PrintTrials(const char* label, const std::vector<BenchResult>& results)
{
  if (results.empty())
    return;

  std::cout << label << " trials:\n";

  // Use fixed formatting so comparisons are easy when pasting logs
  std::cout << std::fixed << std::setprecision(6);

  for (size_t i = 0; i < results.size(); ++i)
  {
    const auto& r = results[i];
    std::cout << "  [" << i << "] elapsed=" << r.elapsedSeconds << " s"
              << ", audio=" << r.audioSeconds << " s"
              << ", RT=" << r.realtimeFactor << "\n";
  }

  // Restore default-ish formatting for the rest of the output
  std::cout.unsetf(std::ios::floatfield);
  std::cout << "\n";
}

#ifdef _WIN32
static void ApplyWindowsJitterReduction(const int pinCore, const bool highPriority)
{
  if (highPriority)
  {
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
  }

  if (pinCore >= 0 && pinCore < 63)
  {
    const DWORD_PTR mask = (static_cast<DWORD_PTR>(1) << pinCore);
    SetThreadAffinityMask(GetCurrentThread(), mask);
  }
}
#endif

static BenchResult RunTrial(nam::DSP& model, const double sampleRate, const int blockSize, const bool prewarm,
                            const std::vector<NAM_SAMPLE>& inputAll, const int iterations, const int warmupBlocks)
{
  model.Reset(sampleRate, blockSize);
  if (prewarm)
    model.prewarm();

  std::vector<NAM_SAMPLE> output(blockSize);

  // Warmup (not timed)
  const int warmIters = std::min(warmupBlocks, iterations);
  for (int it = 0; it < warmIters; ++it)
  {
    NAM_SAMPLE* in = const_cast<NAM_SAMPLE*>(&inputAll[static_cast<size_t>(it) * blockSize]);
    NAM_SAMPLE* out = output.data();
    model.process(&in, &out, blockSize);
  }

  const auto t0 = std::chrono::high_resolution_clock::now();

  for (int it = 0; it < iterations; ++it)
  {
    NAM_SAMPLE* in = const_cast<NAM_SAMPLE*>(&inputAll[static_cast<size_t>(it) * blockSize]);
    NAM_SAMPLE* out = output.data();
    model.process(&in, &out, blockSize);
  }

  const auto t1 = std::chrono::high_resolution_clock::now();
  const std::chrono::duration<double> elapsed = t1 - t0;

  BenchResult r;
  r.elapsedSeconds = elapsed.count();
  r.audioSeconds = (static_cast<double>(iterations) * blockSize) / sampleRate;
  r.realtimeFactor = r.elapsedSeconds / r.audioSeconds;
  return r;
}

static std::vector<BenchResult> RunSuite(const std::filesystem::path& modelPath, const double sampleRate,
                                         const int blockSize, const bool prewarm, const std::vector<NAM_SAMPLE>& inputAll,
                                         const int iterations, const int warmupBlocks, const int trials,
                                         const bool fastTanh)
{
  // Toggle BEFORE loading model
  nam::activations::Activation::disable_fast_tanh();
  if (fastTanh)
    nam::activations::Activation::enable_fast_tanh();

  std::unique_ptr<nam::DSP> model = nam::get_dsp(modelPath);
  if (!model)
    throw std::runtime_error("nam::get_dsp returned null");

  std::vector<BenchResult> results;
  results.reserve(static_cast<size_t>(trials));

  for (int t = 0; t < trials; ++t)
    results.push_back(RunTrial(*model, sampleRate, blockSize, prewarm, inputAll, iterations, warmupBlocks));

  return results;
}

int main(int argc, char** argv)
{
  if (HasFlag(argc, argv, "--help") || HasFlag(argc, argv, "-h"))
  {
    PrintUsage();
    return 0;
  }

  PrintBuildInfo();

  static constexpr const char* kDefaultModelPath =
    R"(C:\Users\npn\source\repos\NeuralAmpModelerPlugin\NeuralAmpModeler\complex_marshall_model_test.nam)";

  const char* modelPathArg = GetOpt(argc, argv, "--model");
  if (!modelPathArg || !*modelPathArg)
  {
    modelPathArg = kDefaultModelPath;
    std::cout << "Note: --model not provided, using default:\n  " << modelPathArg << "\n\n";
  }

  const double sampleRate = GetOpt(argc, argv, "--sr") ? std::stod(GetOpt(argc, argv, "--sr")) : 48000.0;
  const int blockSize = GetOpt(argc, argv, "--block") ? std::stoi(GetOpt(argc, argv, "--block")) : 64;
  const double seconds = GetOpt(argc, argv, "--seconds") ? std::stod(GetOpt(argc, argv, "--seconds")) : 5.0;
  const double freqHz = GetOpt(argc, argv, "--freq") ? std::stod(GetOpt(argc, argv, "--freq")) : 110.0;
  const bool prewarm = !HasFlag(argc, argv, "--no-prewarm");

  const int trials = GetOpt(argc, argv, "--trials") ? std::stoi(GetOpt(argc, argv, "--trials")) : 7;
  const int warmupBlocks =
    GetOpt(argc, argv, "--warmup-blocks") ? std::stoi(GetOpt(argc, argv, "--warmup-blocks")) : 200;

  const bool compareFastTanh = HasFlag(argc, argv, "--compare-fast-tanh");
  const bool fastTanhOnly = HasFlag(argc, argv, "--fast-tanh") && !compareFastTanh;

#ifdef _WIN32
  const bool priorityHigh = HasFlag(argc, argv, "--priority-high");
  const int pinCore = GetOpt(argc, argv, "--pin-core") ? std::stoi(GetOpt(argc, argv, "--pin-core")) : -1;
  ApplyWindowsJitterReduction(pinCore, priorityHigh);
#endif

  try
  {
    const std::filesystem::path modelPath = std::filesystem::u8path(modelPathArg);
    if (!std::filesystem::exists(modelPath))
    {
      std::cerr << "Error: model does not exist: " << modelPathArg << "\n\n";
      PrintUsage();
      return 2;
    }

    const int totalFrames = static_cast<int>(std::llround(seconds * sampleRate));
    const int iterations = (totalFrames + blockSize - 1) / blockSize;

    // Precompute input once
    std::vector<NAM_SAMPLE> inputAll(static_cast<size_t>(iterations) * blockSize);
    double phase = 0.0;
    const double phaseInc = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;

    for (int it = 0; it < iterations; ++it)
    {
      for (int i = 0; i < blockSize; ++i)
      {
        inputAll[static_cast<size_t>(it) * blockSize + i] = static_cast<NAM_SAMPLE>(0.1 * std::sin(phase));
        phase += phaseInc;
        if (phase >= 2.0 * 3.14159265358979323846)
          phase -= 2.0 * 3.14159265358979323846;
      }
    }

    std::cout << "Model: " << modelPath.u8string() << "\n";
    std::cout << "SR: " << sampleRate << " Hz, Block: " << blockSize << ", Target: " << seconds << " s\n";
    std::cout << "Trials: " << trials << ", WarmupBlocks: " << warmupBlocks << "\n";

#ifdef _WIN32
    if (pinCore >= 0)
      std::cout << "Pinned core: " << pinCore << "\n";
    if (priorityHigh)
      std::cout << "Priority: high\n";
#endif

    if (compareFastTanh)
    {
      const auto baseResults =
        RunSuite(modelPath, sampleRate, blockSize, prewarm, inputAll, iterations, warmupBlocks, trials, false);
      const auto fastResults =
        RunSuite(modelPath, sampleRate, blockSize, prewarm, inputAll, iterations, warmupBlocks, trials, true);

      PrintTrials("Baseline", baseResults);
      PrintTrials("FastTanh", fastResults);

      const BenchSummary base = Summarize(baseResults);
      const BenchSummary fast = Summarize(fastResults);

      std::cout << "Baseline (min/med/mean RT): " << base.minRt << " / " << base.medianRt << " / " << base.meanRt
                << "  | elapsed(min/med/mean): " << base.minElapsedSeconds << " / " << base.medianElapsedSeconds
                << " / " << base.meanElapsedSeconds << " s\n";
      std::cout << "FastTanh  (min/med/mean RT): " << fast.minRt << " / " << fast.medianRt << " / " << fast.meanRt
                << "  | elapsed(min/med/mean): " << fast.minElapsedSeconds << " / " << fast.medianElapsedSeconds
                << " / " << fast.meanElapsedSeconds << " s\n";

      const double speedup = base.medianRt / fast.medianRt;
      std::cout << "Speedup (median RT, Baseline/FastTanh): " << speedup << "x\n";
    }
    else
    {
      const auto results =
        RunSuite(modelPath, sampleRate, blockSize, prewarm, inputAll, iterations, warmupBlocks, trials, fastTanhOnly);

      PrintTrials(fastTanhOnly ? "FastTanh" : "Baseline", results);

      const BenchSummary s = Summarize(results);

      std::cout << (fastTanhOnly ? "FastTanh" : "Baseline") << " (min/med/mean RT): " << s.minRt << " / "
                << s.medianRt << " / " << s.meanRt << "\n";
      std::cout << (fastTanhOnly ? "FastTanh" : "Baseline")
                << " elapsed(min/med/mean): " << s.minElapsedSeconds << " / " << s.medianElapsedSeconds << " / "
                << s.meanElapsedSeconds << " s\n";
    }

    return 0;
  }
  catch (const std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
    return 1;
  }
}