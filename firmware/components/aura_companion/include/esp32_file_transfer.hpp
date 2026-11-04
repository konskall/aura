#pragma once
#include "aura/file_transfer.hpp"
#include "esp_http_server.h"
#include <atomic>
#include <cstdio>
namespace aura::firmware {
class Esp32TransferStorage final : public ITransferStorage {
public:
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
    FILE* file_{nullptr};
    std::string destination_;
    bool owned_{false};
};
class Esp32FileTransfer {
public:
    void initialize();
    bool start();
    void stop() { session_.stop(); }
    void shutdown();  // call outside the LVGL lock before reboot/reset/deep sleep
    void poll();
    TransferSession& session() { return session_; }

private:
    static void manager_entry(void* context);
    static esp_err_t request_entry(httpd_req_t* request);
    void manage();
    esp_err_t request(httpd_req_t* request);
    TransferSession session_;
    Esp32TransferStorage storage_;
    std::atomic<bool> available_{false}, listening_{false};
};
}  // namespace aura::firmware
