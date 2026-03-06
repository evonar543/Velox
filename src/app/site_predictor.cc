#include "app/site_predictor.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

#include "include/cef_parser.h"
#include "platform/win/file_utils.h"
#include "platform/win/logger.h"

namespace velox::app {

namespace {

std::wstring ToLower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
    return static_cast<wchar_t>(std::towlower(ch));
  });
  return value;
}

std::wstring ExtractHost(std::wstring value) {
  if (value.empty()) {
    return {};
  }

  CefURLParts parts;
  if (value.find(L"://") != std::wstring::npos && CefParseURL(value, parts) && parts.host.length > 0) {
    return ToLower(CefString(parts.host.str, parts.host.length).ToWString());
  }

  const size_t slash = value.find(L'/');
  if (slash != std::wstring::npos) {
    value.resize(slash);
  }
  const size_t colon = value.find(L':');
  if (colon != std::wstring::npos) {
    value.resize(colon);
  }
  while (!value.empty() && value.back() == L'.') {
    value.pop_back();
  }
  return ToLower(value);
}

std::uint64_t UnixNowSeconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

bool ResolveHostOnce(const std::wstring& host) {
  ADDRINFOW hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  ADDRINFOW* results = nullptr;
  const int status = GetAddrInfoW(host.c_str(), nullptr, &hints, &results);
  if (results != nullptr) {
    FreeAddrInfoW(results);
  }
  return status == 0;
}

}  // namespace

SitePredictor::SitePredictor(const settings::AppSettings& settings, profiling::MetricsRecorder* metrics)
    : settings_(settings), metrics_(metrics), state_path_(settings.paths.profile_dir / L"predictor-state.txt") {}

SitePredictor::~SitePredictor() {
  Shutdown();
}

void SitePredictor::Start() {
  if (started_) {
    return;
  }
  started_ = true;

  platform::EnsureDirectory(settings_.paths.profile_dir);
  LoadState();

  if (!settings_.optimization.predictive_warmup) {
    return;
  }

  std::vector<std::wstring> warm_hosts = CollectWarmHosts();
  const std::wstring startup_host = ExtractHost(settings_.startup_url);
  if (!startup_host.empty() &&
      std::find(warm_hosts.begin(), warm_hosts.end(), startup_host) == warm_hosts.end()) {
    warm_hosts.insert(warm_hosts.begin(), startup_host);
  }

  const size_t max_hosts = static_cast<size_t>(settings_.optimization.predictor_host_count);
  if (warm_hosts.size() > max_hosts) {
    warm_hosts.resize(max_hosts);
  }

  if (metrics_ != nullptr) {
    metrics_->RecordNumeric("predictor.warm_hosts", static_cast<double>(warm_hosts.size()));
  }

  if (warm_hosts.empty()) {
    return;
  }

  warmup_thread_ = std::thread([this, hosts = std::move(warm_hosts)]() mutable {
    WarmHosts(std::move(hosts));
  });
}

void SitePredictor::RecordNavigation(const std::wstring& url_or_host) {
  const std::wstring host = ExtractHost(url_or_host);
  if (host.empty()) {
    return;
  }

  const std::uint64_t now = UnixNowSeconds();
  std::scoped_lock lock(mutex_);
  HostEntry& entry = host_entries_[host];
  const bool burst_repeat = entry.last_seen_epoch != 0 && now > entry.last_seen_epoch && (now - entry.last_seen_epoch) < 15;
  entry.score = std::min(512, entry.score + (burst_repeat ? 1 : 4));
  entry.last_seen_epoch = now;
}

void SitePredictor::Shutdown() {
  shutdown_requested_.store(true);
  if (warmup_thread_.joinable()) {
    warmup_thread_.join();
  }
  if (started_) {
    SaveState();
    started_ = false;
  }
}

