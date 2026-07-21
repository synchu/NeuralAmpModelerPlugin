#pragma once

#include "NAMLibraryTreeNode.h"
#include <string>
#include <vector>
#include <memory>

enum class NAMLibraryGroupBy
{
  Library = 0,
  GearMake,
  GearModel,
  ToneType,
  Author,
  Tag
};

class NAMLibraryManager
{
public:
  NAMLibraryManager();
  ~NAMLibraryManager();

  /**
   * Load metadata from data.json and build tree hierarchy
   * @param jsonFilePath Full path to data.json
   * @return true if successful
   */
  bool LoadMetadata(const std::string& jsonFilePath);

  /**
   * Get root node of the tree (contains entire hierarchy)
   */
  std::shared_ptr<NAMLibraryTreeNode> GetRootNode() const { return mRootNode; }

  /**
   * Get flattened list of all models (for searching)
   */
  const std::vector<std::shared_ptr<NAMLibraryTreeNode>>& GetAllModels() const { return mAllModels; }

  /**
   * Get the sorted tag index built while loading data.json.
   */
  const std::vector<std::string>& GetAllTags() const { return mAllTags; }

  /**
   * Collect the sorted tags represented by a filtered model result set.
   */
  std::vector<std::string> CollectTags(
    const std::vector<std::shared_ptr<NAMLibraryTreeNode>>& models) const;

  /**
   * Search models by name, tags, or metadata (returns flattened results)
   */
  std::vector<std::shared_ptr<NAMLibraryTreeNode>> SearchModels(const std::string& query) const;

  /**
   * Search and apply an optional exact tag filter using the in-memory indexes.
   */
  std::vector<std::shared_ptr<NAMLibraryTreeNode>> FilterModels(
    const std::string& query, const std::string& exactTag) const;

  /**
   * Build a shallow, flat result tree. Keeping results flat avoids rebuilding
   * and expanding a large ancestor hierarchy on every keystroke.
   */
  std::shared_ptr<NAMLibraryTreeNode> BuildSearchResultRoot(
    const std::vector<std::shared_ptr<NAMLibraryTreeNode>>& models) const;

  /**
   * Build an alphabetically sorted grouped view of a filtered model set.
   */
  std::shared_ptr<NAMLibraryTreeNode> BuildGroupedResultRoot(
    const std::vector<std::shared_ptr<NAMLibraryTreeNode>>& models,
    NAMLibraryGroupBy groupBy) const;

  /**
   * Check if model path is valid and accessible
   */
  bool IsModelPathValid(const std::shared_ptr<NAMLibraryTreeNode>& model) const;

private:
  std::shared_ptr<NAMLibraryTreeNode> mRootNode;
  std::vector<std::shared_ptr<NAMLibraryTreeNode>> mAllModels;  // Flattened for searching
  std::vector<std::string> mAllTags;

  // Prefix-search cache: when the user types another character, search the
  // previous result set instead of the entire library.
  mutable std::string mCachedQueryNormalized;
  mutable std::vector<std::shared_ptr<NAMLibraryTreeNode>> mCachedQueryResults;

  /**
   * Recursively process JSON tree and build node hierarchy
   */
  std::shared_ptr<NAMLibraryTreeNode> BuildNodeFromJson(const void* jsonNode,
                                                         std::shared_ptr<NAMLibraryTreeNode> parent,
                                                         int depth);

  /**
   * Normalize text once for fast, case-insensitive searching.
   */
  static std::string NormalizeForSearch(const std::string& text);
};
