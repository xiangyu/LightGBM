#include <LightGBM/tree.h>

#include <LightGBM/utils/threading.h>
#include <LightGBM/utils/common.h>

#include <LightGBM/dataset.h>
#include <LightGBM/feature.h>

#include <sstream>
#include <unordered_map>
#include <functional>
#include <vector>
#include <string>
#include <memory>

namespace LightGBM {

Tree::Tree(int max_leaves)
  :max_leaves_(max_leaves) {

  num_leaves_ = 0;
  left_child_ = std::vector<int>(max_leaves_ - 1);
  right_child_ = std::vector<int>(max_leaves_ - 1);
  split_feature_ = std::vector<int>(max_leaves_ - 1);
  split_feature_real_ = std::vector<int>(max_leaves_ - 1);
  threshold_in_bin_ = std::vector<unsigned int>(max_leaves_ - 1);
  threshold_ = std::vector<double>(max_leaves_ - 1);
  decision_type_ = std::vector<uint8_t>(max_leaves_ - 1);
  split_gain_ = std::vector<double>(max_leaves_ - 1);
  leaf_parent_ = std::vector<int>(max_leaves_);
  leaf_value_ = std::vector<double>(max_leaves_);
  leaf_count_ = std::vector<data_size_t>(max_leaves_);
  internal_value_ = std::vector<double>(max_leaves_ - 1);
  internal_count_ = std::vector<data_size_t>(max_leaves_ - 1);
  leaf_depth_ = std::vector<int>(max_leaves_);
  // root is in the depth 1
  leaf_depth_[0] = 1;
  num_leaves_ = 1;
  leaf_parent_[0] = -1;
}
Tree::~Tree() {

}

int Tree::Split(int leaf, int feature, BinType bin_type, unsigned int threshold_bin, int real_feature,
    double threshold_double, double left_value,
    double right_value, data_size_t left_cnt, data_size_t right_cnt, double gain) {
  int new_node_idx = num_leaves_ - 1;
  // update parent info
  int parent = leaf_parent_[leaf];
  if (parent >= 0) {
    // if cur node is left child
    if (left_child_[parent] == ~leaf) {
      left_child_[parent] = new_node_idx;
    } else {
      right_child_[parent] = new_node_idx;
    }
  }
  internal_count_[parent] = left_cnt + right_cnt;
  // add new node
  split_feature_[new_node_idx] = feature;
  split_feature_real_[new_node_idx] = real_feature;
  threshold_in_bin_[new_node_idx] = threshold_bin;
  threshold_[new_node_idx] = threshold_double;
  if (bin_type == BinType::NumericalBin) {
    decision_type_[new_node_idx] = 0;
  } else {
    decision_type_[new_node_idx] = 1;
  }
  split_gain_[new_node_idx] = gain;
  // add two new leaves
  left_child_[new_node_idx] = ~leaf;
  right_child_[new_node_idx] = ~num_leaves_;
  // update new leaves
  leaf_parent_[leaf] = new_node_idx;
  leaf_parent_[num_leaves_] = new_node_idx;
  // save current leaf value to internal node before change
  internal_value_[new_node_idx] = leaf_value_[leaf];
  internal_count_[new_node_idx] = leaf_count_[leaf];
  leaf_value_[leaf] = left_value;
  leaf_count_[leaf] = left_cnt;
  leaf_value_[num_leaves_] = right_value;
  leaf_count_[num_leaves_] = right_cnt;
  // update leaf depth
  leaf_depth_[num_leaves_] = leaf_depth_[leaf] + 1;
  leaf_depth_[leaf]++;

  ++num_leaves_;
  return num_leaves_ - 1;
}

void Tree::AddPredictionToScore(const Dataset* data, data_size_t num_data, score_t* score) const {
  Threading::For<data_size_t>(0, num_data, [this, data, score](int, data_size_t start, data_size_t end) {
    std::vector<std::unique_ptr<BinIterator>> iterators(data->num_features());
    for (int i = 0; i < data->num_features(); ++i) {
      iterators[i].reset(data->FeatureAt(i)->bin_data()->GetIterator(start));
    }
    for (data_size_t i = start; i < end; ++i) {
      score[i] += static_cast<score_t>(leaf_value_[GetLeaf(iterators, i)]);
    }
  });
}

void Tree::AddPredictionToScore(const Dataset* data, const data_size_t* used_data_indices,
                                             data_size_t num_data, score_t* score) const {
  Threading::For<data_size_t>(0, num_data,
      [this, data, used_data_indices, score](int, data_size_t start, data_size_t end) {
    std::vector<std::unique_ptr<BinIterator>> iterators(data->num_features());
    for (int i = 0; i < data->num_features(); ++i) {
      iterators[i].reset(data->FeatureAt(i)->bin_data()->GetIterator(used_data_indices[start]));
    }
    for (data_size_t i = start; i < end; ++i) {
      score[used_data_indices[i]] += static_cast<score_t>(leaf_value_[GetLeaf(iterators, used_data_indices[i])]);
    }
  });
}

std::string Tree::ToString() {
  std::stringstream ss;
  ss << "num_leaves=" << num_leaves_ << std::endl;
  ss << "split_feature="
    << Common::ArrayToString<int>(split_feature_real_.data(), num_leaves_ - 1, ' ') << std::endl;
  ss << "split_gain="
    << Common::ArrayToString<double>(split_gain_.data(), num_leaves_ - 1, ' ') << std::endl;
  ss << "threshold="
    << Common::ArrayToString<double>(threshold_.data(), num_leaves_ - 1, ' ') << std::endl;
  ss << "decision_type="
    << Common::ArrayToString<uint8_t>(decision_type_.data(), num_leaves_ - 1, ' ') << std::endl;
  ss << "left_child="
    << Common::ArrayToString<int>(left_child_.data(), num_leaves_ - 1, ' ') << std::endl;
  ss << "right_child="
    << Common::ArrayToString<int>(right_child_.data(), num_leaves_ - 1, ' ') << std::endl;
  ss << "leaf_parent="
    << Common::ArrayToString<int>(leaf_parent_.data(), num_leaves_, ' ') << std::endl;
  ss << "leaf_value="
    << Common::ArrayToString<double>(leaf_value_.data(), num_leaves_, ' ') << std::endl;
  ss << "leaf_count="
    << Common::ArrayToString<data_size_t>(leaf_count_.data(), num_leaves_, ' ') << std::endl;
  ss << "internal_value="
    << Common::ArrayToString<double>(internal_value_.data(), num_leaves_ - 1, ' ') << std::endl;
  ss << "internal_count="
    << Common::ArrayToString<data_size_t>(internal_count_.data(), num_leaves_ - 1, ' ') << std::endl;
  ss << std::endl;
  return ss.str();
}

Tree::Tree(const std::string& str) {
  std::vector<std::string> lines = Common::Split(str.c_str(), '\n');
  std::unordered_map<std::string, std::string> key_vals;
  for (const std::string& line : lines) {
    std::vector<std::string> tmp_strs = Common::Split(line.c_str(), '=');
    if (tmp_strs.size() == 2) {
      std::string key = Common::Trim(tmp_strs[0]);
      std::string val = Common::Trim(tmp_strs[1]);
      if (key.size() > 0 && val.size() > 0) {
        key_vals[key] = val;
      }
    }
  }
  if (key_vals.count("num_leaves") <= 0 || key_vals.count("split_feature") <= 0
    || key_vals.count("split_gain") <= 0 || key_vals.count("threshold") <= 0
    || key_vals.count("left_child") <= 0 || key_vals.count("right_child") <= 0
    || key_vals.count("leaf_parent") <= 0 || key_vals.count("leaf_value") <= 0
    || key_vals.count("internal_value") <= 0 || key_vals.count("internal_count") <= 0
    || key_vals.count("leaf_count") <= 0 || key_vals.count("decision_type") <= 0
    ) {
    Log::Fatal("Tree model string format error");
  }

  Common::Atoi(key_vals["num_leaves"].c_str(), &num_leaves_);

  left_child_ = Common::StringToIntArray<int>(key_vals["left_child"], ' ', num_leaves_ - 1);
  right_child_ = Common::StringToIntArray<int>(key_vals["right_child"], ' ', num_leaves_ - 1);
  split_feature_real_ = Common::StringToIntArray<int>(key_vals["split_feature"], ' ', num_leaves_ - 1);
  threshold_ = Common::StringToIntArray<double>(key_vals["threshold"], ' ', num_leaves_ - 1);
  split_gain_ = Common::StringToIntArray<double>(key_vals["split_gain"], ' ', num_leaves_ - 1);
  internal_count_ = Common::StringToIntArray<data_size_t>(key_vals["internal_count"], ' ', num_leaves_ - 1);
  internal_value_ = Common::StringToIntArray<double>(key_vals["internal_value"], ' ', num_leaves_ - 1);
  decision_type_ = Common::StringToIntArray<uint8_t>(key_vals["decision_type"], ' ', num_leaves_ - 1);

  leaf_count_ = Common::StringToIntArray<data_size_t>(key_vals["leaf_count"], ' ', num_leaves_);
  leaf_parent_ = Common::StringToIntArray<int>(key_vals["leaf_parent"], ' ', num_leaves_);
  leaf_value_ = Common::StringToIntArray<double>(key_vals["leaf_value"], ' ', num_leaves_);



}

}  // namespace LightGBM