void SitePredictor::LoadState() {
  std::ifstream stream(state_path_);
  if (!stream.is_open()) {
    return;
  }

  std::unordered_map<std::wstring, HostEntry> loaded_entries;
  std::string line;
  while (std::getline(stream, line)) {
    std::stringstream row(line);
    std::string host_utf8;
    std::string score_text;
    std::string last_seen_text;
    if (!std::getline(row, host_utf8, '\t') || !std::getline(row, score_text, '\t') || !std::getline(row, last_seen_text)) {
      continue;
    }

    HostEntry entry;
    try {
      entry.score = std::max(0, std::stoi(score_text));
      entry.last_seen_epoch = static_cast<std::uint64_t>(std::stoull(last_seen_text));
    } catch (...) {
      continue;
    }

    const std::wstring host = ExtractHost(platform::ToWide(host_utf8));
    if (!host.empty()) {
      loaded_entries.emplace(host, entry);
    }
  }

  std::scoped_lock lock(mutex_);
  host_entries_ = std::move(loaded_entries);
}

void SitePredictor::SaveState() {
  struct PersistedHost {
    std::wstring host;
    HostEntry entry;
  };

  std::vector<PersistedHost> snapshot;
  {
    std::scoped_lock lock(mutex_);
    snapshot.reserve(host_entries_.size());
    for (const auto& [host, entry] : host_entries_) {
      if (host.empty() || entry.score <= 0) {
        continue;
      }
      snapshot.push_back(PersistedHost{host, entry});
    }
  }

  std::sort(snapshot.begin(), snapshot.end(), [](const PersistedHost& left, const PersistedHost& right) {
    if (left.entry.score != right.entry.score) {
      return left.entry.score > right.entry.score;
    }
    return left.entry.last_seen_epoch > right.entry.last_seen_epoch;
  });
  if (snapshot.size() > 64) {
    snapshot.resize(64);
  }

  platform::EnsureDirectory(state_path_.parent_path());
  std::ofstream stream(state_path_, std::ios::out | std::ios::trunc);
  if (!stream.is_open()) {
    return;
  }

  for (const auto& item : snapshot) {
    stream << platform::ToUtf8(item.host) << '\t' << item.entry.score << '\t' << item.entry.last_seen_epoch << '\n';
  }
}

std::vector<std::wstring> SitePredictor::CollectWarmHosts() const {
  struct RankedHost {
    std::wstring host;
    HostEntry entry;
  };

  std::vector<RankedHost> ranked_hosts;
  {
    std::scoped_lock lock(mutex_);
    ranked_hosts.reserve(host_entries_.size());
    for (const auto& [host, entry] : host_entries_) {
      if (host.empty() || entry.score <= 0) {
        continue;
      }
      ranked_hosts.push_back(RankedHost{host, entry});
    }
  }

  std::sort(ranked_hosts.begin(), ranked_hosts.end(), [](const RankedHost& left, const RankedHost& right) {
    if (left.entry.score != right.entry.score) {
      return left.entry.score > right.entry.score;
    }
    return left.entry.last_seen_epoch > right.entry.last_seen_epoch;
  });

  std::vector<std::wstring> hosts;
  const size_t max_hosts = static_cast<size_t>(settings_.optimization.predictor_host_count);
  hosts.reserve(std::min(max_hosts, ranked_hosts.size()));
  for (const auto& ranked_host : ranked_hosts) {
    if (hosts.size() >= max_hosts) {
      break;
    }
    hosts.push_back(ranked_host.host);
  }
  return hosts;
}

void SitePredictor::WarmHosts(std::vector<std::wstring> hosts) {
  WSADATA wsa_data{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    return;
  }

  for (const auto& host : hosts) {
    if (shutdown_requested_.load() || host.empty()) {
      break;
    }

    const auto begin = std::chrono::steady_clock::now();
    const bool resolved = ResolveHostOnce(host);
    const auto end = std::chrono::steady_clock::now();
    const double duration_ms =
        std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() / 1000.0;

    if (metrics_ != nullptr) {
      metrics_->RecordText("predictor.host", platform::ToUtf8(host));
      metrics_->RecordNumeric(resolved ? "predictor.resolve_ms" : "predictor.resolve_failed_ms", duration_ms);
    }
  }

  WSACleanup();
}

}  // namespace velox::app
