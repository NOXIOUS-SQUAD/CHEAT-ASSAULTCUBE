#include "memory.h"

#include <tlhelp32.h>
#include <chrono>

namespace
{
bool EnableDebugPrivilege()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;

    LUID luid{};
    bool enabled = false;

    if (LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid))
    {
        TOKEN_PRIVILEGES privileges{};
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Luid = luid;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        enabled = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr) != FALSE;
    }

    CloseHandle(token);
    return enabled;
}

DWORD FindProcessId(const std::wstring& exeName)
{
    DWORD result = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, exeName.c_str()) == 0)
            {
                result = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return result;
}
}

MemoryWriter::~MemoryWriter()
{
    Stop();
}

void MemoryWriter::Configure(const std::wstring& exeName, const std::vector<TargetSpec>& targets)
{
    std::lock_guard<std::mutex> lock(mutex_);
    exeName_ = exeName;
    targets_.clear();
    targets_.reserve(targets.size());
    for (const TargetSpec& spec : targets)
    {
        Entry entry;
        entry.spec = spec;
        targets_.push_back(entry);
    }
}

void MemoryWriter::Start()
{
    if (running_.exchange(true))
        return;

    worker_ = std::thread(&MemoryWriter::Loop, this);
}

void MemoryWriter::Stop()
{
    if (!running_.exchange(false))
        return;

    if (worker_.joinable())
        worker_.join();

    Detach();
}

void MemoryWriter::SetValue(int value)
{
    if (value < 0)
        value = 0;
    if (value > 9999)
        value = 9999;
    value_.store(value);
}

void MemoryWriter::SetLock(bool enabled)
{
    lock_.store(enabled);
}

void MemoryWriter::RequestAttach()
{
    attachRequest_.store(true);
}

void MemoryWriter::Revalidate()
{
    revalidate_.store(true);
}

void MemoryWriter::SetTargetEnabled(size_t index, bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < targets_.size())
    {
        targets_[index].enabled = enabled;
        targets_[index].valid = true;
    }
}

DWORD_PTR MemoryWriter::PrimaryAddress() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (targets_.empty())
        return 0;
    return targets_.front().spec.address;
}

std::vector<TargetState> MemoryWriter::Snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TargetState> result;
    result.reserve(targets_.size());
    for (const Entry& entry : targets_)
    {
        TargetState state;
        state.address = entry.spec.address;
        state.guarded = entry.spec.guarded;
        state.enabled = entry.enabled;
        state.valid = entry.valid;
        state.lastValue = entry.lastValue;
        state.writes = entry.writes;
        result.push_back(state);
    }
    return result;
}

bool MemoryWriter::Alive() const
{
    if (handle_ == nullptr)
        return false;
    return WaitForSingleObject(handle_, 0) == WAIT_TIMEOUT;
}

bool MemoryWriter::Attach()
{
    const DWORD pid = FindProcessId(exeName_);
    if (pid == 0)
    {
        status_.store(static_cast<int>(MemStatus::Searching));
        return false;
    }

    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
                                 PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr)
    {
        errorCode_.store(GetLastError());
        status_.store(static_cast<int>(MemStatus::OpenFailed));
        return false;
    }

    handle_ = process;
    pid_.store(pid);
    attached_.store(true);
    status_.store(static_cast<int>(MemStatus::Ready));
    return true;
}

void MemoryWriter::Detach()
{
    if (handle_ != nullptr)
    {
        CloseHandle(handle_);
        handle_ = nullptr;
    }

    attached_.store(false);
    pid_.store(0);
    status_.store(static_cast<int>(MemStatus::Idle));
}

void MemoryWriter::Loop()
{
    EnableDebugPrivilege();

    while (running_.load())
    {
        if (attachRequest_.exchange(false) && attached_.load())
            Detach();

        if (!attached_.load())
        {
            if (!Attach())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                continue;
            }
        }

        if (!Alive())
        {
            Detach();
            continue;
        }

        if (revalidate_.exchange(false))
        {
            std::lock_guard<std::mutex> guard(mutex_);
            for (Entry& entry : targets_)
            {
                entry.valid = true;
                entry.writes = 0;
                entry.lastValue = 0;
            }
            writes_.store(0);
        }

        if (lock_.load())
        {
            bool lost = false;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                for (Entry& entry : targets_)
                {
                    if (!entry.enabled)
                        continue;

                    if (entry.spec.guarded)
                    {
                        int probe = 0;
                        SIZE_T read = 0;
                        const BOOL okRead = ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(entry.spec.address),
                                                              &probe, sizeof(probe), &read);
                        if (!okRead || read != sizeof(probe) || probe < 0 || probe > 999)
                        {
                            entry.valid = false;
                            continue;
                        }
                        entry.valid = true;
                    }

                    const int payload = value_.load();
                    SIZE_T written = 0;
                    const BOOL okWrite = WriteProcessMemory(handle_, reinterpret_cast<LPVOID>(entry.spec.address),
                                                            &payload, sizeof(payload), &written);
                    if (!okWrite || written != sizeof(payload))
                    {
                        if (entry.spec.guarded)
                        {
                            entry.valid = false;
                            continue;
                        }

                        errorCode_.store(GetLastError());
                        status_.store(static_cast<int>(MemStatus::WriteFailed));
                        lost = true;
                        break;
                    }

                    entry.writes++;
                    entry.lastValue = payload;
                    writes_.fetch_add(1);
                }
            }

            if (lost)
            {
                Detach();
                continue;
            }

            status_.store(static_cast<int>(MemStatus::Writing));
        }
        else
        {
            status_.store(static_cast<int>(MemStatus::Ready));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }

    Detach();
}
