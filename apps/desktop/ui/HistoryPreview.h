#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <utility>

namespace ccs::ui {
// One disk read at a time. Selection changes immediately; only the latest
// result is published.
class HistoryPreview {
public:
  using Loader = std::function<std::string(const std::filesystem::path &)>;
  std::string text, error;
  bool loading = false;

  void Select(std::filesystem::path path, Loader loader) {
    selected_ = std::move(path);
    loader_ = std::move(loader);
    ++revision_;
    text.clear();
    error.clear();
    loading = true;
    if (!task_.valid())
      Start();
  }

  void Poll() {
    if (!task_.valid() ||
        task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return;
    try {
      auto result = task_.get();
      if (running_revision_ == revision_)
        text = std::move(result);
    } catch (const std::exception &e) {
      if (running_revision_ == revision_)
        error = e.what();
    }
    if (running_revision_ != revision_)
      Start();
    else
      loading = false;
  }

private:
  void Start() {
    running_revision_ = revision_;
    task_ =
        std::async(std::launch::async, [path = selected_, loader = loader_] {
          return loader(path);
        });
  }
  std::future<std::string> task_;
  std::filesystem::path selected_;
  Loader loader_;
  uint64_t revision_ = 0, running_revision_ = 0;
};
} // namespace ccs::ui
