#pragma once

#include <string>
#include <vector>
#include <memory>

/**
 * Represents a node in the library hierarchy (folder or model)
 * A node with path ending in .nam is a leaf (model)
 * A node without path is a folder (can have children)
 */
struct NAMLibraryTreeNode
{
  std::string id;
  std::string name;
  std::string path;  // Empty for folders, contains .nam path for models
  std::vector<std::string> tags;
  
  // Metadata (populated for models)
  std::string metadataName;
  std::string modeled_by;
  std::string gear_type;
  std::string gear_make;
  std::string gear_model;
  std::string tone_type;
  double loudness = 0.0;
  double gain = 0.0;
  double input_level_dbu = 0.0;
  double output_level_dbu = 0.0;
  double validation_esr = 0.0;
  
  // Tree structure
  std::vector<std::shared_ptr<NAMLibraryTreeNode>> children;
  std::shared_ptr<NAMLibraryTreeNode> parent;
  bool expanded = true;
  int depth = 0;
  
  // Helpers
  bool IsModel() const
  {
    if (path.empty()) return false;
    if (path.size() > 4 && path.substr(path.size() - 4) == ".nam") return true;
    if (path.size() > 5 && path.substr(path.size() - 5) == ".pnam") return true;
    return false;
  }
  bool IsFolder() const { return path.empty(); }
  std::string GetDisplayName() const
  {
    if (IsModel())
    {
      return name + " (" + gear_make + " " + gear_model + ")";
    }
    return name;
  }
};