#include "NAMModelMapper.h"
#include "../iPlug2/IPlug/IPlugConstants.h"  
#include "NeuralAmpModelerCore/NAM/get_dsp.h"
#include "NeuralAmpModeler.h" // ResamplingNAM
#include "json.hpp"

#include <cstdio>
#if defined(_WIN32)
#  include <Windows.h>
#  define NAM_MAPPER_LOG(fmt, ...) \
     do { char _d[512]; snprintf(_d, sizeof(_d), fmt, ##__VA_ARGS__); OutputDebugStringA(_d); } while (0)
#else
#  define NAM_MAPPER_LOG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#endif

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// LoadFromFile
// ---------------------------------------------------------------------------
PNAMLoadResult NAMModelMapper::LoadFromFile(const std::string& pnamPath, double sampleRate, int blockSize,
                                            MissingFileResolver resolver, bool preload)
{
  PNAMLoadResult result;

  // --- Parse JSON ---
  std::ifstream f(pnamPath);
  if (!f.is_open())
  {
    result.errorMessage = "Cannot open file: " + pnamPath;
    return result;
  }

  json j;
  try
  {
    j = json::parse(f);
  }
  catch (const json::parse_error& e)
  {
    result.errorMessage = std::string("JSON parse error: ") + e.what();
    return result;
  }

  // Version check
  const int kSupportedVersion = 1;
  int fileVersion = j.value("pnam_version", 0);
  if (fileVersion != kSupportedVersion)
  {
    result.errorMessage = "Unsupported pnam_version: " + std::to_string(fileVersion)
                          + " (expected " + std::to_string(kSupportedVersion) + ")";
    return result;
  }

  if (!j.contains("slots") || !j["slots"].is_array())
  {
    result.errorMessage = "Missing or invalid 'slots' array in .pnam file.";
    return result;
  }

  // --- Build slots ---
  std::vector<ModelMapSlot> newSlots;

  int slotIndex = 0;
  for (const auto& jSlot : j["slots"])
  {
    ModelMapSlot slot;
    slot.ampGainMin = jSlot.value("amp_gain_min", 0.0);
    slot.ampGainMax = jSlot.value("amp_gain_max", 10.0);
    slot.namFilePath  = jSlot.value("nam_path", std::string{});

    // Optional overrides
    if (jSlot.contains("overrides"))
    {
      const auto& jOv = jSlot["overrides"];
      if (jOv.contains("output_level")) slot.overrides.outputLevel = jOv["output_level"].get<double>();
      if (jOv.contains("tone_bass"))    slot.overrides.toneBass    = jOv["tone_bass"].get<double>();
      if (jOv.contains("tone_mid"))     slot.overrides.toneMid     = jOv["tone_mid"].get<double>();
      if (jOv.contains("tone_treble"))  slot.overrides.toneTreble  = jOv["tone_treble"].get<double>();
    }

    // Check if the .nam file exists
    if (!slot.namFilePath.empty() && !std::filesystem::exists(slot.namFilePath))
    {
      if (resolver)
      {
        auto resolved = resolver(slot.namFilePath, slotIndex);
        if (!resolved.has_value())
        {
          // User chose to abort
          result.errorMessage = "Load aborted by user at slot " + std::to_string(slotIndex) + ".";
          return result;
        }
        if (resolved->empty())
        {
          // User chose to skip this slot
          result.skippedFiles.push_back(slot.namFilePath);
          slotIndex++;
          continue;
        }
        slot.namFilePath = *resolved;
      }
      else
      {
        // No resolver: skip silently
        result.skippedFiles.push_back(slot.namFilePath);
        slotIndex++;
        continue;
      }
    }

    newSlots.push_back(std::move(slot));
    slotIndex++;
  }

  // --- Commit ---
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mSlots = std::move(newSlots);
    mActiveSlotIndex = -1;
    mLoadedPNAMPath = pnamPath;
  }

  if (preload)
    result.loadedSlots = PreloadAll(sampleRate, blockSize);

  result.success = true;
  return result;
}

// ---------------------------------------------------------------------------
// PreloadAll
// ---------------------------------------------------------------------------
int NAMModelMapper::PreloadAll(double sampleRate, int blockSize)
{
  // Snapshot slot paths under lock — don't hold the mutex during heavy I/O.
  struct SlotToLoad { int index; std::string namFilePath; };
  std::vector<SlotToLoad> toLoad;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    for (int i = 0; i < static_cast<int>(mSlots.size()); ++i)
    {
      if (!mSlots[i].preloadedModel && !mSlots[i].namFilePath.empty())
        toLoad.push_back({i, mSlots[i].namFilePath});
    }
  }

  if (toLoad.empty())
    return 0;

  // Load all slots concurrently — reduces N×T to ~T for network/CPU-bound models.
  std::atomic<int> loaded{0};
  std::vector<std::thread> workers;
  workers.reserve(toLoad.size());

  for (auto& item : toLoad)
  {
    workers.emplace_back([this, &item, sampleRate, blockSize, &loaded]() {
      std::shared_ptr<ResamplingNAM> resampling;
      try
      {
        auto dspPath = std::filesystem::u8path(item.namFilePath);
        std::unique_ptr<nam::DSP> rawModel = nam::get_dsp(dspPath);
        resampling = std::make_shared<ResamplingNAM>(std::move(rawModel), sampleRate);
        resampling->Reset(sampleRate, blockSize);

        NAM_MAPPER_LOG("[ModelMapper] Preloaded slot %d: %s\n", item.index, item.namFilePath.c_str());
        loaded.fetch_add(1);
      }
      catch (const std::exception& e)
      {
        NAM_MAPPER_LOG("[ModelMapper] FAILED slot %d (%s): %s\n", item.index, item.namFilePath.c_str(), e.what());
        resampling = nullptr;
      }
      catch (...)
      {
        NAM_MAPPER_LOG("[ModelMapper] UNKNOWN EXCEPTION slot %d: %s\n", item.index, item.namFilePath.c_str());
        resampling = nullptr;
      }

      // Write result back under a narrow per-slot lock.
      std::lock_guard<std::mutex> lock(mMutex);
      if (item.index < static_cast<int>(mSlots.size()))
        mSlots[item.index].preloadedModel = std::move(resampling);
    });
  }

  for (auto& w : workers)
    w.join();

  return loaded.load();
}

