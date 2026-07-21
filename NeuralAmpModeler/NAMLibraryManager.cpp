#include "NAMLibraryManager.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
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

  std::ifstream file(jsonFilePath, std::ios::binary);
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
    file.seekg(0, std::ios::end);
    const auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string jsonText;
    if (fileSize > 0)
      jsonText.reserve(static_cast<size_t>(fileSize));
    jsonText.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    json data = json::parse(jsonText);

    NAM_LIBRARY_LOGA("NAMLibraryManager: JSON parsed successfully\n");

    // Create root node
    mRootNode = std::make_shared<NAMLibraryTreeNode>();
    mRootNode->name = "Library";
    mRootNode->id = "root";
    mRootNode->path = "";
    mRootNode->depth = 0;
    mRootNode->expanded = true;
    mRootNode->displayLabel = mRootNode->name;

    mAllModels.clear();
    mAllTags.clear();
    mCachedQueryNormalized.clear();
    mCachedQueryResults.clear();

    // Process items array
    if (data.contains("items") && data["items"].is_array())
    {
      char msg[256];
      std::snprintf(msg, sizeof(msg), "NAMLibraryManager: Found %zu top-level items\n", data["items"].size());
      NAM_LIBRARY_LOGA(msg);

      mRootNode->children.reserve(data["items"].size());
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

    // Tags are indexed once, instead of being rediscovered every time the
    // browser opens or the search query changes.
    mAllTags = CollectTags(mAllModels);

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
  
  // Extract tags array without allowing malformed data to abort the load.
  if (pJsonNode->contains("tags") && (*pJsonNode)["tags"].is_array())
  {
    for (const auto& tag : (*pJsonNode)["tags"])
    {
      if (tag.is_string())
        node->tags.push_back(tag.get<std::string>());
    }
  }
  
  // Extract numeric fields
  node->loudness = getNumber(*pJsonNode, "loudness", 0.0);
  node->gain = getNumber(*pJsonNode, "gain", 0.0);
  node->input_level_dbu = getNumber(*pJsonNode, "input_level_dbu", 0.0);
  node->output_level_dbu = getNumber(*pJsonNode, "output_level_dbu", 0.0);
  node->validation_esr = getNumber(*pJsonNode, "validation_esr", 0.0);

  if (parent && parent->id != "root")
  {
    node->breadcrumb = parent->breadcrumb;
    if (!node->breadcrumb.empty())
      node->breadcrumb += " / ";
    node->breadcrumb += parent->name;
  }

  node->displayLabel = node->name;
  if (node->IsModel())
  {
    std::vector<std::string> metadata;
    if (!node->gear_make.empty() || !node->gear_model.empty())
    {
      std::string gear = node->gear_make;
      if (!gear.empty() && !node->gear_model.empty())
        gear += " ";
      gear += node->gear_model;
      if (!gear.empty())
        metadata.push_back(std::move(gear));
    }

    auto addLevel = [&](const char* label, double value) {
      if (value == 0.0)
        return;
      char buffer[32] = {};
      std::snprintf(buffer, sizeof(buffer), "%s: %.1f", label, value);
      metadata.emplace_back(buffer);
    };
    addLevel("in", node->input_level_dbu);
    addLevel("out", node->output_level_dbu);

    if (!metadata.empty())
    {
      node->displayLabel += " [";
      for (size_t i = 0; i < metadata.size(); ++i)
      {
        if (i > 0)
          node->displayLabel += ", ";
        node->displayLabel += metadata[i];
      }
      node->displayLabel += "]";
    }
  }

  // PresetManager searches every scalar field. Mirror that behavior, but
  // normalize it once at load time instead of on every keystroke.
  std::string searchable;
  auto appendSearchPart = [&](const std::string& value) {
    const std::string normalized = NormalizeForSearch(value);
    if (normalized.empty())
      return;
    if (!searchable.empty())
      searchable.push_back(' ');
    searchable += normalized;
  };

  for (auto it = pJsonNode->begin(); it != pJsonNode->end(); ++it)
  {
    if (it.key() == "children" || it.key() == "tags" || it.value().is_null())
      continue;
    if (it.value().is_string())
      appendSearchPart(it.value().get<std::string>());
    else if (it.value().is_primitive())
      appendSearchPart(it.value().dump());
  }

  appendSearchPart(node->breadcrumb);
  node->tagsNormalized.reserve(node->tags.size());
  for (const auto& tag : node->tags)
  {
    std::string normalizedTag = NormalizeForSearch(tag);
    node->tagsNormalized.push_back(normalizedTag);
    appendSearchPart(tag);
  }
  node->searchTextNormalized = std::move(searchable);

  if (node->IsModel())
    mAllModels.push_back(node);

  // Process children recursively
  if (pJsonNode->contains("children") && (*pJsonNode)["children"].is_array())
  {
    node->children.reserve((*pJsonNode)["children"].size());
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

std::vector<std::shared_ptr<NAMLibraryTreeNode>> NAMLibraryManager::SearchModels(const std::string& query) const
{
  const std::string normalizedQuery = NormalizeForSearch(query);
  if (normalizedQuery.empty())
    return mAllModels;

  if (normalizedQuery == mCachedQueryNormalized)
    return mCachedQueryResults;

  const bool extendsCachedQuery = !mCachedQueryNormalized.empty() &&
    normalizedQuery.size() >= mCachedQueryNormalized.size() &&
    normalizedQuery.compare(0, mCachedQueryNormalized.size(), mCachedQueryNormalized) == 0;
  const auto& candidates = extendsCachedQuery ? mCachedQueryResults : mAllModels;

  std::vector<std::string> terms;
  std::istringstream termStream(normalizedQuery);
  for (std::string term; termStream >> term;)
    terms.push_back(std::move(term));

  std::vector<std::shared_ptr<NAMLibraryTreeNode>> results;
  results.reserve(candidates.size());
  for (const auto& model : candidates)
  {
    if (!model)
      continue;

    bool matches = true;
    for (const auto& term : terms)
    {
      if (model->searchTextNormalized.find(term) == std::string::npos)
      {
        matches = false;
        break;
      }
    }
    if (matches)
      results.push_back(model);
  }

  mCachedQueryNormalized = normalizedQuery;
  mCachedQueryResults = results;
  return results;
}

std::vector<std::shared_ptr<NAMLibraryTreeNode>> NAMLibraryManager::FilterModels(
  const std::string& query, const std::string& exactTag) const
{
  auto results = SearchModels(query);
  const std::string normalizedTag = NormalizeForSearch(exactTag);
  if (normalizedTag.empty())
    return results;

  results.erase(std::remove_if(results.begin(), results.end(), [&](const auto& model) {
    if (!model)
      return true;
    return std::find(model->tagsNormalized.begin(), model->tagsNormalized.end(), normalizedTag) ==
      model->tagsNormalized.end();
  }), results.end());
  return results;
}

std::shared_ptr<NAMLibraryTreeNode> NAMLibraryManager::BuildSearchResultRoot(
  const std::vector<std::shared_ptr<NAMLibraryTreeNode>>& models) const
{
  auto root = std::make_shared<NAMLibraryTreeNode>();
  root->name = "Filtered Results (" + std::to_string(models.size()) + " models)";
  root->displayLabel = root->name;
  root->id = "search_root";
  root->expanded = true;
  root->children.reserve(models.size());

  for (const auto& model : models)
  {
    if (!model)
      continue;
    auto copy = std::make_shared<NAMLibraryTreeNode>(*model);
    copy->children.clear();
    copy->parent = root;
    copy->depth = 1;
    copy->expanded = false;
    if (!copy->breadcrumb.empty())
      copy->displayLabel += "  -  " + copy->breadcrumb;
    root->children.push_back(std::move(copy));
  }
  return root;
}

std::shared_ptr<NAMLibraryTreeNode> NAMLibraryManager::BuildGroupedResultRoot(
  const std::vector<std::shared_ptr<NAMLibraryTreeNode>>& models,
  NAMLibraryGroupBy groupBy) const
{
  if (groupBy == NAMLibraryGroupBy::Library)
    return BuildSearchResultRoot(models);

  struct Group
  {
    std::string label;
    std::vector<std::shared_ptr<NAMLibraryTreeNode>> models;
  };

  std::map<std::string, Group> groups;
  for (const auto& model : models)
  {
    if (!model)
      continue;

    std::vector<std::string> values;
    std::string missingLabel;
    switch (groupBy)
    {
      case NAMLibraryGroupBy::GearMake:
        values.push_back(model->gear_make);
        missingLabel = "(Unspecified gear make)";
        break;
      case NAMLibraryGroupBy::GearModel:
        values.push_back(model->gear_model);
        missingLabel = "(Unspecified gear model)";
        break;
      case NAMLibraryGroupBy::ToneType:
        values.push_back(model->tone_type);
        missingLabel = "(Unspecified tone type)";
        break;
      case NAMLibraryGroupBy::Author:
        values.push_back(model->modeled_by);
        missingLabel = "(Unknown author)";
        break;
      case NAMLibraryGroupBy::Tag:
        values = model->tags;
        missingLabel = "(Untagged)";
        break;
      case NAMLibraryGroupBy::Library:
        break;
    }

    if (values.empty())
      values.push_back(missingLabel);

    std::set<std::string> groupsAddedForModel;
    for (auto value : values)
    {
      if (NormalizeForSearch(value).empty())
        value = missingLabel;
      const std::string key = NormalizeForSearch(value);
      if (key.empty() || !groupsAddedForModel.insert(key).second)
        continue;

      auto& group = groups[key];
      if (group.label.empty())
        group.label = std::move(value);
      group.models.push_back(model);
    }
  }

  auto root = std::make_shared<NAMLibraryTreeNode>();
  root->name = "Grouped Results (" + std::to_string(models.size()) + " models)";
  root->displayLabel = root->name;
  root->id = "grouped_root";
  root->expanded = true;
  root->children.reserve(groups.size());

  for (auto& entry : groups)
  {
    auto& group = entry.second;
    std::sort(group.models.begin(), group.models.end(), [&](const auto& lhs, const auto& rhs) {
      const std::string left = lhs ? NormalizeForSearch(lhs->GetDisplayName()) : std::string{};
      const std::string right = rhs ? NormalizeForSearch(rhs->GetDisplayName()) : std::string{};
      if (left != right)
        return left < right;
      return lhs && rhs ? lhs->path < rhs->path : static_cast<bool>(lhs);
    });

    auto groupNode = std::make_shared<NAMLibraryTreeNode>();
    groupNode->name = group.label;
    groupNode->displayLabel = group.label + " (" + std::to_string(group.models.size()) + ")";
    groupNode->id = "group_" + std::to_string(static_cast<int>(groupBy)) + "_" + entry.first;
    groupNode->parent = root;
    groupNode->depth = 1;
    groupNode->expanded = false;
    groupNode->children.reserve(group.models.size());

    for (const auto& model : group.models)
    {
      auto copy = std::make_shared<NAMLibraryTreeNode>(*model);
      copy->children.clear();
      copy->parent = groupNode;
      copy->depth = 2;
      copy->expanded = false;
      if (!copy->breadcrumb.empty())
        copy->displayLabel += "  -  " + copy->breadcrumb;
      groupNode->children.push_back(std::move(copy));
    }

    root->children.push_back(std::move(groupNode));
  }

  return root;
}

std::vector<std::string> NAMLibraryManager::CollectTags(
  const std::vector<std::shared_ptr<NAMLibraryTreeNode>>& models) const
{
  std::map<std::string, std::string> tagsByNormalizedName;
  for (const auto& model : models)
  {
    if (!model)
      continue;
    for (const auto& tag : model->tags)
    {
      const std::string normalized = NormalizeForSearch(tag);
      if (!normalized.empty())
        tagsByNormalizedName.emplace(normalized, tag);
    }
  }

  std::vector<std::string> tags;
  tags.reserve(tagsByNormalizedName.size());
  for (const auto& entry : tagsByNormalizedName)
    tags.push_back(entry.second);
  return tags;
}

bool NAMLibraryManager::IsModelPathValid(const std::shared_ptr<NAMLibraryTreeNode>& model) const
{
  if (!model || !model->IsModel())
    return false;

  std::ifstream file(model->path);
  return file.good();
}

std::string NAMLibraryManager::NormalizeForSearch(const std::string& text)
{
  std::string normalized;
  normalized.reserve(text.size());
  bool previousWasSpace = true;
  for (unsigned char ch : text)
  {
    if (std::isspace(ch))
    {
      if (!previousWasSpace)
        normalized.push_back(' ');
      previousWasSpace = true;
    }
    else
    {
      normalized.push_back(static_cast<char>(std::tolower(ch)));
      previousWasSpace = false;
    }
  }
  if (!normalized.empty() && normalized.back() == ' ')
    normalized.pop_back();
  return normalized;
}
