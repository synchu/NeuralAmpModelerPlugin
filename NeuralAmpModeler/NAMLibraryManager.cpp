#include "NAMLibraryManager.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstdio>
#if defined(_WIN32)
  #include <Windows.h>
#endif
#include "json.hpp"

using json = nlohmann::json;

#if defined(_WIN32) && defined(_DEBUG)
  #define NAM_LIBRARY_LOGA(msg) OutputDebugStringA(msg)
#else
  #define NAM_LIBRARY_LOGA(msg) ((void)0)
#endif

NAMLibraryManager::NAMLibraryManager()
{
}

NAMLibraryManager::~NAMLibraryManager()
{
}

bool NAMLibraryManager::LoadMetadata(const std::string& jsonFilePath)
{
  NAM_LIBRARY_LOGA("NAMLibraryManager::LoadMetadata() ENTER\n");

  std::ifstream file(jsonFilePath);
  if (!file.is_open())
  {
    char msg[512];
    std::snprintf(msg, sizeof(msg), "NAMLibraryManager: Cannot open file: %s\n", jsonFilePath.c_str());
    NAM_LIBRARY_LOGA(msg);
    return false;
  }

  NAM_LIBRARY_LOGA("NAMLibraryManager: File opened successfully\n");

  try
  {
    NAM_LIBRARY_LOGA("NAMLibraryManager: About to parse JSON\n");
    json data;
    file >> data;

    NAM_LIBRARY_LOGA("NAMLibraryManager: JSON parsed successfully\n");

    // Create root node
    mRootNode = std::make_shared<NAMLibraryTreeNode>();
    mRootNode->name = "Library";
    mRootNode->id = "root";
    mRootNode->path = "";
    mRootNode->depth = 0;
    mRootNode->expanded = true;

    // Process items array
    if (data.contains("items") && data["items"].is_array())
    {
      char msg[256];
      std::snprintf(msg, sizeof(msg), "NAMLibraryManager: Found %zu top-level items\n", data["items"].size());
      NAM_LIBRARY_LOGA(msg);

      for (const auto& topItem : data["items"])
      {
        auto node = BuildNodeFromJson(&topItem, mRootNode, 1);
        if (node)
        {
          mRootNode->children.push_back(node);
#if defined(_WIN32) && defined(_DEBUG)
          std::snprintf(msg, sizeof(msg), "NAMLibraryManager: Added node: %s\n", node->name.c_str());
          NAM_LIBRARY_LOGA(msg);
#endif
        }
      }
    }
    else
    {
      NAM_LIBRARY_LOGA("NAMLibraryManager: No 'items' array found in JSON\n");
    }

    // Build flattened list for searching
    mAllModels.clear();
    FlattenModels(mRootNode);

    char msg[256];
    std::snprintf(msg, sizeof(msg), "NAMLibraryManager: Total models found: %zu\n", mAllModels.size());
    NAM_LIBRARY_LOGA(msg);

    return true;
  }
  catch (const std::exception& e)
  {
    char msg[512];
    std::snprintf(msg, sizeof(msg), "NAMLibraryManager: Exception during parsing: %s\n", e.what());
    NAM_LIBRARY_LOGA(msg);
    return false;
  }
}

std::shared_ptr<NAMLibraryTreeNode> NAMLibraryManager::BuildNodeFromJson(
  const void* jsonNodePtr,
  std::shared_ptr<NAMLibraryTreeNode> parent,
  int depth)
{
  auto* pJsonNode = static_cast<const json*>(jsonNodePtr);
  if (!pJsonNode || !pJsonNode->is_object())
    return nullptr;

  auto node = std::make_shared<NAMLibraryTreeNode>();
  node->parent = parent;
  node->depth = depth;
  node->expanded = pJsonNode->value("expanded", true);
  
  // Helper lambdas for null-safe extraction
  auto getString = [](const json& j, const char* key, const std::string& def = "") -> std::string {
    return (j.contains(key) && !j[key].is_null() && j[key].is_string()) ? j[key].get<std::string>() : def;
  };
  
  auto getNumber = [](const json& j, const char* key, double def) -> double {
    return (j.contains(key) && !j[key].is_null() && j[key].is_number()) ? j[key].get<double>() : def;
  };
  
  // Extract string fields
  node->id = getString(*pJsonNode, "id");
  node->name = getString(*pJsonNode, "name");
  node->path = getString(*pJsonNode, "path");
  node->metadataName = getString(*pJsonNode, "metadataName");
  node->modeled_by = getString(*pJsonNode, "modeled_by");
  node->gear_type = getString(*pJsonNode, "gear_type");
  node->gear_make = getString(*pJsonNode, "gear_make");
  node->gear_model = getString(*pJsonNode, "gear_model");
  node->tone_type = getString(*pJsonNode, "tone_type");
  
  // Extract tags array
  node->tags = pJsonNode->value("tags", std::vector<std::string>());
  
  // Extract numeric fields
  node->loudness = getNumber(*pJsonNode, "loudness", 0.0);
  node->gain = getNumber(*pJsonNode, "gain", 0.0);
  node->input_level_dbu = getNumber(*pJsonNode, "input_level_dbu", 0.0);
  node->output_level_dbu = getNumber(*pJsonNode, "output_level_dbu", 0.0);
  node->validation_esr = getNumber(*pJsonNode, "validation_esr", 0.0);

  // Process children recursively
  if (pJsonNode->contains("children") && (*pJsonNode)["children"].is_array())
  {
    for (const auto& child : (*pJsonNode)["children"])
    {
      auto childNode = BuildNodeFromJson(&child, node, depth + 1);
      if (childNode)
      {
        node->children.push_back(childNode);
      }
    }
  }

  return node;
}

void NAMLibraryManager::FlattenModels(const std::shared_ptr<NAMLibraryTreeNode>& node)
{
  if (!node)
    return;

  if (node->IsModel())
  {
    mAllModels.push_back(node);
  }

  for (const auto& child : node->children)
  {
    FlattenModels(child);
  }
}

std::vector<std::shared_ptr<NAMLibraryTreeNode>> NAMLibraryManager::SearchModels(const std::string& query) const
{
  std::vector<std::shared_ptr<NAMLibraryTreeNode>> results;

  if (query.empty())
    return mAllModels;

  for (const auto& model : mAllModels)
  {
    if (ContainsIgnoreCase(model->name, query))
    {
      results.push_back(model);
      continue;
    }

    if (ContainsIgnoreCase(model->metadataName, query))
    {
      results.push_back(model);
      continue;
    }

    if (ContainsIgnoreCase(model->gear_make, query) || ContainsIgnoreCase(model->gear_model, query))
    {
      results.push_back(model);
      continue;
    }

    if (ContainsIgnoreCase(model->tone_type, query))
    {
      results.push_back(model);
      continue;
    }

    for (const auto& tag : model->tags)
    {
      if (ContainsIgnoreCase(tag, query))
      {
        results.push_back(model);
        break;
      }
    }
  }

  return results;
}

bool NAMLibraryManager::IsModelPathValid(const std::shared_ptr<NAMLibraryTreeNode>& model) const
{
  if (!model || !model->IsModel())
    return false;

  std::ifstream file(model->path);
  return file.good();
}

bool NAMLibraryManager::ContainsIgnoreCase(const std::string& haystack, const std::string& needle)
{
  std::string lowerHaystack = haystack;
  std::string lowerNeedle = needle;

  std::transform(lowerHaystack.begin(), lowerHaystack.end(), lowerHaystack.begin(), ::tolower);
  std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(), ::tolower);

  return lowerHaystack.find(lowerNeedle) != std::string::npos;
}