// ---------------------------------------------------------------------------
// PreloadAllAsync
// ---------------------------------------------------------------------------
void NAMModelMapper::PreloadAllAsync(double sampleRate, int blockSize, std::function<void(int loadedCount)> onComplete)
{
  if (mPreloadThread.joinable())
    mPreloadThread.join();

  mPreloading.store(true);
  mPreloadThread = std::thread([this, sampleRate, blockSize, onComplete = std::move(onComplete)]() {
    NAM_MAPPER_LOG("[ModelMapper] PreloadAllAsync thread STARTED\n");
    int loaded = 0;
    try
    {
      loaded = PreloadAll(sampleRate, blockSize);
    }
    catch (...) 
    {
      NAM_MAPPER_LOG("[ModelMapper] PreloadAll threw unexpected exception\n");
    }
    mPreloading.store(false);
    NAM_MAPPER_LOG("[ModelMapper] PreloadAllAsync thread DONE, loaded=%d, firing callback\n", loaded);
    if (onComplete)
      onComplete(loaded);
    NAM_MAPPER_LOG("[ModelMapper] PreloadAllAsync callback RETURNED\n");
  });
}

// ---------------------------------------------------------------------------
// EvaluateInputGain
// ---------------------------------------------------------------------------
std::shared_ptr<ResamplingNAM> NAMModelMapper::EvaluateInputGain(double inputGainValue)
{
  std::lock_guard<std::mutex> lock(mMutex);

  int newIndex = FindSlotForGain(inputGainValue);

  if (newIndex == mActiveSlotIndex || newIndex < 0)
    return nullptr;

  if (!mSlots[newIndex].preloadedModel)
    return nullptr;

  mActiveSlotIndex = newIndex;
  return mSlots[newIndex].preloadedModel;
}

