#pragma once

#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <filesystem>
#include <optional>
#include <iostream>
#include <mutex>
#include <thread>
#include <atomic>

#include "NeuralAmpModelerCore/NAM/dsp.h"

class ResamplingNAM;

// Per-model parameter overrides (optional).
// If a value is set, it will be applied when the model activates.
struct ModelParamOverrides
{
  std::optional<double> outputLevel; // dB
  std::optional<double> toneBass;    // 0..10
  std::optional<double> toneMid;     // 0..10
  std::optional<double> toneTreble;  // 0..10
};

// A single slot in the model map: range + preloaded model + optional param overrides
struct ModelMapSlot
{
  double ampGainMin = 0.0;   // Amp Gain knob range min (0–10)
  double ampGainMax = 10.0;  // Amp Gain knob range max (0–10)
  std::string namFilePath;
  ModelParamOverrides overrides;

  // Preloaded model, kept alive here for instant switching
  std::shared_ptr<ResamplingNAM> preloadedModel;
};

// Result returned from LoadFromFile
struct PNAMLoadResult
{
  bool success = false;
  std::string errorMessage;
  std::vector<std::string> skippedFiles; // .nam paths that were skipped (missing)
  int loadedSlots = 0;
};

// Called when a .nam file referenced in a .pnam slot cannot be found.
// Return the replacement path to use, or empty string to skip the slot.
// Return nullopt to abort the entire load.
using MissingFileResolver = std::function<std::optional<std::string>(const std::string& missingPath, int slotIndex)>;

class NAMModelMapper
{
public:
  NAMModelMapper() = default;
  ~NAMModelMapper()
  {
    // Ensure background preload thread is finished before destruction.
    if (mPreloadThread.joinable())
      mPreloadThread.join();
  }

  // Non-copyable due to thread member
  NAMModelMapper(const NAMModelMapper&) = delete;
  NAMModelMapper& operator=(const NAMModelMapper&) = delete;
  NAMModelMapper(NAMModelMapper&&) = default;
  NAMModelMapper& operator=(NAMModelMapper&&) = default;

  // Whether the mapper is active
  bool IsActive() const { return mActive && !mSlots.empty(); }
  void SetActive(bool active) { mActive = active; }

  // Whether a background preload is in progress
  bool IsPreloading() const { return mPreloading.load(); }

  // Add a slot manually (alternative to LoadFromFile)
  void AddSlot(double gainMin, double gainMax, const std::string& namPath,
               const ModelParamOverrides& overrides = {})
  {
    ModelMapSlot slot;
    slot.ampGainMin = gainMin;
    slot.ampGainMax = gainMax;
    slot.namFilePath = namPath;
    slot.overrides = overrides;
    mSlots.push_back(std::move(slot));
  }

  void ClearSlots()
  {
    // Wait for any in-progress preload to finish before clearing.
    if (mPreloadThread.joinable())
      mPreloadThread.join();

    std::lock_guard<std::mutex> lock(mMutex);
    mSlots.clear();
    mActiveSlotIndex = -1;
    mLoadedPNAMPath.clear();
  }

  // Load a .pnam JSON file — parses slots and resolves missing files.
  // When preload=false, PreloadAllAsync() must be called separately.
  // Call from the UI thread only (resolver may show dialogs).
  PNAMLoadResult LoadFromFile(const std::string& pnamPath, double sampleRate, int blockSize,
                              MissingFileResolver resolver = nullptr, bool preload = true);

  // Preload all slots synchronously. Returns number of successfully loaded models.
  int PreloadAll(double sampleRate, int blockSize);

  // Preload all slots on a background thread.
  // onComplete is called from that thread when done — use an atomic flag and check in OnIdle.
  void PreloadAllAsync(double sampleRate, int blockSize, std::function<void(int loadedCount)> onComplete);

  // Evaluate input gain, returns new model if the active slot changed, else nullptr.
  std::shared_ptr<ResamplingNAM> EvaluateInputGain(double inputGainValue);

  std::optional<ModelParamOverrides> GetActiveOverrides() const
  {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mActiveSlotIndex >= 0 && mActiveSlotIndex < static_cast<int>(mSlots.size()))
      return mSlots[mActiveSlotIndex].overrides;
    return std::nullopt;
  }

  const std::string& GetLoadedPNAMPath() const { return mLoadedPNAMPath; }
  int GetActiveSlotIndex() const { return mActiveSlotIndex; }
  size_t GetSlotCount() const { return mSlots.size(); }
  const std::vector<ModelMapSlot>& GetSlots() const { return mSlots; }

  // Reset active slot tracking (e.g. when sample rate changes)
  void ResetActiveSlot()
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mActiveSlotIndex = -1;
  }

private:
  int FindSlotForGain(double gain) const
  {
    const int n = static_cast<int>(mSlots.size());
    for (int i = 0; i < n; i++)
    {
      if (gain < mSlots[i].ampGainMin)
        continue;
      // Last slot: inclusive upper bound (catches gain == 10.0).
      // All others: exclusive upper bound so the boundary value belongs to the
      // next slot, and any tiny gap produced by Distribute is filled.
      const bool belowMax = (i == n - 1) ? (gain <= mSlots[i].ampGainMax)
                                          : (gain < mSlots[i].ampGainMax + 0.01);
      if (belowMax)
        return i;
    }
    return -1;
  }

  std::vector<ModelMapSlot> mSlots;
  int mActiveSlotIndex = -1;
  bool mActive = false;
  std::string mLoadedPNAMPath;
  mutable std::mutex mMutex;

  std::thread mPreloadThread;
  std::atomic<bool> mPreloading{false};
};