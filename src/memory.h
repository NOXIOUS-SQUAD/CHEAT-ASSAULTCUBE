#pragma once

#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class MemStatus
{
    Idle = 0,
    Searching,
    Ready,
    Writing,
    OpenFailed,
    WriteFailed
};

struct TargetSpec
{
    DWORD_PTR address = 0;
    bool guarded = false;
};

struct TargetState
{
    DWORD_PTR address = 0;
    bool guarded = false;
    bool enabled = true;
    bool valid = true;
    int lastValue = 0;
    unsigned long long writes = 0;
};

class MemoryWriter
{
public:
    MemoryWriter() = default;
    ~MemoryWriter();

    MemoryWriter(const MemoryWriter&) = delete;
    MemoryWriter& operator=(const MemoryWriter&) = delete;

    void Configure(const std::wstring& exeName, const std::vector<TargetSpec>& targets);
    void Start();
    void Stop();
    void SetValue(int value);
    void SetLock(bool enabled);
    void RequestAttach();
    void SetTargetEnabled(size_t index, bool enabled);
    void Revalidate();

    bool Attached() const { return attached_.load(); }
    bool Alive() const;
    DWORD Pid() const { return pid_.load(); }
    unsigned long long Writes() const { return writes_.load(); }
    const std::wstring& ExeName() const { return exeName_; }
    int Value() const { return value_.load(); }
    MemStatus Status() const { return static_cast<MemStatus>(status_.load()); }
    DWORD ErrorCode() const { return errorCode_.load(); }
    DWORD_PTR PrimaryAddress() const;
    std::vector<TargetState> Snapshot() const;

private:
    struct Entry
    {
        TargetSpec spec;
        bool enabled = true;
        bool valid = true;
        int lastValue = 0;
        unsigned long long writes = 0;
    };

    void Loop();
    bool Attach();
    void Detach();

    std::wstring exeName_ = L"ac_client.exe";
    std::vector<Entry> targets_;
    mutable std::mutex mutex_;
    HANDLE handle_ = nullptr;
    std::thread worker_;

    std::atomic<bool> running_{ false };
    std::atomic<bool> attached_{ false };
    std::atomic<bool> lock_{ true };
    std::atomic<bool> attachRequest_{ false };
    std::atomic<bool> revalidate_{ false };
    std::atomic<int> value_{ 20 };
    std::atomic<DWORD> pid_{ 0 };
    std::atomic<int> status_{ static_cast<int>(MemStatus::Idle) };
    std::atomic<DWORD> errorCode_{ 0 };
    std::atomic<unsigned long long> writes_{ 0 };
};
