#pragma once

#include "NAMLibraryTreeNode.h"
#include <string>
#include <vector>
#include <memory>

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
   * Search models by name, tags, or metadata (returns flattened results)
   */
  std::vector<std::shared_ptr<NAMLibraryTreeNode>> SearchModels(const std::string& query) const;

  /**
   * Check if model path is valid and accessible
   */
  bool IsModelPathValid(const std::shared_ptr<NAMLibraryTreeNode>& model) const;

private:
  std::shared_ptr<NAMLibraryTreeNode> mRootNode;
  std::vector<std::shared_ptr<NAMLibraryTreeNode>> mAllModels;  // Flattened for searching

  /**
   * Recursively process JSON tree and build node hierarchy
   */
  std::shared_ptr<NAMLibraryTreeNode> BuildNodeFromJson(const void* jsonNode, 
                                                         std::shared_ptr<NAMLibraryTreeNode> parent,
                                                         int depth);

  /**
   * Recursively flatten all models for search index
   */
  void FlattenModels(const std::shared_ptr<NAMLibraryTreeNode>& node);

  /**
   * Case-insensitive string search
   */
  static bool ContainsIgnoreCase(const std::string& haystack, const std::string& needle);
};