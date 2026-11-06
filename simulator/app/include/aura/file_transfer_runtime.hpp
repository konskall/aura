#pragma once
#include "aura/file_transfer.hpp"
#include <atomic>
#include <filesystem>
#include <thread>

namespace aura::simulator {
class DesktopTransferStorage final : public ITransferStorage {
public:
    explicit DesktopTransferStorage(std::filesystem::path root) : root_(std::move(root)) {}
    ~DesktopTransferStorage() override;
    TransferError begin(TransferArea area, const std::string& name) override;
    bool write(const std::uint8_t* bytes, std::size_t size) override;
    bool seal() override;
    bool read_staged(PhotoBytes& bytes) override;
    TransferError commit() override;
    void cancel() override;
    TransferError list(TransferArea area, const std::string& after, std::vector<TransferFile>& files,
                       bool& more) override;
    TransferError erase(TransferArea area, const std::string& name) override;

private:
    std::filesystem::path root_, stage_, destination_;
    void* handle_{nullptr};
    void* lock_{nullptr};
    bool owned_{false};
};
class FileTransferRuntime {
public:
    // `aoraton` serves POST /api/aoraton/* (the device's PC-assisted sign-in);
    // the simulator signs in by itself, so only the helper's test passes one.
    FileTransferRuntime(const std::string& root, bool deterministic, std::string bind_address = {},
                        std::uint16_t port = 0, IAoratonLinkEndpoint* aoraton = nullptr);
    ~FileTransferRuntime();
    bool start();
    void stop();
    bool open_browser();
    void poll();
    TransferSession& session() { return session_; }

private:
    void run();
    TransferSession session_;
    DesktopTransferStorage storage_;
    bool deterministic_{false}, has_root_{false};
    std::string bind_address_;
    std::uint16_t port_{0};
    IAoratonLinkEndpoint* aoraton_{nullptr};
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};
}  // namespace aura::simulator
